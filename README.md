# CoroEd

> Simplicity is a necessary condition for beauty
>
> -- Leo Tolstoy, 📖 Russian writer

## Features

- Fixed thread pool executor
- Stackfull coroutines
- Platform: x64
- Zombie-fibers
- Event
- Sleep

## Build & Run

### Quick Start

```bash
# Сборка в Docker
docker build --platform linux/amd64 -t coroed .

# Запуск
docker run --platform linux/amd64 --rm -it -p 8080:8080 -p 8081:8081 coroed

# Внутри контейнера:
./build/bin/app                 # тесты
./build/bin/http_coroed         # HTTP сервер (корутины, порт 8080)
./build/bin/http_threaded       # HTTP сервер (потоки, порт 8081)

# Бенчмарк
apt-get update && apt-get install -y wrk
./benchmark.sh
```

📖 Подробный туториал: [TUTORIAL.md](./TUTORIAL.md)

### Linux x64

```bash
make clean && bear -- make compile && make
./build/bin/app
```

Run precommit checks locally.

```bash
./ci/precommit.bash
```

## Reference

- [Stackless Coroutines in C by @vityaman](https://github.com/vityaman-edu/c-coroutines)

- [Stepik OS Course by CSCenter](https://github.com/cscenter/OS_online_course)

- [Concurrency Course by Roman Lipovsky: Repository](https://gitlab.com/Lipovsky/concurrency-course)

- [Concurrency Course by Roman Lipovsky: Lectures](https://youtube.com/playlist?list=PL4_hYwCyhAva37lNnoMuBcKRELso5nvBm)

- [Concurrency Course by Roman Lipovsky: Seminars](https://youtube.com/playlist?list=PL4_hYwCyhAvYTxm55RBm_HA5Bq5W1Nv-R)
