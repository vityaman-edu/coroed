#pragma once

// Use pthread on macOS/BSD, C11 threads elsewhere
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define KTHREAD_PTHREAD
#else
#define KTHREAD_STDLIB
#endif

#include <stdint.h>
#include <unistd.h>

#ifdef KTHREAD_STDLIB
#include <threads.h>
#endif

#ifdef KTHREAD_PTHREAD
#include <pthread.h>
#endif

/**
 * Поток операционной системы.
 *
 * На таких потоках исполняются файберы. Лучше создавать их
 * по количеству ядер вашего процессора.
 */
struct kthread;

/**
 * Статус операции с потоком.
 */
enum kthread_status {
  KTHREAD_SUCCESS,  // Операция завешилась успешно
  KTHREAD_FAILURE,  // Операция провалилась
};

/**
 * Процедура, которая будет запущена в потоке.
 *
 * Принимает указатель на аргумент, чей лайфтайм
 * гарантируется вызывающим кодом. Отдает код
 * возврата.
 */
typedef int (*kthread_routine)(void*);

/**
 * Уникальный идентификатор потока.
 */
typedef size_t kthread_id_t;

/**
 * Создать и запустить поток.
 */
enum kthread_status kthread_create(
    struct kthread* kthread, kthread_routine routine, void* argument
);

/**
 * Дождаться завершения потока.
 */
enum kthread_status kthread_join(struct kthread* kthread);

/**
 * Получить идентификатор текущего потока.
 */
kthread_id_t kthread_id();

#ifdef KTHREAD_STDLIB

/**
 * Реализация на потоке стандартной библиотеки.
 */
struct kthread {
  thrd_t thrd;
};

#endif

#ifdef KTHREAD_PTHREAD

/**
 * Реализация на pthread.
 */
struct kthread {
  pthread_t thread;
  kthread_routine routine;
  void* argument;
  int return_code;
};

#endif
