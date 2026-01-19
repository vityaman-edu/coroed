#!/bin/bash
set -e

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== HTTP Server Benchmark ===${NC}\n"

# Параметры бенчмарка
REQUESTS=10000
CONCURRENCY=100
WARMUP_REQUESTS=100

# Проверка наличия wrk или ab
if command -v wrk &> /dev/null; then
  BENCHMARK_TOOL="wrk"
  echo -e "${GREEN}Using wrk for benchmarking${NC}"
elif command -v ab &> /dev/null; then
  BENCHMARK_TOOL="ab"
  echo -e "${GREEN}Using Apache Bench (ab) for benchmarking${NC}"
else
  echo -e "${RED}Error: Neither wrk nor ab is installed${NC}"
  echo "Please install one of them:"
  echo "  - wrk: apt-get install wrk (Linux)"
  echo "  - ab:  apt-get install apache2-utils (Linux)"
  exit 1
fi

# Проверка наличия nc (нужен для ожидания порта)
if ! command -v nc &> /dev/null; then
  echo -e "${RED}Error: nc (netcat) is not installed${NC}"
  echo "Install it:"
  echo "  - Linux (Debian/Ubuntu): apt-get install netcat-openbsd"
  exit 1
fi

# Функция для запуска сервера
start_server() {
  local server_name=$1
  local server_bin=$2
  local port=$3

  echo -e "\n${YELLOW}Starting $server_name...${NC}" >&2

  # Запуск в фоне
  $server_bin >/tmp/"$server_name".log 2>&1 &
  local pid=$!

  # Если процесс мгновенно умер — покажем лог и упадём
  sleep 0.05
  if ! kill -0 "$pid" 2>/dev/null; then
    echo -e "${RED}$server_name exited immediately (PID: $pid)${NC}" >&2
    echo -e "${YELLOW}--- last log ---${NC}" >&2
    tail -n 50 /tmp/"$server_name".log >&2 || true
    return 1
  fi

  # Ждём запуска сервера (порт должен открыться)
  for i in {1..50}; do
    if nc -z 127.0.0.1 "$port" 2>/dev/null; then
      echo -e "${GREEN}$server_name started (PID: $pid)${NC}" >&2
      echo "$pid"
      return 0
    fi
    sleep 0.1
  done

  echo -e "${RED}Failed to start $server_name on port $port${NC}" >&2
  echo -e "${YELLOW}--- last log ---${NC}" >&2
  tail -n 50 /tmp/"$server_name".log >&2 || true

  kill "$pid" 2>/dev/null || true
  return 1
}

# Функция для бенчмарка
benchmark_server() {
  local server_name=$1
  local url=$2

  echo -e "\n${BLUE}=== Benchmarking $server_name ===${NC}"

  # Прогрев
  echo "Warming up..."
  if [ "$BENCHMARK_TOOL" = "wrk" ]; then
    wrk -t2 -c10 -d2s --latency "$url" > /dev/null 2>&1 || true
  else
    ab -n "$WARMUP_REQUESTS" -c 10 "$url" > /dev/null 2>&1 || true
  fi

  sleep 1

  # Основной бенчмарк
  echo "Running benchmark..."
  if [ "$BENCHMARK_TOOL" = "wrk" ]; then
    wrk -t8 -c"$CONCURRENCY" -d10s --latency "$url"
  else
    ab -n "$REQUESTS" -c "$CONCURRENCY" "$url"
  fi
}

# Функция для остановки сервера
stop_server() {
  local pid=$1
  local server_name=$2

  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    echo -e "\n${YELLOW}Stopping $server_name (PID: $pid)...${NC}"
    kill "$pid" 2>/dev/null || true

    # Подождём мягко, потом добьём
    for i in {1..20}; do
      if ! kill -0 "$pid" 2>/dev/null; then
        return 0
      fi
      sleep 0.1
    done

    kill -9 "$pid" 2>/dev/null || true
  fi
}

# Сборка проектов
echo -e "${BLUE}Building servers...${NC}"
make clean > /dev/null
make compile > /dev/null
echo -e "${GREEN}Build complete${NC}"

# Бенчмарк coroed сервера
if [ -f "build/bin/http_coroed" ]; then
  COROED_PID="$(start_server "http_coroed" "./build/bin/http_coroed" 8080)"
  benchmark_server "CoroEd (coroutines)" "http://127.0.0.1:8080/hello/benchmark"
  stop_server "$COROED_PID" "CoroEd server"
else
  echo -e "${RED}CoroEd server binary not found${NC}"
fi

sleep 2

# Бенчмарк threaded сервера
if [ -f "build/bin/http_threaded" ]; then
  THREADED_PID="$(start_server "http_threaded" "./build/bin/http_threaded" 8081)"
  benchmark_server "Threaded (1 thread per request)" "http://127.0.0.1:8081/hello/benchmark"
  stop_server "$THREADED_PID" "Threaded server"
else
  echo -e "${RED}Threaded server binary not found${NC}"
fi

echo -e "\n${GREEN}=== Benchmark Complete ===${NC}\n"

echo -e "${BLUE}Summary:${NC}"
echo "- CoroEd server uses epoll-based non-blocking I/O with coroutines"
echo "- Threaded server creates one thread per request"
echo ""
echo "Expected results:"
echo "- CoroEd should handle more concurrent requests efficiently"
echo "- Threaded server may struggle with high concurrency due to thread overhead"
echo "- Memory usage of threaded server grows linearly with concurrent requests"
