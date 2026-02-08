CC ?= cc
EMCC ?= emcc

ROOT := $(shell pwd)
WEB_DIR := $(ROOT)/web
C_DIR := $(ROOT)/c

WEB_OUT := $(WEB_DIR)/protoscript.js
WEB_SRCS := \
  $(C_DIR)/cli/ps.c \
  $(C_DIR)/frontend.c \
  $(C_DIR)/runtime/psmod_io_builtin.c \
  $(C_DIR)/runtime/psmod_json_builtin.c \
  $(C_DIR)/runtime/psmod_math_builtin.c \
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

WEB_FLAGS := -O2 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=ProtoScript -s EXIT_RUNTIME=0 \
  -s FORCE_FILESYSTEM=1 -s ALLOW_MEMORY_GROWTH=1 -s INVOKE_RUN=0 \
  -s EXPORTED_RUNTIME_METHODS="['FS','callMain']" \
  --preload-file $(ROOT)/modules/registry.json@/modules/registry.json \
  -DPS_WASM=1 -I$(ROOT)/include -I$(C_DIR) -I$(C_DIR)/runtime

.PHONY: all c clean web web-clean

all: c

c:
	$(MAKE) -C c

clean:
	$(MAKE) -C c clean
	$(MAKE) web-clean

web: $(WEB_OUT)

$(WEB_OUT): $(WEB_SRCS)
	$(EMCC) $(WEB_FLAGS) -o $(WEB_OUT) $(WEB_SRCS) -lm

web-clean:
	rm -f $(WEB_DIR)/protoscript.js $(WEB_DIR)/protoscript.wasm $(WEB_DIR)/protoscript.data
