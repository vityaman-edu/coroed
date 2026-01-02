FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /work

# зависимости для сборки + форматирования + bear
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential clang make git ca-certificates \
    bear clang-format \
 && rm -rf /var/lib/apt/lists/*

# копируем проект внутрь образа
COPY . .

# сборка (и compile_commands.json если нужен)
RUN make clean && bear -- make compile && make

CMD ["bash"]
