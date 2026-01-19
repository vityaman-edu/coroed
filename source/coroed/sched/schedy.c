#include "schedy.h"

#include <assert.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "coroed/api/task.h"
#include "coroed/core/relax.h"
#include "coroed/core/spinlock.h"
#include "ioloop.h"
#include "kthread.h"
#include "uthread.h"

enum {
  /**
   * Максимальное количество файберов, которое может
   * одновременно выполняться на планировщике.
   */
  SCHED_THREADS_LIMIT = 512,

  /**
   * Количество рабочих потоков для исполнения файберов.
   */
  SCHED_WORKERS_COUNT = (size_t)(8),

  /**
   * Обеспечивает костыль для ретрая операций
   * `submit` и `acquire_next` при высокой
   * конкуренции на спинлоках файберов.
   */
  SCHED_NEXT_MAX_ATTEMPTS = (size_t)(16),
};

/**
 * Задача, выполняющаяся на планировщике.
 *
 * Hint: также тут можно хранить список задач, ожидающих
 *       завершения этой, чтобы уведомить их о данном
 *       событии.
 */
struct task {
  /**
   * Контекст исполнения.
   */
  struct uthread* thread;

  /**
   * Установлен при `UTHREAD_RUNNING`. Указывает на
   * рабочий поток, на котором исполняется задача.
   * Необходим для получения контекста локального
   * планировщика при операциях `yield`, `await`, `submit`.
   */
  struct worker* worker;

  enum {
    /** Готова к исполнению. */
    UTHREAD_RUNNABLE,

    /** Прямо сейчас выполняется. */
    UTHREAD_RUNNING,

    /** Заблокирована на I/O. */
    UTHREAD_BLOCKED,

    /** Завершена и скоро станет зомби. */
    UTHREAD_FINISHED,

    /** Отработала и может быть переиспользования. */
    UTHREAD_ZOMBIE,
  } state;  // Текущее состояние задачи

  struct spinlock lock;
  struct task* next_ready;

  int waiting_fd;
  uint32_t waiting_events;
};

/**
 * Рабочий поток, исполняющий файберы.
 */
struct worker {
  /**
   * Идентификатор рабочего потока.
   */
  size_t index;

  /**
   * Поток операционной системы, на котором
   * исполняется рабочий.
   */
  struct kthread kthread;

  /**
   * Контекст планировщика. Нужно переключиться на него,
   * чтобы вернуться в планировщик.
   */
  struct uthread sched_thread;

  /**
   * В данный момент исполняемая на рабочем
   * задача. Может быть `NULL`.
   */
  struct task* running_task;

  struct {
    size_t steps;     // Сколько шагов было выполнено
    size_t finished;  // Сколько задач было завершено
  } statistics;       // Локальная статистика работяги
};

static struct spinlock tasks_lock;
static struct task tasks[SCHED_THREADS_LIMIT];

static kthread_id_t kthread_ids[SCHED_WORKERS_COUNT];
static struct worker workers[SCHED_WORKERS_COUNT];

struct ready_queue {
  struct spinlock lock;
  struct task* head;
  struct task* tail;
};

static struct ready_queue ready_q;
static struct spinlock alive_lock;
static size_t alive_count = 0;
static void ready_queue_init(struct ready_queue* q) {
  spinlock_init(&q->lock);
  q->head = NULL;
  q->tail = NULL;
}

static void ready_queue_push(struct ready_queue* q, struct task* t) {
  t->next_ready = NULL;
  spinlock_lock(&q->lock);
  if (q->tail) {
    q->tail->next_ready = t;
  } else {
    q->head = t;
  }
  q->tail = t;
  spinlock_unlock(&q->lock);
}

static struct task* ready_queue_pop(struct ready_queue* q) {
  spinlock_lock(&q->lock);
  struct task* t = q->head;
  if (t) {
    q->head = t->next_ready;
    if (!q->head) q->tail = NULL;
    t->next_ready = NULL;
  }
  spinlock_unlock(&q->lock);
  return t;
}

