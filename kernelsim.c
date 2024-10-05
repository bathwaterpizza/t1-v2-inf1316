#include "cfg.h"
#include "types.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Whether the kernel is running and reading the interrupt controller pipe
static volatile sig_atomic_t kernel_running = false;
// Whether the kernel has been paused by a SIGUSR1
static volatile sig_atomic_t kernel_paused = false;
// Queue of apps waiting on device D1
static queue_t *D1_app_queue;
// Queue of apps waiting on device D2
static queue_t *D2_app_queue;
// Round-robin queue of apps waiting to run
static queue_t *dispatch_queue;
// PID of the intersim process
static pid_t intersim_pid;
// Array of app info structs
static proc_info_t apps[APP_AMOUNT];
// Shared memory segment between apps and kernel
static int *shm;
// Semaphore to avoid a syscall while the dispatcher is making a decision
static sem_t *dispatch_sem;

// Updates the stats of an app according to the syscall type
static inline void update_app_stats(syscall_t call, int app_id) {
  switch (call) {
  case SYSCALL_D1_R:
    apps[app_id].D1_access_count++;
    apps[app_id].read_count++;
    break;
  case SYSCALL_D1_W:
    apps[app_id].D1_access_count++;
    apps[app_id].write_count++;
    break;
  case SYSCALL_D1_X:
    apps[app_id].D1_access_count++;
    apps[app_id].exec_count++;
    break;
  case SYSCALL_D2_R:
    apps[app_id].D2_access_count++;
    apps[app_id].read_count++;
    break;
  case SYSCALL_D2_W:
    apps[app_id].D2_access_count++;
    apps[app_id].write_count++;
    break;
  case SYSCALL_D2_X:
    apps[app_id].D2_access_count++;
    apps[app_id].exec_count++;
    break;
  default:
    fprintf(stderr, "update_app_stats error\n");
    exit(7);
  }
}

// Returns whether all apps have finished executing
static bool all_apps_finished(void) {
  for (int i = 0; i < APP_AMOUNT; i++) {
    if (apps[i].state != FINISHED) {
      return false;
    }
  }

  return true;
}

// Returns the appid of the current running app
static int get_running_appid(void) {
  for (int i = 0; i < APP_AMOUNT; i++) {
    if (apps[i].state == RUNNING) {
      return apps[i].app_id;
    }
  }

  return -1;
}

// Returns how many apps are either blocked or have finished
static int amount_apps_not_ready(void) {
  int count = 0;

  for (int i = 0; i < APP_AMOUNT; i++) {
    if (apps[i].state == FINISHED || apps[i].state == BLOCKED) {
      count++;
    }
  }

  return count;
}

static inline bool has_pending_syscall(int app_id) {
  return get_app_syscall(shm, app_id) != SYSCALL_NONE;
}

// Handles an incoming syscall from the apps syscall pipe
static void handle_app_syscall(int app_id) {
  assert(apps[app_id].state == RUNNING);

  syscall_t call = get_app_syscall(shm, app_id);

  assert(call != SYSCALL_NONE);

  if (call == SYSCALL_APP_FINISHED) {
    dmsg("Kernel got finished app %d", app_id + 1);

    apps[app_id].state = FINISHED;

    if (all_apps_finished()) {
      dmsg("Syscall handler: All apps finished");
      kernel_running = false;
      kill(intersim_pid, SIGTERM);
    }

    return;
  }

  // Device syscall. Save, block, update stats, enqueue.
  apps[app_id].state = BLOCKED;
  kill(apps[app_id].app_pid, SIGUSR1); // save state
  update_app_stats(call, app_id);

  if (call >= SYSCALL_D1_R && call <= SYSCALL_D1_X) {
    enqueue(D1_app_queue, app_id);
  } else {
    enqueue(D2_app_queue, app_id);
  }

  dmsg("App %d blocked for syscall: %s", app_id + 1, SYSCALL_STR[call]);
}

// Called on Ctrl+C.
// Terminate children, cleanup and exit
static void handle_sigint(int signum) {
  printf("\n");
  fflush(stdout);
  msg("Kernel stopping from SIGINT");

  // kill all apps
  for (int i = 0; i < APP_AMOUNT; i++) {
    if (apps[i].state != FINISHED) {
      kill(apps[i].app_pid, SIGTERM);
    }
  }

  // kill intersim
  kill(intersim_pid, SIGTERM);

  // and exit from main
  kernel_paused = false;
  kernel_running = false;
}

