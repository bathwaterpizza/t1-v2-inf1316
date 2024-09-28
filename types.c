#include "types.h"

const char *SYSCALL_STR[] = {"None",          "Read from D1", "Write to D1",
                             "Execute on D1", "Read from D2", "Write to D2",
                             "Execute on D2", "App finished"};

const char *PROC_STATE_STR[] = {"Running", "Blocked", "Paused", "Finished"};
