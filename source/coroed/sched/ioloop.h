#pragma once

#include <stdint.h>

// I/O event loop using epoll (Linux only)

struct task;

// Initialize I/O event loop
void ioloop_init();

// Destroy I/O event loop
void ioloop_destroy();

// Register a task to wait for events on fd
void ioloop_register(int fd, uint32_t events, struct task* task);

// Poll for I/O events and wake up waiting tasks
// Returns number of tasks woken up
int ioloop_poll(int timeout_ms);

// Unregister fd from I/O loop
void ioloop_unregister(int fd);

