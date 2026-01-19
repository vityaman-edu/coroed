#include "ioloop.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "coroed/api/task.h"
#include "coroed/core/spinlock.h"

enum {
  IO_FD_LIMIT = 65536,
  IO_MAX_EVENTS = 128,
};

// Таблица ожидающих задач: fd -> task*
static struct task* io_waiting[IO_FD_LIMIT];
static struct spinlock io_lock;
static int epfd = -1;

void ioloop_init() {
  spinlock_init(&io_lock);
  memset(io_waiting, 0, sizeof(io_waiting));
  epfd = epoll_create1(EPOLL_CLOEXEC);
  assert(epfd >= 0 && "epoll_create1() failed");
}

void ioloop_destroy() {
  if (epfd >= 0) {
    close(epfd);
    epfd = -1;
  }
}

void ioloop_register(int fd, uint32_t events, struct task* task) {
  assert(fd >= 0 && fd < IO_FD_LIMIT);

  spinlock_lock(&io_lock);
  io_waiting[fd] = task;

  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.data.fd = fd;

  if (events & IO_EVENT_READ) {
    ev.events |= EPOLLIN;
  }
  if (events & IO_EVENT_WRITE) {
    ev.events |= EPOLLOUT;
  }

  // Пробуем ADD, если не получается — MOD
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
    if (errno == EEXIST) {
      int code = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
      if (code != 0) {
        perror("epoll_ctl MOD");
      }
    } else {
      perror("epoll_ctl ADD");
    }
  }

  spinlock_unlock(&io_lock);
}

void ioloop_unregister(int fd) {
  if (fd < 0 || fd >= IO_FD_LIMIT) return;

  spinlock_lock(&io_lock);
  io_waiting[fd] = NULL;
  (void)epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
  spinlock_unlock(&io_lock);
}

// Внутренняя функция для пробуждения задачи
extern void sched_wake_task(struct task* t);

int ioloop_poll(int timeout_ms) {
  struct epoll_event events[IO_MAX_EVENTS];

  int n = epoll_wait(epfd, events, IO_MAX_EVENTS, timeout_ms);
  if (n < 0) {
    if (errno == EINTR) return 0;
    perror("epoll_wait");
    return 0;
  }

  int woken = 0;
  spinlock_lock(&io_lock);
  for (int i = 0; i < n; ++i) {
    int fd = events[i].data.fd;
    if (fd < 0 || fd >= IO_FD_LIMIT) continue;

    struct task* t = io_waiting[fd];
    if (!t) continue;

    // Снимаем ожидание (one-shot)
    io_waiting[fd] = NULL;

    // Убираем из epoll
    (void)epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);

    // Разблокируем задачу
    sched_wake_task(t);
    woken++;
  }
  spinlock_unlock(&io_lock);

  return woken;
}

