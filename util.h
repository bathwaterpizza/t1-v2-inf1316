#pragma once

#include "types.h"

// printf + timestamp
void msg(const char *format, ...);

// printf + timestamp for DEBUG only
void dmsg(const char *format, ...);

// Get program counter value from shm for the given app_id
int get_app_counter(int *shm, int app_id);

// Get syscall request status from shm for the given app_id
syscall_t get_app_syscall(int *shm, int app_id);

// Set program counter value in shm for the given app_id
void set_app_counter(int *shm, int app_id, int value);

// Set syscall request status in shm for the given app_id
void set_app_syscall(int *shm, int app_id, syscall_t call);

// Allocates a queue for storing app_ids as ints
queue_t *create_queue(void);

// Frees a queue
void free_queue(queue_t *q);

// Enqueues an app_id to the queue
void enqueue(queue_t *q, int value);

// Dequeues an app_id from the queue
int dequeue(queue_t *q);
