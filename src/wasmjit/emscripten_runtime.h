/* -*-mode:c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
  Copyright (c) 2018 Rian Hunter et. al, see AUTHORS file.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
 */

#ifndef __WASMJIT__EMSCRIPTEN_RUNTIME_H__
#define __WASMJIT__EMSCRIPTEN_RUNTIME_H__

#include <wasmjit/runtime.h>
#include <wasmjit/util.h>
#include <wasmjit/sys.h>
#include <wasmjit/vector.h>
#include <wasmjit/posix_sys.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	WASMJIT_EMSCRIPTEN_TOTAL_MEMORY = 16777216,
};

struct EmFILE {
};

struct EmFile {
	DIR *dirp;
};

struct EmSem {
	uint32_t user_addr;
	sem_t *real_sem;
};

struct EmscriptenContext {
	struct ModuleInst *asm_;
	struct FuncInst *errno_location_inst;
	char **environ;
	int buildEnvironmentCalled;
	struct FuncInst *malloc_inst;
	struct FuncInst *free_inst;
	DEFINE_ANON_VECTOR(struct EmFile *) fd_table;
	/* NB: only used in kernel */
	struct EmFILE *grp_file;
	uint32_t gai_strerror_buffer;
	uint32_t getenv_buffer;
	uint32_t getgrent_buffer;
	uint32_t getpwent_buffer;
	uint32_t tmzone_buffer;
	uint32_t *LLVM_SAVEDSTACKS;
	size_t LLVM_SAVEDSTACKS_sz;
	uint32_t tmtm_buffer;
	DEFINE_ANON_VECTOR(struct EmSem) sem_table;
};

#define CTYPE_VALTYPE_I32 uint32_t
#define CTYPE_VALTYPE_NULL void
#define CTYPE(val) CTYPE_ ## val

#define __PARAM(to, n, t) CTYPE(t)

#define COMMA_0
#define COMMA_1 ,
#define COMMA_2 ,
#define COMMA_3 ,
#define COMMA_4 ,
#define COMMA_IF_NOT_EMPTY(_n) CAT(COMMA_, _n)

#define DEFINE_WASM_FUNCTION(_name, _fptr, _output, _n, ...)		\
	CTYPE(_output)  wasmjit_emscripten_ ## _name(__KMAP(_n, __PARAM, ##__VA_ARGS__) COMMA_IF_NOT_EMPTY(_n) struct FuncInst *);

#define DEFINE_WASM_START_FUNCTION(_name) \
	void _name(struct FuncInst *);

#define START_MODULE()
#define END_MODULE()
#define START_FUNCTION_DEFS()
#define END_FUNCTION_DEFS()
#define START_TABLE_DEFS()
#define END_TABLE_DEFS()
#define START_MEMORY_DEFS()
#define END_MEMORY_DEFS()
#define START_GLOBAL_DEFS()
#define END_GLOBAL_DEFS()
#define DEFINE_WASM_GLOBAL(...)
#define DEFINE_WASM_TABLE(...)
#define DEFINE_WASM_MEMORY(...)
#define DEFINE_EXTERNAL_WASM_GLOBAL(...)
#define DEFINE_EXTERNAL_WASM_TABLE(...)

#include <wasmjit/emscripten_runtime_def.h>

#undef COMMA_0
#undef COMMA_1
#undef COMMA_2
#undef COMMA_3
#undef COMMA_IF_NOT_EMPTY
#undef START_MODULE
#undef END_MODULE
#undef DEFINE_WASM_GLOBAL
#undef DEFINE_WASM_FUNCTION
#undef DEFINE_WASM_TABLE
#undef DEFINE_WASM_MEMORY
#undef START_TABLE_DEFS
#undef END_TABLE_DEFS
#undef START_MEMORY_DEFS
#undef END_MEMORY_DEFS
#undef START_GLOBAL_DEFS
#undef END_GLOBAL_DEFS
#undef START_FUNCTION_DEFS
#undef END_FUNCTION_DEFS
#undef DEFINE_WASM_START_FUNCTION
#undef DEFINE_EXTERNAL_WASM_TABLE
#undef DEFINE_EXTERNAL_WASM_GLOBAL

#undef __PARAM
#undef CTYPE
#undef CTYPE_VALTYPE_I32
#undef CTYPE_VALTYPE_NULL

struct EmscriptenContext *wasmjit_emscripten_get_context(struct ModuleInst *);
void wasmjit_emscripten_cleanup(struct ModuleInst *);

void wasmjit_emscripten_internal_abort(const char *msg) __attribute__((noreturn));
struct MemInst *wasmjit_emscripten_get_mem_inst(struct FuncInst *funcinst);


int wasmjit_emscripten_init(struct EmscriptenContext *ctx,
			    struct ModuleInst *asm_,
			    struct FuncInst *errno_location_inst,
			    struct FuncInst *malloc_inst,
			    struct FuncInst *free_inst,
			    char *envp[]);

int wasmjit_emscripten_build_environment(struct FuncInst *environ_constructor);

int wasmjit_emscripten_invoke_main(struct MemInst *meminst,
				   struct FuncInst *stack_alloc_inst,
				   struct FuncInst *main_inst,
				   int argc,
				   char *argv[]);

struct WasmJITEmscriptenMemoryGlobals {
	uint32_t __memory_base;
	uint32_t __table_base;
	uint32_t memoryBase;
	uint32_t tempDoublePtr;
	uint32_t DYNAMICTOP_PTR;
	uint32_t STACKTOP;
	uint32_t STACK_MAX;
};

void wasmjit_emscripten_derive_memory_globals(uint32_t static_bump,
					      struct WasmJITEmscriptenMemoryGlobals *out);

#ifdef __cplusplus
}
#endif

#endif
