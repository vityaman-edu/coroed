#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdatomic.h>

#define SERVER_PORT 8081
#define BACKLOG 128
#define BUFFER_SIZE 4096

static atomic_size_t requests_handled = 0;
static atomic_size_t threads_created = 0;

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

// Обработка клиентского соединения в отдельном потоке
static void* handle_client(void* arg) {
  int client_fd = (int)(long)arg;

  char buffer[BUFFER_SIZE];
  ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);

  if (n <= 0) {
    close(client_fd);
    atomic_fetch_add(&requests_handled, 1);
    return NULL;
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
    write(client_fd, error_response, strlen(error_response));
    close(client_fd);
    atomic_fetch_add(&requests_handled, 1);
    return NULL;
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

    write(client_fd, response, strlen(response));
  } else {
    // Неизвестный путь
    const char* error_response =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Not Found";
    write(client_fd, error_response, strlen(error_response));
  }

  close(client_fd);
  atomic_fetch_add(&requests_handled, 1);
  return NULL;
}

int main() {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    close(listen_fd);
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(SERVER_PORT);

  if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(listen_fd);
    return 1;
  }

  if (listen(listen_fd, BACKLOG) < 0) {
    perror("listen");
    close(listen_fd);
    return 1;
  }

  printf("[threaded] HTTP server listening on port %d\n", SERVER_PORT);
  printf("[threaded] Press Ctrl+C to stop and see statistics\n");

  // Принимаем клиентов
  for (;;) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
      if (errno == EINTR) {
        break;  // Прерван сигналом
      }
      perror("accept");
      continue;
    }

    // Создаём новый поток для обработки клиента
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&thread, &attr, handle_client, (void*)(long)client_fd) != 0) {
      perror("pthread_create");
      close(client_fd);
    } else {
      atomic_fetch_add(&threads_created, 1);
    }

    pthread_attr_destroy(&attr);
  }

  close(listen_fd);

  printf("\n[threaded] Statistics:\n");
  printf("|- Threads created: %zu\n", atomic_load(&threads_created));
  printf("|- Requests handled: %zu\n", atomic_load(&requests_handled));

  return 0;
}

