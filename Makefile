# *** THIS IS NOT A LONG TERM SOLUTION ***

LCFLAGS ?= -Isrc -g -Wall -Wextra -Werror

all: wasmjit

WASMJIT_PREQS = src/wasmjit/main.o src/wasmjit/vector.o src/wasmjit/ast.o src/wasmjit/parse.o src/wasmjit/ast_dump.o src/wasmjit/compile.o src/wasmjit/runtime.o src/wasmjit/util.o src/wasmjit/elf_relocatable.o src/wasmjit/dynamic_emscripten_runtime.o src/wasmjit/posix_sys_posix.o src/wasmjit/instantiate.o src/wasmjit/emscripten_runtime.o src/wasmjit/high_level.o src/wasmjit/dynamic_runtime.o

clean:
	rm -f wasmjit $(WASMJIT_PREQS) src/wasmjit/posix_sys_linux_kernel.o src/wasmjit/kwasmjit_linux.o src/wasmjit/x86_64_jmp.o

wasmjit: $(WASMJIT_PREQS)
	$(CC) -o $@ $(WASMJIT_PREQS) $(LCFLAGS) -pthread

%.o: %.c
	$(CC) -c -o $@ $< $(LCFLAGS)

UNAME ?= $(shell uname -r)
EXTRA_CFLAGS := -I$(src)/src -msse -DIEC559_FLOAT_ENCODING

obj-m += kwasmjit.o
kwasmjit-objs := src/wasmjit/kwasmjit_linux.o  src/wasmjit/parse.o src/wasmjit/ast.o  src/wasmjit/instantiate.o src/wasmjit/runtime.o src/wasmjit/compile.o src/wasmjit/vector.o src/wasmjit/util.o src/wasmjit/emscripten_runtime.o src/wasmjit/dynamic_emscripten_runtime.o src/wasmjit/posix_sys_linux_kernel.o src/wasmjit/high_level.o src/wasmjit/x86_64_jmp.o src/wasmjit/dynamic_runtime.o

.PHONY: kwasmjit.ko
kwasmjit.ko:
	make -C /lib/modules/$(UNAME)/build M=$(PWD) modules

modclean:
	make -C /lib/modules/$(UNAME)/build M=$(PWD) clean
