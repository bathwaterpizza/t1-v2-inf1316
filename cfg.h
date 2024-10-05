#pragma once

// Show debug logging on console
#define DEBUG

// How many application processes should be created
#define APP_AMOUNT 3
// Program counter value at which the app terminates
#define APP_MAX_PC 5
// How long should one counter iteration of each app be
#define APP_SLEEP_TIME_MS 1000
// Percentage chance of app sending a syscall for each iteration
#define APP_SYSCALL_PROB 15

// How often should we generate a timeslice interrupt
#define INTERSIM_SLEEP_TIME_MS 500
// Percentage chance of generating a D1/D2 interrupt for each intersim iteration
#define INTERSIM_D1_INT_PROB 10
#define INTERSIM_D2_INT_PROB 5

// Size of shm for each app process
#define APP_SHM_SIZE (sizeof(int) * 2)
// Total size of shm segment
#define SHM_SIZE (APP_SHM_SIZE * APP_AMOUNT)

// Name of dispatch semaphore
#define DISPATCH_SEM_NAME "/kernelsim_dispatch_sem"
