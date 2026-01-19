#include "kthread.h"

#include <assert.h>

#ifdef KTHREAD_STDLIB
#include <threads.h>
#endif

#ifdef KTHREAD_PTHREAD
#include <pthread.h>
#include <stdlib.h>
#endif

#ifdef KTHREAD_STDLIB

enum kthread_status kthread_status_from(int thrd_status) {
  return thrd_status == thrd_success ? KTHREAD_SUCCESS : KTHREAD_FAILURE;
}

enum kthread_status kthread_create(
    struct kthread* kthread, kthread_routine routine, void* argument
) {
  int code = thrd_create(&kthread->thrd, routine, /* arg = */ argument);
  return kthread_status_from(code);
}

enum kthread_status kthread_join(struct kthread* kthread) {
  int status = 0;
  int code = thrd_join(kthread->thrd, &status);
  assert(code != thrd_success || status == 0);
  return kthread_status_from(code);
}

kthread_id_t kthread_id() {
  return (int)thrd_current();
}

#endif

#ifdef KTHREAD_PTHREAD

static void* kthread_wrapper(void* arg) {
  struct kthread* kt = (struct kthread*)arg;
  kt->return_code = kt->routine(kt->argument);
  return NULL;
}

enum kthread_status kthread_create(
    struct kthread* kthread, kthread_routine routine, void* argument
) {
  kthread->routine = routine;
  kthread->argument = argument;
  kthread->return_code = 0;
  
  int code = pthread_create(&kthread->thread, NULL, kthread_wrapper, kthread);
  return code == 0 ? KTHREAD_SUCCESS : KTHREAD_FAILURE;
}

enum kthread_status kthread_join(struct kthread* kthread) {
  int code = pthread_join(kthread->thread, NULL);
  return code == 0 ? KTHREAD_SUCCESS : KTHREAD_FAILURE;
}

kthread_id_t kthread_id() {
  return (kthread_id_t)pthread_self();
}

#endif