// Stops current running app and dispatch the next app in queue
static void dispatch_next_app(void) {
  // Check if we're done
  if (all_apps_finished()) {
    dmsg("Dispatcher: All apps finished");
    kernel_running = false;
    kill(intersim_pid, SIGTERM);

    return;
  }

  int cur_app_id = get_running_appid();

  // Pause app unless it's the only ready one, or has a pending syscall
  if (cur_app_id != -1 && amount_apps_not_ready() < (APP_AMOUNT - 1) &&
      !has_pending_syscall(cur_app_id)) {
    // Pause and insert into dispatch queue
    assert(apps[cur_app_id].state == RUNNING);
    dmsg("Dispatcher pausing app %d", cur_app_id + 1);

    apps[cur_app_id].state = PAUSED;
    kill(apps[cur_app_id].app_pid, SIGUSR1);
    enqueue(dispatch_queue, cur_app_id);
  } else {
    // No apps to pause
    dmsg("Dispatcher found no apps to pause");
  }

  // Dispatch next app
  int next_app_id = dequeue(dispatch_queue);
  if (next_app_id != -1) {
    assert(apps[next_app_id].state == PAUSED);
    dmsg("Dispatcher continued app %d", next_app_id + 1);
    apps[next_app_id].state = RUNNING;
    kill(apps[next_app_id].app_pid, SIGCONT);
  } else {
    dmsg("Dispatcher found no apps to continue");
  }
}

// Prints proc_info_t and shm state for each app
static void dump_apps_info(void) {
  for (int i = 0; i < APP_AMOUNT; i++) {
    msg("----------- App %d -----------", i + 1);
    msg("Counter        | %d", get_app_counter(shm, i));
    msg("State          | %s", PROC_STATE_STR[apps[i].state]);
    msg("Pending call   | %s", SYSCALL_STR[get_app_syscall(shm, i)]);
    msg("D1/D2 access   | %d / %d", apps[i].D1_access_count,
        apps[i].D2_access_count);
    msg("R/W/X requests | %d / %d / %d", apps[i].read_count,
        apps[i].write_count, apps[i].exec_count);
  }

  msg("-----------------------------");
}

// Called on SIGUSR1.
// Pauses or unpauses intersim, the current running app, and the kernelsim.
// Dumps apps info after pausing
static void handle_pause(int signum) {
  int running_app = get_running_appid();

  if (kernel_paused) {
    // unpause
    if (running_app != -1) {
      kill(apps[running_app].app_pid, SIGCONT);
    }
    kill(intersim_pid, SIGCONT);

    kernel_paused = false;
    msg("Kernel resumed");
  } else {
    // pause and dump apps info
    if (running_app != -1) {
      kill(apps[running_app].app_pid, SIGSTOP);
    }
    kill(intersim_pid, SIGSTOP);

    dump_apps_info();

    kernel_paused = true;
    msg("Kernel paused");

    // pause kernelsim and don't block SIGUSR1
    sigset_t mask;
    sigemptyset(&mask);
    sigsuspend(&mask);
  }
}

// Dequeue app from device queue and change its blocked state,
// then add it to the dispatch queue
static void unblock_next_app(irq_t irq) {
  int app_id = (irq == IRQ_D1) ? dequeue(D1_app_queue) : dequeue(D2_app_queue);

  if (app_id == -1) {
    dmsg("No apps waiting on D%d", irq);
    return;
  }

  assert(apps[app_id].state == BLOCKED);
  apps[app_id].state = PAUSED;
  enqueue(dispatch_queue, app_id);

  dmsg("Kernel unblocked app %d", app_id + 1);
}

