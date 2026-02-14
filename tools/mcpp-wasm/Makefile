EMCC ?= emcc
EMAR ?= emar
ROOT := $(abspath $(CURDIR)/../..)
MCPP_DIR := $(ROOT)/third_party/mcpp
MCPP_LIB := $(MCPP_DIR)/lib-wasm/libmcpp.a
OUT_DIR := $(CURDIR)/out
SRC_DIR := $(CURDIR)/src
EM_CACHE ?= $(CURDIR)/.emcache

.PHONY: all clean mcpp-lib

all: $(OUT_DIR)/mcpp_core.js $(OUT_DIR)/mcpp.wasm $(OUT_DIR)/mcpp_node.js

mcpp-lib:
	EM_CACHE=$(EM_CACHE) $(MAKE) -C $(MCPP_DIR) CC=$(EMCC) AR=$(EMAR) CFLAGS="-O2 -Wno-deprecated-declarations -Wno-unused-command-line-argument" LIBDIR=lib-wasm

$(OUT_DIR)/mcpp_core.js $(OUT_DIR)/mcpp.wasm: $(SRC_DIR)/mcpp_bridge.c | mcpp-lib
	@mkdir -p $(OUT_DIR)
	EM_CACHE=$(EM_CACHE) $(EMCC) -O2 \
	  -I$(ROOT)/include -I$(ROOT)/c -I$(ROOT)/third_party/mcpp \
	  $(SRC_DIR)/mcpp_bridge.c $(ROOT)/c/preprocess.c $(MCPP_LIB) \
	  -s WASM=1 \
	  -s ENVIRONMENT=node \
	  -s FILESYSTEM=1 \
	  -s ALLOW_MEMORY_GROWTH=1 \
	  -s WASM_ASYNC_COMPILATION=0 \
	  -s EXPORTED_FUNCTIONS="['_ps_mcpp_preprocess','_ps_mcpp_output','_ps_mcpp_error','_ps_mcpp_map_len','_ps_mcpp_map_line','_ps_mcpp_map_file','_ps_mcpp_clear']" \
	  -s EXPORTED_RUNTIME_METHODS="['ccall','UTF8ToString']" \
	  -o $(OUT_DIR)/mcpp_core.js
	@if [ -f "$(OUT_DIR)/mcpp_core.wasm" ]; then cp -f "$(OUT_DIR)/mcpp_core.wasm" "$(OUT_DIR)/mcpp.wasm"; fi

$(OUT_DIR)/mcpp_node.js: $(SRC_DIR)/mcpp_node_wrapper.js $(OUT_DIR)/mcpp_core.js
	@mkdir -p $(OUT_DIR)
	cp $(SRC_DIR)/mcpp_node_wrapper.js $(OUT_DIR)/mcpp_node.js

clean:
	rm -f $(OUT_DIR)/mcpp_runtime.js $(OUT_DIR)/mcpp_runtime.wasm $(OUT_DIR)/mcpp_core.js $(OUT_DIR)/mcpp_core.wasm $(OUT_DIR)/mcpp.wasm $(OUT_DIR)/mcpp_node.js
	$(MAKE) -C $(MCPP_DIR) clean
