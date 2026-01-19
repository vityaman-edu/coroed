#include "uthread.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/**
 * Размер страницы виртуальной памяти.
 */
#define PAGE_SIZE (size_t)(4 * 1024)  // 4 KB

/**
 * Размер памяти, выделяемой под поток.
 */
#define THREAD_SIZE (size_t)(256 * PAGE_SIZE)

/**
 * Размер памяти, выделяемой под стек потока,
 * Отступаем 8 страниц для безопасности.
 */
#define STACK_SIZE (size_t)(THREAD_SIZE - 8 * PAGE_SIZE)

/**
 * Сохраняемые callee-saved регистры.
 */
struct [[gnu::packed]] switch_frame {
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
  // Сохраняем лишь callee-saved регистры в `switch.S`, так как
  // caller-saved регистры будут сохранены компилятором, ведь
  // мы вызываем функцию.

  void __switch_threads(void** prev, void* next);  // NOLINT
  __switch_threads(&prev->context, next->context);
}

struct uthread* uthread_allocate() {
  struct uthread* thread = mmap(
      /* addr   */ NULL,
      /* len    */ THREAD_SIZE,
      /* prot   */ PROT_READ | PROT_WRITE,
      /* flags  */ MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
      /* fd     */ -1,
      /* offset */ 0
  );
  if (!thread) {
    return NULL;
  }

  uthread_reset(thread);

  return thread;
}

void uthread_free(struct uthread* thread) {
  assert(thread != NULL);
  uthread_reset(thread);
  int code = munmap(thread, THREAD_SIZE);
  assert(code == 0);
}

void uthread_reset(struct uthread* thread) {
  uint8_t* stack_top = (uint8_t*)thread + STACK_SIZE;
  thread->context = stack_top - sizeof(struct switch_frame) - 8;

  struct switch_frame* frame = (struct switch_frame*)(thread->context);
  memset(frame, 0, sizeof(struct switch_frame));
  
  uint64_t* dummy_ret = (uint64_t*)(stack_top - 8);
  *dummy_ret = 0;
  
  uthread_set_entry(thread, NULL);
  uthread_set_arg_0(thread, NULL);
  uthread_set_arg_1(thread, NULL);
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
