#pragma once

// Show debug logging on console
#define DEBUG

// How many application processes should be created
#define APP_AMOUNT 3
// Program counter value at which the apps terminate
#define APP_MAX_PC 5
// How long should +1 counter increment take
#define APP_SLEEP_TIME_MS 1000
// Percentage chance of app sending a syscall during each iteration
#define APP_SYSCALL_PROB 15

// How often to generate a timeslice interrupt
#define INTERSIM_SLEEP_TIME_MS 500
// Percentage chance of generating a D1/D2 interrupt with each timeslice change
#define INTERSIM_D1_INT_PROB 10
#define INTERSIM_D2_INT_PROB 5

// Name of dispatch semaphore
#define DISPATCH_SEM_NAME "/kernelsim_dispatch_sem"
