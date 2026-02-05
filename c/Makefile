CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -I../include -I.

TARGET = pscc
TARGET_PS = ps
SRC = pscc.c frontend.c runtime/ps_json.c
SRC_RUNTIME = \
  runtime/ps_api.c \
  runtime/ps_errors.c \
  runtime/ps_heap.c \
  runtime/ps_value.c \
  runtime/ps_string.c \
  runtime/ps_list.c \
  runtime/ps_object.c \
  runtime/ps_map.c \
  runtime/ps_dynlib_posix.c \
  runtime/ps_json.c \
  runtime/ps_modules.c \
  runtime/ps_vm.c
SRC_PS = cli/ps.c frontend.c $(SRC_RUNTIME)

.PHONY: all clean

all: $(TARGET) $(TARGET_PS)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

$(TARGET_PS): $(SRC_PS)
	$(CC) $(CFLAGS) $(SRC_PS) -o $(TARGET_PS) -ldl

clean:
	rm -f $(TARGET) $(TARGET_PS)
