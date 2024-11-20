// Linux
#define _GNU_SOURCE

#include "schedy.h"

#include <assert.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

// Linux
#include <linux/sched.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "spinlock.h"
#include "task.h"
#include "uthread.h"

#define SCHED_THREADS_LIMIT 64

#define SCHED_WORKER_STACK_SIZE (size_t)(1024 * 1024)
#define SCHED_WORKERS_COUNT (size_t)(8)

#define SCHED_NEXT_MAX_ATTEMPTS (size_t)(8)

struct task {
  struct uthread* thread;
  struct worker* worker;
  enum {
    UTHREAD_RUNNABLE,
    UTHREAD_RUNNING,
    UTHREAD_CANCELLED,
    UTHREAD_ZOMBIE,
  } state;
  struct spinlock lock;
};

struct task tasks[SCHED_THREADS_LIMIT];

struct worker {
  struct uthread sched_thread;
  size_t curr_index;
  struct {
    size_t steps;
  } statistics;
};

thread_local struct worker worker = {
    .curr_index = 0,
    .statistics = {.steps = 0},
};

void sched_init() {
  for (size_t i = 0; i < SCHED_THREADS_LIMIT; ++i) {
    struct task* task = &tasks[i];
    task->thread = NULL;
    task->state = UTHREAD_ZOMBIE;
    spinlock_init(&task->lock);
  }
}

void sched_switch_to_scheduler(struct task* task) {
  struct uthread* sched = &task->worker->sched_thread;
  task->worker = NULL;
  uthread_switch(task->thread, sched);
}

void sched_switch_to(struct task* task) {
  assert(task->thread != &worker.sched_thread);

  task->worker = &worker;

  struct uthread* sched = &worker.sched_thread;
  uthread_switch(sched, task->thread);
}

struct task* sched_next();

int sched_loop(void* argument) {
  (void)argument;

  for (;;) {
    struct task* task = sched_next();
    if (task == NULL) {
      break;
    }

    task->state = UTHREAD_RUNNING;

    sched_switch_to(task);

    if (task->state == UTHREAD_CANCELLED) {
      uthread_reset(task->thread);
      task->state = UTHREAD_ZOMBIE;
    } else if (task->state == UTHREAD_RUNNING) {
      task->state = UTHREAD_RUNNABLE;
    } else {
      assert(false);
    }

    spinlock_unlock(&task->lock);

    worker.statistics.steps += 1;
  }

  return 0;
}

void sched_start() {
  thrd_t workers[SCHED_WORKERS_COUNT];
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    int code = thrd_create(&workers[i], sched_loop, /* arg = */ NULL);
    assert(code == thrd_success);
  }

  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    int status = 0;
    int code = thrd_join(workers[i], &status);
    assert(code == thrd_success);
  }
}

struct task* sched_next() {
  for (size_t attempt = 0; attempt < SCHED_NEXT_MAX_ATTEMPTS; ++attempt) {
    for (size_t i = 0; i < SCHED_THREADS_LIMIT; ++i) {
      struct task* task = &tasks[worker.curr_index];
      if (!spinlock_try_lock(&task->lock)) {
        continue;
      }

      worker.curr_index = (worker.curr_index + 1) % SCHED_THREADS_LIMIT;
      if (task->thread != NULL && task->state == UTHREAD_RUNNABLE) {
        return task;
      }

      spinlock_unlock(&task->lock);
    }
    sleep(1);
  }

  return NULL;
}

void sched_cancel(struct task* task) {
  task->state = UTHREAD_CANCELLED;
}

void task_yield(struct task* task) {
  sched_switch_to_scheduler(task);
}

void task_exit(struct task* task) {
  sched_cancel(task);
  task_yield(task);
}

void sched_submit(void (*entry)(), void* argument) {
  for (size_t i = 0; i < SCHED_THREADS_LIMIT; ++i) {
    struct task* task = &tasks[i];

    if (!spinlock_try_lock(&task->lock)) {
      continue;
    }

    if (task->thread == NULL) {
      task->thread = uthread_allocate();
      assert(task->thread != NULL);
      task->state = UTHREAD_ZOMBIE;
    }

    const bool is_submitted = task->state == UTHREAD_ZOMBIE;

    if (task->state == UTHREAD_ZOMBIE) {
      uthread_reset(task->thread);
      uthread_set_entry(task->thread, entry);
      uthread_set_arg_0(task->thread, task);
      uthread_set_arg_1(task->thread, argument);
      task->state = UTHREAD_RUNNABLE;
    }

    spinlock_unlock(&task->lock);
    if (is_submitted) {
      return;
    }
  }

  printf("Threads exhausted");
  exit(1);
}

void sched_destroy() {
  for (size_t i = 0; i < SCHED_THREADS_LIMIT; ++i) {
    struct task* task = &tasks[i];
    spinlock_lock(&task->lock);
    if (task->thread != NULL) {
      uthread_free(task->thread);
    }
    spinlock_unlock(&task->lock);
  }
}