void sched_task_init(struct task* task) {
  task->thread = NULL;
  task->worker = NULL;
  task->state = UTHREAD_ZOMBIE;
  task->next_ready = NULL;
  task->waiting_fd = -1;
  task->waiting_events = 0;
  spinlock_init(&task->lock);
}

/**
 * Установить рабочего в пустое состояние.
 */
void sched_worker_init(struct worker* worker, size_t index) {
  worker->index = index;
  worker->sched_thread.context = NULL;
  worker->running_task = NULL;
  worker->statistics.steps = 0;
  worker->statistics.finished = 0;
}

void sched_init() {
  spinlock_init(&tasks_lock);
  spinlock_init(&alive_lock);
  alive_count = 0;

  ready_queue_init(&ready_q);
  ioloop_init();

  for (size_t i = 0; i < SCHED_THREADS_LIMIT; ++i) {
    sched_task_init(&tasks[i]);
  }
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    kthread_ids[i] = 0;
    sched_worker_init(&workers[i], i);
  }
}

/**
 * Находясь в контексте задачи `task`, переключиться
 * в контекст планировщика.
 */
void sched_switch_to_scheduler(struct task* task) {
  struct uthread* sched = &task->worker->sched_thread;
  task->worker = NULL;
  uthread_switch(task->thread, sched);
}

/**
 * Находясь в контексте планировщика, переключиться
 * в контекст задачи `task`. Выполнять ее до следующего
 * невынужденного возвращения в планировщик.
 */
void sched_switch_to(struct worker* worker, struct task* task) {
  assert(task->thread != &worker->sched_thread);

  task->state = UTHREAD_RUNNING;
  task->worker = worker;
  worker->running_task = task;

  struct uthread* sched = &worker->sched_thread;
  uthread_switch(sched, task->thread);
}

/**
 * Получить следующую задачу на исполнение в
 * соответствии с текущим алгоритмом планирования.
 *
 * Вызывающему передается владение задачей, а также
 * захваченный лок `task->lock`.
 */
struct task* sched_acquire_next();

/**
 * Вернуть задачу в очередь планирования.
 *
 * Очереди передается владение задачей,
 * а также она отпустит лок `task->lock`.
 */
void sched_release(struct task* task);

/**
 * Цикл планировщика. Выполняется, пока есть задачи.
 */
int sched_loop(void* argument) {
  struct worker* worker = argument;
  kthread_ids[worker->index] = kthread_id();

  for (;;) {
    spinlock_lock(&alive_lock);
    size_t alive = alive_count;
    spinlock_unlock(&alive_lock);
    
    if (alive == 0) {
      break;
    }

    struct task* task = sched_acquire_next();
    if (task == NULL) {
      ioloop_poll(10);
      continue;
    }

    sched_switch_to(worker, task);

    worker->statistics.steps += 1;
    if (task->state == UTHREAD_FINISHED) {
      worker->statistics.finished += 1;
    }

    sched_release(task);
  }

  return 0;
}

struct task* sched_acquire_next() {
  for (size_t attempt = 0; attempt < SCHED_NEXT_MAX_ATTEMPTS; ++attempt) {
    struct task* task = ready_queue_pop(&ready_q);
    if (task == NULL) return NULL;

    if (!spinlock_try_lock(&task->lock)) {
      ready_queue_push(&ready_q, task);
      SPINLOOP(2 * attempt);
      continue;
    }

    if (task->thread != NULL && task->state == UTHREAD_RUNNABLE) {
      return task;
    }

    spinlock_unlock(&task->lock);
  }

  return NULL;
}

