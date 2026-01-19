#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "coroed/api/log.h"
#include "coroed/api/task.h"

#define SERVER_PORT 8080
#define BACKLOG 128
#define BUFFER_SIZE 4096

// Парсинг пути /hello/<name>
static int parse_hello_path(const char* path, char* name_out, size_t name_size) {
  const char* prefix = "/hello/";
  size_t prefix_len = strlen(prefix);

  if (strncmp(path, prefix, prefix_len) != 0) {
    return -1;
  }

  const char* name_start = path + prefix_len;
  const char* name_end = name_start;

  // Найти конец имени (до пробела, ? или конца строки)
  while (*name_end && *name_end != ' ' && *name_end != '?' && *name_end != '\r' &&
         *name_end != '\n') {
    name_end++;
  }

  size_t name_len = (size_t)(name_end - name_start);
  if (name_len == 0 || name_len >= name_size) {
    return -1;
  }

  memcpy(name_out, name_start, name_len);
  name_out[name_len] = '\0';
  return 0;
}

// Извлечь путь из HTTP запроса
static int extract_path(const char* request, char* path_out, size_t path_size) {
  // Ожидаем "GET /path HTTP/1.1"
  if (strncmp(request, "GET ", 4) != 0) {
    return -1;
  }

  const char* path_start = request + 4;
  const char* path_end = strchr(path_start, ' ');
  if (!path_end) {
    return -1;
  }

  size_t path_len = (size_t)(path_end - path_start);
  if (path_len == 0 || path_len >= path_size) {
    return -1;
  }

  memcpy(path_out, path_start, path_len);
  path_out[path_len] = '\0';
  return 0;
}

// Обработка клиентского соединения
TASK_DEFINE(handle_client, int, client_fd_ptr) {
  int client_fd = *client_fd_ptr;
  free(client_fd_ptr);

  char buffer[BUFFER_SIZE];
  ssize_t n = coro_read(__self, client_fd, buffer, sizeof(buffer) - 1);

  if (n <= 0) {
    close(client_fd);
    return;
  }

  buffer[n] = '\0';

  char path[256];
  if (extract_path(buffer, path, sizeof(path)) != 0) {
    const char* error_response =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Bad Request";
    coro_write(__self, client_fd, error_response, strlen(error_response));
    close(client_fd);
    return;
  }

  char name[128];
  if (parse_hello_path(path, name, sizeof(name)) == 0) {
    // Успешно распарсили /hello/<name>
    char response[1024];
    char json_body[256];
    snprintf(json_body, sizeof(json_body), "{\"message\":\"%s\"}", name);

    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             strlen(json_body), json_body);

    coro_write(__self, client_fd, response, strlen(response));
  } else {
    // Неизвестный путь
    const char* error_response =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Not Found";
    coro_write(__self, client_fd, error_response, strlen(error_response));
  }

  close(client_fd);
}

TASK_DEFINE(http_server, void, unused) {
  (void)unused;

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return;
  }

  int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    close(listen_fd);
    return;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(SERVER_PORT);

  if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(listen_fd);
    return;
  }

  if (listen(listen_fd, BACKLOG) < 0) {
    perror("listen");
    close(listen_fd);
    return;
  }

  // Принимаем клиентов
  for (;;) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd =
        coro_accept(__self, listen_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
      if (errno == EINTR) continue;
      perror("accept");
      continue;
    }

    // Запускаем новую корутину для обработки клиента
    int* fd_ptr = malloc(sizeof(int));
    *fd_ptr = client_fd;

    // ВАЖНО: submit'им wrapper handle_client(), а не task_body_handle_client()
    GO(handle_client, fd_ptr);
  }

  close(listen_fd);
}

int main() {
  log_init();

  tasks_init();

  // ВАЖНО: submit'им wrapper http_server(), а не task_body_http_server()
  tasks_submit(http_server, NULL);

  tasks_start();
  tasks_wait();
  tasks_print_statistics();
  tasks_destroy();

  return 0;
}