int main(void) {
  srand(time(NULL) ^ (getpid() << 16)); // reset seed
  dmsg("Kernel booting");
  // Validate some configs
  assert(APP_MAX_PC > 0);
  assert(APP_SLEEP_TIME_MS > 0);
  assert(INTERSIM_SLEEP_TIME_MS > 0);
  assert(APP_SYSCALL_PROB >= 0 && APP_SYSCALL_PROB <= 100);

  // Register signal handlers
  if (signal(SIGINT, handle_sigint) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }
  if (signal(SIGUSR1, handle_pause) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }

  // Allocate shared memory to store app states (simulating a snapshot)
  int shm_id =
      shmget(IPC_PRIVATE, sizeof(int) * 2 * APP_AMOUNT, IPC_CREAT | S_IRWXU);
  if (shm_id < 0) {
    fprintf(stderr, "Shm alloc error\n");
    exit(3);
  }

  shm = (int *)shmat(shm_id, NULL, 0);
  memset(shm, 0, sizeof(int) * 2 * APP_AMOUNT);

  // Create semaphore for avoiding race conditions
  sem_unlink(DISPATCH_SEM_NAME); // remove any existing semaphore
  dispatch_sem = sem_open(DISPATCH_SEM_NAME, O_CREAT, 0666, 1);
  if (dispatch_sem == SEM_FAILED) {
    fprintf(stderr, "Semaphore error\n");
    exit(11);
  }

  // Create apps pipe
  int apps_pipe_fd[2];
  if (pipe(apps_pipe_fd) == -1) {
    fprintf(stderr, "Pipe error\n");
    exit(8);
  }

  // Allocate device waiting and dispatch queues
  D1_app_queue = create_queue();
  D2_app_queue = create_queue();
  dispatch_queue = create_queue();

  // Spawn apps
  for (int i = 0; i < APP_AMOUNT; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "Fork error\n");
      exit(2);
    } else if (pid == 0) {
      // child
      // passing shm_id and app_id as args, and pipe fds
      char shm_id_str[10];
      char app_id_str[10];
      char pipe_read_str[10];
      char pipe_write_str[10];
      sprintf(shm_id_str, "%d", shm_id);
      sprintf(app_id_str, "%d", i);
      sprintf(pipe_read_str, "%d", apps_pipe_fd[PIPE_READ]);
      sprintf(pipe_write_str, "%d", apps_pipe_fd[PIPE_WRITE]);

      execlp("./app", "app", shm_id_str, app_id_str, pipe_read_str,
             pipe_write_str, NULL);
    }

    apps[i].app_id = i;
    apps[i].app_pid = pid;
    apps[i].D1_access_count = 0;
    apps[i].D2_access_count = 0;
    apps[i].read_count = 0;
    apps[i].write_count = 0;
    apps[i].exec_count = 0;
    apps[i].state = PAUSED;

    enqueue(dispatch_queue, i); // add app to dispatch queue
  }

  close(apps_pipe_fd[PIPE_WRITE]); // close write

  // Create interrupts pipe
  int interpipe_fd[2];
  if (pipe(interpipe_fd) == -1) {
    fprintf(stderr, "Pipe error\n");
    exit(8);
  }

  // Spawn intersim
  intersim_pid = fork();
  if (intersim_pid < 0) {
    fprintf(stderr, "Fork error\n");
    exit(2);
  } else if (intersim_pid == 0) {
    // child
    // passing pipe fds as args, as well as the apps read pipe that needs to be
    // closed, as it's being inherited
    char pipe_read_str[10];
    char pipe_write_str[10];
    char app_pipe_read_str[10];
    sprintf(pipe_read_str, "%d", interpipe_fd[PIPE_READ]);
    sprintf(pipe_write_str, "%d", interpipe_fd[PIPE_WRITE]);
    sprintf(app_pipe_read_str, "%d", apps_pipe_fd[PIPE_READ]);

    execlp("./intersim", "intersim", pipe_read_str, pipe_write_str,
           app_pipe_read_str, NULL);
  }

  close(interpipe_fd[PIPE_WRITE]); // close write

  // Wait for all processes to boot, start kernel and intersim
  sleep(1);
  kernel_running = true;
  msg("Kernel running");
  kill(intersim_pid, SIGCONT);

  // Setup for reading both pipes without blocking
  fd_set fdset;
  int max_fd = interpipe_fd[PIPE_READ] > apps_pipe_fd[PIPE_READ]
                   ? interpipe_fd[PIPE_READ]
                   : apps_pipe_fd[PIPE_READ];

  // Main loop for reading pipes
  while (kernel_running) {
    irq_t irq;
    int syscall_app_id;

    // Read both pipes
    FD_ZERO(&fdset);
    FD_SET(apps_pipe_fd[PIPE_READ], &fdset);
    FD_SET(interpipe_fd[PIPE_READ], &fdset);

    // This handling is necessary in case select gets interrupted by a signal
    int select_result;

    do {
      select_result = select(max_fd + 1, &fdset, NULL, NULL, NULL);
    } while (select_result == -1 && errno == EINTR);

    if (select_result == -1) {
      fprintf(stderr, "Select error\n");
      exit(10);
    }

    if (FD_ISSET(apps_pipe_fd[PIPE_READ], &fdset)) {
      // Got syscall from app
      read(apps_pipe_fd[PIPE_READ], &syscall_app_id, sizeof(int));

      handle_app_syscall(syscall_app_id);
    }
    if (FD_ISSET(interpipe_fd[PIPE_READ], &fdset)) {
      // Got interrupt from intersim
      read(interpipe_fd[PIPE_READ], &irq, sizeof(irq_t));

      if (irq == IRQ_TIME) {
        // Time interrupt
        sem_wait(dispatch_sem);
        dmsg("Kernel got time interrupt");

        dispatch_next_app();
        sem_post(dispatch_sem);
      } else {
        // Device interrupt
        assert(irq == IRQ_D1 || irq == IRQ_D2);
        dmsg("Kernel got device interrupt D%d", irq);

        unblock_next_app(irq);
      }
    }
  }

  msg("Kernel left main loop");

  // Cleanup
  free_queue(D1_app_queue);
  free_queue(D2_app_queue);
  free_queue(dispatch_queue);
  shmdt(shm);
  shmctl(shm_id, IPC_RMID, NULL);
  close(interpipe_fd[PIPE_READ]);
  close(apps_pipe_fd[PIPE_READ]);
  sem_close(dispatch_sem);
  sem_unlink(DISPATCH_SEM_NAME);

  msg("Kernel finished");
  sleep(1); // wait for children cleanup

  return 0;
}
