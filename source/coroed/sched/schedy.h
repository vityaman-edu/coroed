#pragma once

#include <stdint.h>

#include "coroed/api/task.h"
#include "uthread.h"

struct task;

void sched_init();

task_t sched_submit(uthread_routine entry, void* argument);

void sched_start();

void sched_wait();

void sched_print_statistics();

void sched_destroy();

void sched_block_on_fd(struct task* task, int fd, uint32_t events);

void sched_poll_io_and_wake(int timeout_ms);

void sched_wake_task(struct task* task);
