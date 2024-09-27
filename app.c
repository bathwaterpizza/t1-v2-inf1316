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
