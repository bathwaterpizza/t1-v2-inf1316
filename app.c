#include "cfg.h"
#include "types.h"
#include "util.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

static int *shm;
static int app_id;
static int counter = 0;
static int syscall_pipe[2];

// Called when app receives SIGUSR1 from kernelsim
// Saves state in shm and raises SIGSTOP
static void handle_kernel_stop(int signum) {
  msg("App %d stopped at counter %d", app_id + 1, counter);

  // Save program counter state to shm
  set_app_counter(shm, app_id, counter);

  // Wait for continue from kernelsim
  raise(SIGSTOP);
}

// Generate a random syscall from options (D1/D2 + R/W/X)
static syscall_t rand_syscall(void) {
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
  msg("App %d resumed at counter %d", app_id + 1, counter);

  // Restore program counter state from shm
  counter = get_app_counter(shm, app_id);

  // Restore syscall state from shm
  if (get_app_syscall(shm, app_id) != SYSCALL_NONE) {
    // announce syscall completed and change status to none
    dmsg("App %d completed syscall: %s", app_id + 1,
         SYSCALL_STR[get_app_syscall(shm, app_id)]);
    set_app_syscall(shm, app_id, SYSCALL_NONE);
  }
}

// Called by parent on Ctrl+C.
// Cleanup and exit
static void handle_sigterm(int signum) {
  dmsg("App %d stopping from SIGTERM", app_id + 1);
  close(syscall_pipe[PIPE_WRITE]); // close write
  shmdt(shm);
  exit(0);
}

// Sends a syscall request to kernelsim
static void send_syscall(syscall_t call) {
  // There should be no pending syscalls
  assert(get_app_syscall(shm, app_id) == SYSCALL_NONE);

  dmsg("App %d started syscall: %s", app_id + 1, SYSCALL_STR[call]);

  // Set desired syscall and send request to kernelsim
  set_app_syscall(shm, app_id, call);
  write(syscall_pipe[PIPE_WRITE], &app_id, sizeof(int));

  // Wait for SIGUSR1->SIGSTOP
  pause();
}

int main(int argc, char **argv) {
  // Get IDs from command line
  int shm_id = atoi(argv[1]);
  app_id = atoi(argv[2]);

  // Pipe setup
  syscall_pipe[PIPE_READ] = atoi(argv[3]);
  syscall_pipe[PIPE_WRITE] = atoi(argv[4]);
  close(syscall_pipe[PIPE_READ]); // close read

  dmsg("App %d booting", app_id + 1);
  assert(argc == 5);
  srand(time(NULL)); // reset seed

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

  // Attach to kernelsim shm
  shm = (int *)shmat(shm_id, NULL, 0);

  // Begin paused
  raise(SIGSTOP);

  dmsg("App %d running", app_id + 1);

  // Main application loop
  while (counter < APP_MAX_PC) {
    usleep((APP_SLEEP_TIME_MS / 2) * 1000);

    // TODO: semaphores for concurrency
    if (rand() % 100 < APP_SYSCALL_PROB) {
      send_syscall(rand_syscall());
    }

    counter++;
    dmsg("App %d counter is now %d", app_id + 1, counter);

    usleep((APP_SLEEP_TIME_MS / 2) * 1000);
  }

  // update context before exiting
  set_app_counter(shm, app_id, counter);

  // cleanup
  close(syscall_pipe[PIPE_WRITE]); // close read
  shmdt(shm);
  dmsg("App %d finished", app_id + 1);

  return 0;
}
