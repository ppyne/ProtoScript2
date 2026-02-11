CC ?= cc
EMCC ?= emcc
EMAR ?= emar

ROOT := $(shell pwd)
WEB_DIR := $(ROOT)/web
C_DIR := $(ROOT)/c
MCPP_DIR := $(ROOT)/third_party/mcpp
MCPP_WEB_LIB := $(MCPP_DIR)/lib-wasm/libmcpp.a

WEB_OUT := $(WEB_DIR)/protoscript.js
WEB_SRCS := \
  $(C_DIR)/cli/ps.c \
  $(C_DIR)/frontend.c \
  $(C_DIR)/preprocess.c \
  $(C_DIR)/diag.c \
  $(C_DIR)/runtime/ps_api.c \
  $(C_DIR)/runtime/ps_errors.c \
  $(C_DIR)/runtime/ps_heap.c \
  $(C_DIR)/runtime/ps_value.c \
  $(C_DIR)/runtime/ps_string.c \
  $(C_DIR)/runtime/ps_list.c \
  $(C_DIR)/runtime/ps_object.c \
  $(C_DIR)/runtime/ps_map.c \
  $(C_DIR)/runtime/ps_json.c \
  $(C_DIR)/runtime/ps_modules.c \
  $(C_DIR)/runtime/ps_vm.c \
  $(C_DIR)/runtime/ps_dynlib_stub.c

WEB_OBJS := \
  $(WEB_DIR)/modules_io.o \
  $(WEB_DIR)/modules_json.o \
  $(WEB_DIR)/modules_math.o \
  $(WEB_DIR)/modules_time.o \
  $(WEB_DIR)/modules_time_civil.o

WEB_CPPFLAGS := -DPS_WASM=1 -I$(ROOT)/include -I$(C_DIR) -I$(C_DIR)/runtime -I$(MCPP_DIR)
WEB_CFLAGS := -O2
WEB_LDFLAGS := -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=ProtoScript -s EXIT_RUNTIME=0 \
  -s FORCE_FILESYSTEM=1 -s ALLOW_MEMORY_GROWTH=1 -s INVOKE_RUN=0 \
  -s EXPORTED_RUNTIME_METHODS="['FS','callMain']" \
  --preload-file $(ROOT)/modules/registry.json@/modules/registry.json

.PHONY: all c clean web web-clean test mcpp-web

all: c

c:
	$(MAKE) -C c

clean:
	$(MAKE) -C c clean

web: $(WEB_OUT)

$(WEB_OUT): $(WEB_SRCS) $(WEB_OBJS) mcpp-web
	$(EMCC) $(WEB_CFLAGS) $(WEB_CPPFLAGS) $(WEB_LDFLAGS) -o $(WEB_OUT) $(WEB_SRCS) $(WEB_OBJS) $(MCPP_WEB_LIB) -lm

$(WEB_DIR)/modules_io.o: $(ROOT)/tests/modules_src/io.c
	@mkdir -p $(WEB_DIR)
	$(EMCC) $(WEB_CFLAGS) $(WEB_CPPFLAGS) -Dps_module_init=ps_module_init_Io -c $< -o $@

$(WEB_DIR)/modules_json.o: $(ROOT)/tests/modules_src/json.c
	@mkdir -p $(WEB_DIR)
	$(EMCC) $(WEB_CFLAGS) $(WEB_CPPFLAGS) -Dps_module_init=ps_module_init_JSON -c $< -o $@

$(WEB_DIR)/modules_math.o: $(ROOT)/tests/modules_src/math.c
	@mkdir -p $(WEB_DIR)
	$(EMCC) $(WEB_CFLAGS) $(WEB_CPPFLAGS) -Dps_module_init=ps_module_init_Math -c $< -o $@

$(WEB_DIR)/modules_time.o: $(C_DIR)/modules/time.c
	@mkdir -p $(WEB_DIR)
	$(EMCC) $(WEB_CFLAGS) $(WEB_CPPFLAGS) -Dps_module_init=ps_module_init_Time -c $< -o $@

$(WEB_DIR)/modules_time_civil.o: $(C_DIR)/modules/time_civil.c
	@mkdir -p $(WEB_DIR)
	$(EMCC) $(WEB_CFLAGS) $(WEB_CPPFLAGS) -Dps_module_init=ps_module_init_TimeCivil -c $< -o $@

mcpp-web:
	$(MAKE) -C $(MCPP_DIR) clean
	$(MAKE) -C $(MCPP_DIR) CC=$(EMCC) AR=$(EMAR) CFLAGS="-O2 -Wno-deprecated-declarations -Wno-unused-command-line-argument" LIBDIR=lib-wasm

web-clean:
	rm -f $(WEB_DIR)/protoscript.js $(WEB_DIR)/protoscript.wasm $(WEB_DIR)/protoscript.data $(WEB_OBJS)

test:
	tests/run_all.sh
