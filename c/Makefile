override CC := cc
CFLAGS_BASE := -std=c11 -Wall -Wextra -Werror -O2 -I../include -I.

UNAME_S := $(shell uname -s)
ARCH_CFLAGS :=
ifeq ($(UNAME_S),Darwin)
ARCH_CFLAGS := -arch arm64
endif

override CFLAGS := $(CFLAGS_BASE) $(ARCH_CFLAGS)

MCPP_DIR := ../third_party/mcpp
MCPP_LIB := $(MCPP_DIR)/lib/libmcpp.a
MCPP_INC := -I$(MCPP_DIR)
override MCPP_CFLAGS := -O2 -Wno-deprecated-declarations -fno-common $(ARCH_CFLAGS)

TARGET = pscc
TARGET_PS = ps
SRC = pscc.c frontend.c preprocess.c diag.c runtime/ps_json.c
SRC_RUNTIME = \
  diag.c \
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
SRC_PS = cli/ps.c frontend.c preprocess.c $(SRC_RUNTIME)

MODULE_OBJS = \
  modules/time.o \
  modules/time_civil.o \
  modules_regexp.o \
  modules_debug.o \
  modules_io.o \
  modules_json.o \
  modules_math.o

.PHONY: all clean force-mcpp print-build-vars

all: $(TARGET) $(TARGET_PS)

$(TARGET): $(SRC) $(MCPP_LIB)
	$(CC) $(CFLAGS) $(MCPP_INC) $(SRC) $(MCPP_LIB) -o $(TARGET)

$(TARGET_PS): $(SRC_PS) $(MODULE_OBJS) $(MCPP_LIB)
	$(CC) $(CFLAGS) $(MCPP_INC) $(SRC_PS) $(MODULE_OBJS) $(MCPP_LIB) -o $(TARGET_PS) -ldl

$(MCPP_LIB): force-mcpp
	$(MAKE) -C $(MCPP_DIR) clean
	$(MAKE) -C $(MCPP_DIR) CFLAGS="$(MCPP_CFLAGS)"

force-mcpp:

print-build-vars:
	@echo "CC=$(CC)"
	@echo "ARCH=arm64"
	@echo "ARCH_CFLAGS=$(ARCH_CFLAGS)"
	@echo "CFLAGS=$(CFLAGS)"
	@echo "MCPP_CFLAGS=$(MCPP_CFLAGS)"

clean:
	rm -f $(TARGET) $(TARGET_PS) $(MODULE_OBJS)

modules/time.o: modules/time.c
	$(CC) $(CFLAGS) $(MCPP_INC) -Dps_module_init=ps_module_init_Time -c $< -o $@

modules/time_civil.o: modules/time_civil.c
	$(CC) $(CFLAGS) $(MCPP_INC) -Dps_module_init=ps_module_init_TimeCivil -c $< -o $@

modules_debug.o: modules/debug.c
	$(CC) $(CFLAGS) $(MCPP_INC) -Dps_module_init=ps_module_init_Debug -c $< -o $@

modules_io.o: ../tests/modules_src/io.c
	$(CC) $(CFLAGS) $(MCPP_INC) -Dps_module_init=ps_module_init_Io -c $< -o $@

modules_json.o: ../tests/modules_src/json.c
	$(CC) $(CFLAGS) $(MCPP_INC) -Dps_module_init=ps_module_init_JSON -c $< -o $@

modules_math.o: ../tests/modules_src/math.c
	$(CC) $(CFLAGS) $(MCPP_INC) -Dps_module_init=ps_module_init_Math -c $< -o $@

modules_regexp.o: modules/regexp.c
	$(CC) $(CFLAGS) $(MCPP_INC) -Dps_module_init=ps_module_init_RegExp -c $< -o $@
