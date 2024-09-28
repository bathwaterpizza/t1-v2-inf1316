#include "cfg.h"
#include "types.h"
#include "util.h"
#include <assert.h>
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
static bool kernel_running;
// Queue for apps waiting on device D1
static queue_t *D1_app_queue;
// Queue for apps waiting on device D2
static queue_t *D2_app_queue;
// Queue for apps waiting to run
static queue_t *dispatch_queue;
// PID of the intersim process
static pid_t intersim_pid;
// Array of app info structs
static proc_info_t apps[APP_AMOUNT];
// Shared memory segment between apps and kernel
static int *shm;

// Returns the appid of the current running app
static int get_running_appid(void) {
  for (int i = 0; i < APP_AMOUNT; i++) {
    if (apps[i].state == RUNNING) {
      return apps[i].app_id;
    }
  }

  return -1;
}

// Updates the stats of an app according to the syscall type
static void update_app_stats(syscall_t call, int app_id) {
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

// Called on Ctrl+C.
// Terminate children, cleanup and exit
static void handle_sigint(int signum) {
  msg("Kernel stopping from SIGINT");

  // kill all apps
  for (int i = 0; i < APP_AMOUNT; i++) {
    kill(apps[i].app_pid, SIGTERM);
  }

  // kill intersim
  kill(intersim_pid, SIGTERM);

  // and exit from main
  kernel_running = false;
}

int main(void) {
  srand(time(NULL));
  dmsg("Kernel booting");
  // Validate some configs
  assert(APP_MAX_PC > 0);
  assert(APP_SLEEP_TIME_MS > 0);
  assert(APP_SYSCALL_PROB >= 0 && APP_SYSCALL_PROB <= 100);

  // Register signal handlers
  if (signal(SIGINT, handle_sigint) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }

  // Allocate shared memory to store app states (simulating a snapshot)
  int shm_id = shmget(IPC_PRIVATE, SHM_SIZE, IPC_CREAT | S_IRWXU);
  if (shm_id < 0) {
    fprintf(stderr, "Shm alloc error\n");
    exit(3);
  }

  shm = (int *)shmat(shm_id, NULL, 0);
  memset(shm, 0, SHM_SIZE);

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
  dmsg("Kernel running");
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

    if (select(max_fd + 1, &fdset, NULL, NULL, NULL) < 0) {
      fprintf(stderr, "Select error\n");
      exit(10);
    }

    if (FD_ISSET(apps_pipe_fd[PIPE_READ], &fdset)) {
      // Got syscall from app
      read(apps_pipe_fd[PIPE_READ], &syscall_app_id, sizeof(int));
    }
    if (FD_ISSET(interpipe_fd[PIPE_READ], &fdset)) {
      // Got interrupt from intersim
      read(interpipe_fd[PIPE_READ], &irq, sizeof(irq_t));
    }
  }

  // Cleanup
  free_queue(D1_app_queue);
  free_queue(D2_app_queue);
  free_queue(dispatch_queue);
  shmdt(shm);
  shmctl(shm_id, IPC_RMID, NULL);
  close(interpipe_fd[PIPE_READ]);
  close(apps_pipe_fd[PIPE_READ]);
  msg("Kernel finished");

  return 0;
}
