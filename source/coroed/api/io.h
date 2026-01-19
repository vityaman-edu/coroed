#pragma once

#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "coroed/api/task.h"

/** Прочитать из fd, логически блокируясь в корутине. */
ssize_t coro_read(struct task* self, int fd, void* buf, size_t n);

/** Записать в fd, логически блокируясь в корутине. */
ssize_t coro_write(struct task* self, int fd, const void* buf, size_t n);

/** accept() с логическим блокированием. */
int coro_accept(struct task* self, int listen_fd, struct sockaddr* addr, socklen_t* addrlen);