void sched_release(struct task* task) {
  task->worker = NULL;

  if (task->state == UTHREAD_FINISHED) {
    uthread_reset(task->thread);
    task->state = UTHREAD_ZOMBIE;
    spinlock_unlock(&task->lock);
    
    spinlock_lock(&alive_lock);
    alive_count--;
    spinlock_unlock(&alive_lock);
    return;
  }

  if (task->state == UTHREAD_RUNNING) {
    task->state = UTHREAD_RUNNABLE;
    spinlock_unlock(&task->lock);
    ready_queue_push(&ready_q, task);
    return;
  }

  if (task->state == UTHREAD_BLOCKED) {
    // Задача заблокирована на I/O, просто отпускаем lock
    spinlock_unlock(&task->lock);
    return;
  }

  spinlock_unlock(&task->lock);
}

/**
 * Отметить задачу завершенной.
 */
void sched_finish(struct task* task) {
  task->state = UTHREAD_FINISHED;
}

void task_yield(struct task* caller) {
  sched_switch_to_scheduler(caller);
}

void task_exit(struct task* caller) {
  sched_finish(caller);
  task_yield(caller);
}

task_t task_submit(struct task* caller, uthread_routine entry, void* argument) {
  (void)caller;  // Может быть полезно.
  task_t child = sched_submit(*entry, argument);
  return child;
}

task_t sched_try_submit(void (*entry)(), void* argument) {
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

    if (task->state == UTHREAD_ZOMBIE) {
      uthread_reset(task->thread);
      uthread_set_entry(task->thread, entry);
      uthread_set_arg_0(task->thread, task);
      uthread_set_arg_1(task->thread, argument);
      task->state = UTHREAD_RUNNABLE;
    spinlock_unlock(&task->lock);

      spinlock_lock(&alive_lock);
      alive_count++;
      spinlock_unlock(&alive_lock);
      
      ready_queue_push(&ready_q, task);
      return (task_t){.task = task};
    }

    spinlock_unlock(&task->lock);
  }

  return (task_t){.task = NULL};
}

task_t sched_submit(void (*entry)(), void* argument) {
  for (size_t attempt = 0; attempt < SCHED_NEXT_MAX_ATTEMPTS; ++attempt) {
    task_t handle = sched_try_submit(entry, argument);
    if (handle.task != NULL) {
      return handle;
    }
    SPINLOOP(2 * attempt);
  }

  assert(false && "Can't create a task");
}

void sched_block_on_fd(struct task* task, int fd, uint32_t events) {
  task->waiting_fd = fd;
  task->waiting_events = events;
  task->state = UTHREAD_BLOCKED;
  
  ioloop_register(fd, events, task);
  sched_switch_to_scheduler(task);
}

void sched_wake_task(struct task* t) {
  if (spinlock_try_lock(&t->lock)) {
    if (t->state == UTHREAD_BLOCKED) {
      t->state = UTHREAD_RUNNABLE;
      t->waiting_fd = -1;
      t->waiting_events = 0;
      spinlock_unlock(&t->lock);
      ready_queue_push(&ready_q, t);
    } else {
      spinlock_unlock(&t->lock);
    }
  }
}

void sched_start() {
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    struct worker* worker = &workers[i];
    enum kthread_status status = kthread_create(&worker->kthread, sched_loop, worker);
    assert(status == KTHREAD_SUCCESS);
  }
}

void sched_wait() {
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    struct worker* worker = &workers[i];
    enum kthread_status status = kthread_join(&worker->kthread);
    assert(status == KTHREAD_SUCCESS);
  }
}

void sched_print_statistics() {
  printf("\nsched statistics\n");

  size_t tasks_count = 0;
  size_t steps_count = 0;
  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    struct worker* worker = &workers[i];
    tasks_count += worker->statistics.finished;
    steps_count += worker->statistics.steps;
  }

  printf("|- tasks executed %zu\n", tasks_count);
  printf("|- steps done     %zu\n", steps_count);

  for (size_t i = 0; i < SCHED_WORKERS_COUNT; ++i) {
    struct worker* worker = &workers[i];
    printf("|- worker %zu %zu\n", i, kthread_ids[i]);
    printf("   |- steps     %zu\n", worker->statistics.steps);
    printf("   |- finished  %zu\n", worker->statistics.finished);
  }
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
  ioloop_destroy();
}