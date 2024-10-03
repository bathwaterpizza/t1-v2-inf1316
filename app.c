#include "cfg.h"
#include "types.h"
#include "util.h"
#include <assert.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

// Shared memory segment between apps and kernel
static int *shm;
// App ID received from kernelsim
static int app_id;
// Internal program counter to demonstrate context switching
static int counter = 0;
// Pipe fds for sending a syscall request to kernelsim
static int syscall_pipe_fd[2];
// Semaphore to avoid a syscall while the dispatcher is making a decision
static sem_t *dispatch_sem;
// Used to differentiate kernel unpause SIGCONT from timesharing SIGCONT
static bool app_waiting_syscall_block = false;

// Called when app receives SIGUSR1 from kernelsim
// Saves context in shm and raises SIGSTOP
static void handle_kernel_stop(int signum) {
  msg("App %d stopped at counter %d", app_id + 1, counter);

  app_waiting_syscall_block = false;

  // Save program counter state to shm
  set_app_counter(shm, app_id, counter);

  // Simulate data loss
  counter = 0;

  // Wait for continue from kernelsim
  raise(SIGSTOP);
}

// Generate a random syscall from options (D1/D2 + R/W/X)
static inline syscall_t rand_syscall(void) {
  int r = rand();

  switch (r % 6) {
  case 0:
    return SYSCALL_D1_R;
  case 1:
    return SYSCALL_D1_W;
  case 2:
    return SYSCALL_D1_X;
  case 3:
    return SYSCALL_D2_R;
  case 4:
    return SYSCALL_D2_W;
  case 5:
    return SYSCALL_D2_X;
  default:
    fprintf(stderr, "rand_syscall error\n");
    exit(5);
  }
}

// Called when app receives SIGCONT from kernelsim
// Restores state from shm
static void handle_kernel_cont(int signum) {
  // Check if it's a SIGCONT from a kernel unpause
  if (app_waiting_syscall_block) {
    dmsg("App %d resumed from kernel pause", app_id + 1);
    pause();
    return;
  }

  // Restore program counter state from shm
  counter = get_app_counter(shm, app_id);

  msg("App %d resumed at counter %d", app_id + 1, counter);

  // Restore syscall state from shm
  sem_wait(dispatch_sem);
  if (get_app_syscall(shm, app_id) != SYSCALL_NONE) {
    // announce syscall completed and change status to none
    dmsg("App %d completed syscall: %s", app_id + 1,
         SYSCALL_STR[get_app_syscall(shm, app_id)]);
    set_app_syscall(shm, app_id, SYSCALL_NONE);
  }
  sem_post(dispatch_sem);
}

// Called by parent on Ctrl+C.
// Cleanup and exit
static void handle_sigterm(int signum) {
  dmsg("App %d stopping from SIGTERM", app_id + 1);

  // cleanup
  close(syscall_pipe_fd[PIPE_WRITE]); // close write
  shmdt(shm);
  sem_close(dispatch_sem);
  exit(0);
}

// Sends a syscall request to kernelsim
static void send_syscall(syscall_t call) {
  // There should be no pending syscalls
  assert(get_app_syscall(shm, app_id) == SYSCALL_NONE);

  dmsg("App %d started syscall: %s", app_id + 1, SYSCALL_STR[call]);

  // Set desired syscall and send request to kernelsim
  set_app_syscall(shm, app_id, call);
  write(syscall_pipe_fd[PIPE_WRITE], &app_id, sizeof(int));

  // Wait for SIGUSR1->SIGSTOP
  sem_post(dispatch_sem);
  app_waiting_syscall_block = true;
  pause();
}

// Called on segfault, necessary in order to show a messsage if it happens
static void handle_sigsegv(int signum) {
  dmsg("App %d segmentation fault!", app_id + 1);

  // cleanup
  close(syscall_pipe_fd[PIPE_WRITE]); // close write
  shmdt(shm);
  sem_close(dispatch_sem);

  exit(12);
}

int main(int argc, char **argv) {
  assert(argc == 5);

  srand(time(NULL) ^ (getpid() << 16)); // reset seed

  // Get IDs from command line
  int shm_id = atoi(argv[1]);
  app_id = atoi(argv[2]);

  dmsg("App %d booting", app_id + 1);

  // Pipe setup
  syscall_pipe_fd[PIPE_READ] = atoi(argv[3]);
  syscall_pipe_fd[PIPE_WRITE] = atoi(argv[4]);
  close(syscall_pipe_fd[PIPE_READ]); // close read

  // Register signal callbacks
  if (signal(SIGUSR1, handle_kernel_stop) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }
  if (signal(SIGCONT, handle_kernel_cont) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }
  if (signal(SIGTERM, handle_sigterm) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }
  if (signal(SIGSEGV, handle_sigsegv) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }

  // Attach to kernelsim shm
  shm = (int *)shmat(shm_id, NULL, 0);

  // Get semaphore created by kernelsim
  dispatch_sem = sem_open(DISPATCH_SEM_NAME, 0);
  if (dispatch_sem == SEM_FAILED) {
    fprintf(stderr, "Semaphore error\n");
    exit(11);
  }

  // Begin paused
  raise(SIGSTOP);

  dmsg("App %d running", app_id + 1);

  // Main application loop
  while (counter < APP_MAX_PC) {
    usleep((APP_SLEEP_TIME_MS / 2) * 1000);

    sem_wait(dispatch_sem);
    if (rand() % 100 < APP_SYSCALL_PROB) {
      send_syscall(rand_syscall());
    } else {
      sem_post(dispatch_sem);
    }

    counter++;
    dmsg("App %d counter increased to %d", app_id + 1, counter);

    usleep((APP_SLEEP_TIME_MS / 2) * 1000);
  }

  msg("App %d left main loop", app_id + 1);

  // update context before exiting
  // write to notify that app finished
  sem_wait(dispatch_sem);
  set_app_syscall(shm, app_id, SYSCALL_APP_FINISHED);
  set_app_counter(shm, app_id, counter);
  write(syscall_pipe_fd[PIPE_WRITE], &app_id, sizeof(int));
  sem_post(dispatch_sem);

  // cleanup
  close(syscall_pipe_fd[PIPE_WRITE]); // close read
  shmdt(shm);
  sem_close(dispatch_sem);

  msg("App %d finished", app_id + 1);

  return 0;
}
