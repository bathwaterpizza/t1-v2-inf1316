#include "cfg.h"
#include "types.h"
#include "util.h"
#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// Controls whether the main loop continues
static bool intersim_running;

// Called by parent on Ctrl+C or all apps finished.
// Cleanup and exit
static void handle_sigterm(int signum) {
  dmsg("Intersim stopping from SIGTERM");

  intersim_running = false;
}

int main(int argc, char **argv) {
  dmsg("Intersim booting");
  assert(argc == 4);
  srand(time(NULL) ^ (getpid() << 16)); // reset seed
  if (signal(SIGTERM, handle_sigterm) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }

  // Get pipe fds from parent
  int interpipe_fd[] = {atoi(argv[1]), atoi(argv[2])};
  close(interpipe_fd[PIPE_READ]); // close read
  close(atoi(argv[3]));           // close app read inherited from parent

  // Start paused
  raise(SIGSTOP);

  intersim_running = true;
  msg("Intersim running");

  // Main loop
  while (intersim_running) {
    usleep((INTERSIM_SLEEP_TIME_MS / 2) * 1000);

    // Send timeslice interrupt
    irq_t irq = IRQ_TIME;
    write(interpipe_fd[PIPE_WRITE], &irq, sizeof(irq_t));

    dmsg("Intersim sent timeslice interrupt");

    // Randomly send D1 and D2 interrupts
    if (rand() % 100 < INTERSIM_D1_INT_PROB) {
      irq = IRQ_D1;

      write(interpipe_fd[PIPE_WRITE], &irq, sizeof(irq_t));
      dmsg("Intersim sent device interrupt D%d", irq);
    }
    if (rand() % 100 < INTERSIM_D2_INT_PROB) {
      irq = IRQ_D2;

      write(interpipe_fd[PIPE_WRITE], &irq, sizeof(irq_t));
      dmsg("Intersim sent device interrupt D%d", irq);
    }

    usleep((INTERSIM_SLEEP_TIME_MS / 2) * 1000);
  }

  dmsg("Intersim left main loop");

  close(interpipe_fd[PIPE_WRITE]);
  msg("Intersim finished");

  return 0;
}
