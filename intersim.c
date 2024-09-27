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

static bool intersim_running;

// Called by parent on Ctrl+C or all apps finished.
// Cleanup and exit
static void handle_sigterm(int signum) {
  msg("Intersim stopping from SIGTERM");

  intersim_running = false;
}

static void send_device_int(int *fd, irq_t irq) {
  write(fd[PIPE_WRITE], &irq, sizeof(irq_t));
  dmsg("Intersim sent device interrupt D%d", irq);
}

int main(int argc, char **argv) {
  dmsg("Intersim booting");
  assert(argc == 3);
  srand(time(NULL));
  if (signal(SIGTERM, handle_sigterm) == SIG_ERR) {
    fprintf(stderr, "Signal error\n");
    exit(4);
  }

  // Get pipe fds from parent
  int interpipe_fd[] = {atoi(argv[1]), atoi(argv[2])};
  close(interpipe_fd[PIPE_READ]); // close read

  // Start paused
  raise(SIGSTOP);

  intersim_running = true;
  dmsg("Intersim running");

  // Main loop
  while (intersim_running) {
    usleep((INTERSIM_SLEEP_TIME_MS / 2) * 1000);

    // Send timeslice interrupt
    irq_t irq = IRQ_TIME;
    write(interpipe_fd[PIPE_WRITE], &irq, sizeof(irq_t));

    dmsg("Intersim sent timeslice interrupt");

    // Randomly send D1 and D2 interrupts
    if (rand() % 100 < INTERSIM_D1_INT_PROB) {
      send_device_int(interpipe_fd, IRQ_D1);
    }
    if (rand() % 100 < INTERSIM_D2_INT_PROB) {
      send_device_int(interpipe_fd, IRQ_D2);
    }

    usleep((INTERSIM_SLEEP_TIME_MS / 2) * 1000);
  }

  close(interpipe_fd[PIPE_WRITE]);
  dmsg("Intersim finished");

  return 0;
}
