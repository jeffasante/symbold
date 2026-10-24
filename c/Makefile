CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion
# clock_gettime and rusage.ru_maxrss under -std=c99 (glibc / macOS)
CPPFLAGS += -Iinclude -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE
SRC = src/interpreter.c src/features.c src/main.c

build/symbold: $(SRC) include/symbold.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC)

build/symbold-asan: $(SRC) include/symbold.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $(SRC)

# Needs an LLVM clang with the wasm32 target, wasm-ld on PATH, and a WASI sysroot
# (macOS: brew install llvm lld wasi-libc wasi-runtimes).
WASM_CC ?= /opt/homebrew/opt/llvm/bin/clang
WASI_SYSROOT ?= /opt/homebrew/share/wasi-sysroot
WASM_RESOURCE_DIR ?= /opt/homebrew/opt/wasi-runtimes/share/wasi-runtimes
WASM_SRC = src/interpreter.c src/features.c src/wasm.c

build/symbold.wasm: $(WASM_SRC) include/symbold.h
	@mkdir -p build
	PATH="/opt/homebrew/opt/lld/bin:$$PATH" $(WASM_CC) --target=wasm32-wasip1 \
	  --sysroot=$(WASI_SYSROOT) -resource-dir=$(WASM_RESOURCE_DIR) -Iinclude \
	  -std=c99 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion \
	  -mexec-model=reactor -Wl,--strip-all -o $@ $(WASM_SRC)

../web/symbold.wasm: build/symbold.wasm
	cp $< $@

wasm: ../web/symbold.wasm

# Reference outputs in ../samples/expected.txt were produced by the training code.
test: build/symbold-asan
	build/symbold-asan check ../models/tree.sdp
	build/symbold-asan features ../samples/mnist-test-500.u8 build/features.bin
	build/symbold-asan run ../models/tree.sdp build/features.bin | diff -u ../samples/expected.txt -
	cut -d' ' -f1 ../samples/expected.txt > build/labels.expected
	build/symbold-asan predict ../models/tree.sdp ../samples/mnist-test-500.u8 | diff -u build/labels.expected -
	@echo "ok: C matches the reference on $$(wc -l < ../samples/expected.txt | tr -d ' ') images"

clean:
	rm -rf build

.PHONY: clean wasm test
