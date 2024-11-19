#include "uthread.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STACK_SIZE (size_t)(1024 * 1024)

struct [[gnu::packed]] switch_frame {
  uint64_t rflags;
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rip;
};

struct switch_frame* uthread_frame(struct uthread* thread) {
  return thread->context;
}

void uthread_switch(struct uthread* prev, struct uthread* next) {
  void __switch_threads(void** prev, void* next);  // NOLINT
  __switch_threads(&prev->context, next->context);
}

struct uthread* uthread_allocate() {
  const size_t size = STACK_SIZE + sizeof(struct uthread);

  struct uthread* thread = (struct uthread*)(malloc(size));
  if (!thread) {
    return NULL;
  }

  uthread_reset(thread);

  return thread;
}

void uthread_reset(struct uthread* thread) {
  thread->context = (uint8_t*)thread + STACK_SIZE - sizeof(struct switch_frame);

  struct switch_frame* frame = (struct switch_frame*)(thread->context);
  memset(frame, 0, sizeof(struct switch_frame));
  uthread_set_entry(thread, NULL);
  uthread_set_arg_0(thread, NULL);
  uthread_set_arg_1(thread, NULL);
}

void* uthread_arg_0(struct uthread* thread) {
  return (void*)(uthread_frame(thread)->r15);
}

void* uthread_arg_1(struct uthread* thread) {
  return (void*)(uthread_frame(thread)->r14);
}

void uthread_set_entry(struct uthread* thread, uthread_routine entry) {
  uthread_frame(thread)->rip = (uint64_t)(entry);
}

void uthread_set_arg_0(struct uthread* thread, void* value) {
  uthread_frame(thread)->r15 = (uint64_t)(value);
}

void uthread_set_arg_1(struct uthread* thread, void* value) {
  uthread_frame(thread)->r14 = (uint64_t)(value);
}
