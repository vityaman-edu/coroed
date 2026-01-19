SRC_DIR   = source
BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj
BIN_DIR   = $(BUILD_DIR)/bin

COMPILER           ?= clang
OPTIMIZATION_LEVEL ?= -O2
SANITIZERS         ?=

CC        = $(COMPILER)

CFLAGS    = -g -I $(SRC_DIR)
CFLAGS   += -Wall -Werror
CFLAGS   += -fno-omit-frame-pointer
CFLAGS   += $(OPTIMIZATION_LEVEL)
CFLAGS   += $(SANITIZERS)

LDFLAGS   = $(CFLAGS)

SRCS     := $(shell find source -iname '*.c') $(shell find source -iname '*.S')

OBJS     := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
OBJS     := $(patsubst $(SRC_DIR)/%.S, $(OBJ_DIR)/%.o, $(OBJS))

# Объекты библиотеки coroed (без main.c и тестов и http серверов)
COROED_OBJS := $(filter-out $(OBJ_DIR)/main.o $(OBJ_DIR)/test_%.o $(OBJ_DIR)/http_%.o, $(OBJS))

# Объекты для основного бинарника (тесты + main.c + библиотека)
APP_OBJS := $(filter-out $(OBJ_DIR)/http_%.o, $(OBJS))

# Основная программа тестов
TARGET        = app
HTTP_COROED   = http_coroed
HTTP_THREADED = http_threaded

run: $(BIN_DIR)/$(TARGET)
	./$(BIN_DIR)/$(TARGET)

http-coroed: $(BIN_DIR)/$(HTTP_COROED)
	./$(BIN_DIR)/$(HTTP_COROED)

http-threaded: $(BIN_DIR)/$(HTTP_THREADED)
	./$(BIN_DIR)/$(HTTP_THREADED)

compile: clean $(BIN_DIR)/$(TARGET) $(BIN_DIR)/$(HTTP_COROED) $(BIN_DIR)/$(HTTP_THREADED)

clean:
	rm -rf $(BUILD_DIR)
	rm -rf ./**/*.o

$(BIN_DIR)/$(TARGET): $(BIN_DIR) $(OBJ_DIR) $(APP_OBJS)
	$(CC) $(APP_OBJS) -o $@ $(LDFLAGS)

$(BIN_DIR)/$(HTTP_COROED): $(BIN_DIR) $(OBJ_DIR) $(COROED_OBJS) $(OBJ_DIR)/http_coroed.o
	$(CC) $(COROED_OBJS) $(OBJ_DIR)/http_coroed.o -o $@ $(LDFLAGS)

$(BIN_DIR)/$(HTTP_THREADED): $(BIN_DIR) $(OBJ_DIR) $(OBJ_DIR)/http_threaded.o
	$(CC) $(OBJ_DIR)/http_threaded.o -o $@ $(LDFLAGS) -lpthread

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.S
	mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR): $(BUILD_DIR)
	mkdir -p $(OBJ_DIR)

$(BIN_DIR): $(BUILD_DIR)
	mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

check-style:
	find source -iname '*.h' -o -iname '*.c' \
	| xargs clang-tidy -p compile_commands.json

check-format:
	find source -iname '*.h' -o -iname '*.c' \
	| xargs clang-format -Werror --dry-run --fallback-style=Google --verbose

fix-format:
	find source -iname '*.h' -o -iname '*.c' \
	| xargs clang-format -i --fallback-style=Google --verbose

info:
	$(info CC        = $(CC))
	$(info CFLAGS    = $(CFLAGS))
	$(info LDFLAGS   = $(LDFLAGS))
	$(info SRC_DIR   = $(SRC_DIR))
	$(info BUILD_DIR = $(BUILD_DIR))
	$(info SRCS      = $(SRCS))
	$(info OBJS      = $(OBJS))
