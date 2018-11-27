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

/* For SCM_CREDENTIALS */
#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <wasmjit/emscripten_runtime.h>

#ifndef __KERNEL__
/* for system's getaddrinfo */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include <wasmjit/posix_sys.h>
#include <wasmjit/util.h>
#include <wasmjit/runtime.h>
#include <wasmjit/sys.h>

#if defined(__linux__) || defined(__KERNEL__)
#define IS_LINUX 1
#else
#define IS_LINUX 0
#endif

#define STATIC_ASSERT(COND,MSG) typedef char static_assertion_##MSG[(COND)?1:-1]
#define COMPILE_TIME_ASSERT3(X,L) STATIC_ASSERT(X,static_assertion_at_line_##L)
#define COMPILE_TIME_ASSERT2(X,L) COMPILE_TIME_ASSERT3(X,L)
#define COMPILE_TIME_ASSERT(X)    COMPILE_TIME_ASSERT2(X,__LINE__)

/* We directly memcpy int32_t from wasm memory */
/* TODO: fix this */
COMPILE_TIME_ASSERT(-1 == ~0);

/* we need to at least be able to compute wasm addresses */
COMPILE_TIME_ASSERT(sizeof(size_t) >= sizeof(uint32_t));

/* the sys_ interface uses ints extensively,
   we need to be able to represent wasm values in ints */
COMPILE_TIME_ASSERT(sizeof(int) >= sizeof(uint32_t));

#define __MMAP0(args,m,...)
#define __MMAP1(args,m,t,a,...) m(args, t, a)
#define __MMAP2(args,m,t,a,...) m(args, t, a) __MMAP1(args,m,__VA_ARGS__)
#define __MMAP3(args,m,t,a,...) m(args, t, a) __MMAP2(args,m,__VA_ARGS__)
#define __MMAP4(args,m,t,a,...) m(args, t, a) __MMAP3(args,m,__VA_ARGS__)
#define __MMAP5(args,m,t,a,...) m(args, t, a) __MMAP4(args,m,__VA_ARGS__)
#define __MMAP6(args,m,t,a,...) m(args, t, a) __MMAP5(args,m,__VA_ARGS__)
#define __MMAP7(args,m,t,a,...) m(args, t, a) __MMAP6(args,m,__VA_ARGS__)
#define __MMAP(args,n,...) __MMAP##n(args, __VA_ARGS__)

#define __DECL(args, t, a) t a;
#define __SWAP(args, t, a) args.a = t ## _swap_bytes(args.a);

static int32_t int32_t_swap_bytes(int32_t a)
{
	return uint32_t_swap_bytes(a);
}

#define LOAD_ARGS_CUSTOM(args, funcinst, varargs, n, ...)		\
	struct {							\
		__MMAP(args, n, __DECL, __VA_ARGS__)			\
	} args;								\
	if (_wasmjit_emscripten_copy_from_user(funcinst,		\
					       &args, varargs,		\
					       sizeof(args)))		\
		return -EM_EFAULT;					\
	__MMAP(args, n, __SWAP, __VA_ARGS__)

#define LOAD_ARGS(...)				\
	LOAD_ARGS_CUSTOM(args, __VA_ARGS__)

enum {
#define ERRNO(name, value) EM_ ## name = value,
#include <wasmjit/emscripten_runtime_sys_errno_def.h>
#undef ERRNO
};

#ifndef __LONG_WIDTH__
#ifdef __LP64__
#define __LONG_WIDTH__ 64
#else
#error Please define __LONG_WIDTH__
#endif
#endif

__attribute__((unused))
static int32_t _convert_errno(long errno_)
{
	static int32_t to_sys_errno[] = {
#define ERRNO(name, value) [name] = -value,
#include <wasmjit/emscripten_runtime_sys_errno_def.h>
#undef ERRNO
	};

	int32_t toret;

	if ((size_t) errno_ >= sizeof(to_sys_errno) / sizeof(to_sys_errno[0])) {
		toret = -EM_EINVAL;
	} else {
		toret = to_sys_errno[errno_];
		if (!toret)
			toret = -EM_EINVAL;
	}

	return toret;
}

#define convert_errno(x) (-_convert_errno((x)))

/* error codes are the same for these targets */
/* issigned == 0 implies no error */
#if (defined(__KERNEL__) || defined(__linux__)) && defined(__x86_64__)

static int32_t check_ret_signed(long errno_, int issigned)
{
	if (!issigned) {
#if __LONG_WIDTH__ > 32
		if ((unsigned long) errno_ > UINT32_MAX)
			wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
#endif
		return (unsigned long) errno_;
	}
#if __LONG_WIDTH__ > 32
	if (errno_ < -2147483648)
		wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
#endif
#if __LONG_WIDTH__ > 32
	if (errno_ > INT32_MAX)
		wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
#endif
	return errno_;
}

#else

static int32_t check_ret_signed(long errno_, int issigned)
{
	if (!issigned) {
#if __LONG_WIDTH__ > 32
		if ((unsigned long) errno_ > UINT32_MAX)
			wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
#endif
		return (unsigned long) errno_;
	}

	if (errno_ >= 0) {
#if __LONG_WIDTH__ > 32
		if (errno_ > INT32_MAX)
			wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);
#endif
		return errno_;
	}

	if (errno_ == LONG_MIN)
		wasmjit_trap(WASMJIT_TRAP_INTEGER_OVERFLOW);

	errno_ = -errno_;

	return _convert_errno(errno_);
}

#endif

#define check_ret(errno_) check_ret_signed((errno_), 1)

static int wasmjit_emscripten_check_range(struct MemInst *meminst,
					  uint32_t user_ptr,
					  size_t extent)
{
	size_t ret;
	if (!user_ptr)
		return 0;
	if (__builtin_add_overflow(user_ptr, extent, &ret))
		return 0;
	return ret <= meminst->size;
}

static size_t wasmjit_emscripten_copy_to_user(struct MemInst *meminst,
					      uint32_t user_dest_ptr,
					      void *src,
					      size_t src_size)
{
	if (!wasmjit_emscripten_check_range(meminst, user_dest_ptr, src_size)) {
		return src_size;
	}

	memcpy(user_dest_ptr + meminst->data, src, src_size);
	return 0;
}

static void wasmjit_memcpy_from_user(struct MemInst *restrict meminst,
				     void *restrict dest,
				     uint32_t user_src_ptr,
				     size_t src_size)
{
	size_t i;
	char *destc = dest;
	for (i = 0; i < (src_size & (~(size_t) 0x7)); i += 0x8) {
		uint32_t sptr = wasmjit_array_index_nospec(user_src_ptr + i, 0x8, meminst->size);
		memcpy(destc + i, meminst->data + sptr, 0x8);
	}

	src_size &= 0x7;

	if (src_size & 0x4) {
		uint32_t sptr = wasmjit_array_index_nospec(user_src_ptr + i, 0x4, meminst->size);
		memcpy(destc + i, meminst->data + sptr, 0x4);
		i += 0x4;
	}

	if (src_size & 0x2) {
		uint32_t sptr = wasmjit_array_index_nospec(user_src_ptr + i, 0x2, meminst->size);
		memcpy(destc + i, meminst->data + sptr, 0x2);
		i += 0x2;
	}

	if (src_size & 0x1) {
		uint32_t sptr = wasmjit_array_index_nospec(user_src_ptr + i, 0x1, meminst->size);
		memcpy(destc + i, meminst->data + sptr, 0x1);
		i += 0x1;
	}
}

static size_t wasmjit_emscripten_copy_from_user(struct MemInst *meminst,
						void *dest,
						uint32_t user_src_ptr,
						size_t src_size)
{
	if (!wasmjit_emscripten_check_range(meminst, user_src_ptr,
					    src_size)) {
		return src_size;
	}

	wasmjit_memcpy_from_user(meminst, dest, user_src_ptr, src_size);
	return 0;
}

static int _wasmjit_emscripten_check_string(struct FuncInst *funcinst,
					    uint32_t user_ptr,
					    size_t max)
{
	struct MemInst *meminst = wasmjit_emscripten_get_mem_inst(funcinst);
	size_t len = 0;

	while (user_ptr < meminst->size &&
	       len < max) {
		user_ptr = wasmjit_array_index_nospec(user_ptr, 1, meminst->size);
		if (!*(meminst->data + user_ptr))
			return 1;
		user_ptr += 1;
		len += 1;
	}

	return 0;
}

/* shortcut functions */
#define _wasmjit_emscripten_check_range(funcinst, user_ptr, src_size) \
	wasmjit_emscripten_check_range(wasmjit_emscripten_get_mem_inst(funcinst), \
				       user_ptr,			\
				       src_size)			\

#define _wasmjit_memcpy_from_user(funcinst, dest, user_ptr, src_size)	\
	wasmjit_memcpy_from_user(wasmjit_emscripten_get_mem_inst((funcinst)), \
				 (dest),				\
				 (user_ptr),				\
				 (src_size))

#define _wasmjit_emscripten_copy_to_user(funcinst, user_dest_ptr, src, src_size) \
	wasmjit_emscripten_copy_to_user(wasmjit_emscripten_get_mem_inst(funcinst), \
					user_dest_ptr,			\
					src,				\
					src_size)			\

#define _wasmjit_emscripten_copy_from_user(funcinst, dest, user_src_ptr, src_size) \
	wasmjit_emscripten_copy_from_user(wasmjit_emscripten_get_mem_inst(funcinst), \
					  dest,				\
					  user_src_ptr,			\
					  src_size)			\

static char *wasmjit_emscripten_get_base_address(struct FuncInst *funcinst) {
	return wasmjit_emscripten_get_mem_inst(funcinst)->data;
}

/* shortcut function */
static struct EmscriptenContext *_wasmjit_emscripten_get_context(struct FuncInst *funcinst)
{
	return wasmjit_emscripten_get_context(funcinst->module_inst);
}

static struct EmscriptenContext *_g_handler_ctx;
static sig_atomic_t _g_handler_setting;

static void compiler_barrier(void) {
	asm volatile ("" : : : "memory");
}

static void set_signal_context(struct EmscriptenContext *ctx)
{
	_g_handler_setting = 1;
	compiler_barrier();
	/* There should only be one instance of emscripten module per process,
	   there is no isolation between modules, only process-level isolation
	 */
	assert(_g_handler_ctx == NULL);
	_g_handler_ctx = ctx;
	compiler_barrier();
	_g_handler_setting = 0;
}

static void remove_signal_context(void)
{
	_g_handler_setting = 1;
	compiler_barrier();
	_g_handler_ctx = NULL;
	compiler_barrier();
	_g_handler_setting = 0;
}

#ifdef __KERNEL__

typedef int wasmjit_signal_block_ctx;

static void _wasmjit_block_signals(wasmjit_signal_block_ctx *set)
{
	(void) set;
}

static void _wasmjit_unblock_signals(const wasmjit_signal_block_ctx *set)
{
	(void) set;
}

static size_t _wasmjit_add_unfreed_pointer(struct FuncInst *funcinst, void *ptr)
{
	(void) funcinst;
	(void) ptr;
	return 0;
}

static void _wasmjit_remove_unfreed_pointer(struct FuncInst *funcinst, size_t ptr)
{
	(void) funcinst;
	(void) ptr;
}

#else

typedef sigset_t wasmjit_signal_block_ctx;

/* We must block signals when calling non-async safe functions
   because we may siglongjmp during signal handlers, and we can't
   leave those functions in incomplete states */
static void _wasmjit_block_signals(wasmjit_signal_block_ctx *set)
{
	int ret;
	sigset_t new;
	sigfillset(&new);
	ret = sigprocmask(SIG_SETMASK, &new, set);
	if (ret < 0) {
		/* this can't fail */
		abort();
	}
}

static void _wasmjit_unblock_signals(const wasmjit_signal_block_ctx *set)
{
	int ret;
	ret = sigprocmask(SIG_SETMASK, set, NULL);
	if (ret < 0) {
		/* this can't fail */
		abort();
	}
}

static size_t _wasmjit_add_unfreed_pointer(struct FuncInst *funcinst, void *ptr)
{
	size_t i;
	struct EmscriptenContext *ctx;

	assert(ptr);

	ctx = _wasmjit_emscripten_get_context(funcinst);
	for (i = 0; i < ctx->unfreed_pointers.n_elts; ++i) {
		if (!ctx->unfreed_pointers.elts[i]) {
			ctx->unfreed_pointers.elts[i] = ptr;
			return i + 1;
		}
	}

	if (ctx->unfreed_pointers.n_elts == SIZE_MAX - 1)
		return 0;

	/* all pointers are taken, grow vector */
	if (!VECTOR_GROW(&ctx->unfreed_pointers, 1))
		return 0;

	ctx->unfreed_pointers.elts[ctx->unfreed_pointers.n_elts - 1] = ptr;

	return ctx->unfreed_pointers.n_elts;
}

static void _wasmjit_remove_unfreed_pointer(struct FuncInst *funcinst, size_t idx)
{
	struct EmscriptenContext *ctx;
	if (!idx) return;
	ctx = _wasmjit_emscripten_get_context(funcinst);
	ctx->unfreed_pointers.elts[idx - 1] = NULL;
}

#endif

int wasmjit_emscripten_init(struct EmscriptenContext *ctx,
			    struct ModuleInst *asm_,
			    struct FuncInst *errno_location_inst,
			    struct FuncInst *malloc_inst,
			    struct FuncInst *free_inst,
			    char *envp[])
{
	if (malloc_inst) {
		struct FuncType malloc_type;
		wasmjit_valtype_t malloc_input_type = VALTYPE_I32;
		wasmjit_valtype_t malloc_return_type = VALTYPE_I32;

		_wasmjit_create_func_type(&malloc_type,
					  1, &malloc_input_type,
					  1, &malloc_return_type);

		if (!wasmjit_typecheck_func(&malloc_type,
					    malloc_inst))
			return -1;
	}

	if (free_inst) {
		struct FuncType free_type;
		wasmjit_valtype_t free_input_type = VALTYPE_I32;

		_wasmjit_create_func_type(&free_type,
					  1, &free_input_type,
					  0, NULL);

		if (!wasmjit_typecheck_func(&free_type,
					    free_inst))
			return -1;
	}

	if (errno_location_inst) {
		struct FuncType errno_location_type;
		wasmjit_valtype_t errno_location_return_type = VALTYPE_I32;

		_wasmjit_create_func_type(&errno_location_type,
					  0, NULL,
					  1, &errno_location_return_type);

		if (!wasmjit_typecheck_func(&errno_location_type,
					    errno_location_inst)) {
			return -1;
		}
	}

	ctx->asm_ = asm_;
	ctx->errno_location_inst = errno_location_inst;
	ctx->malloc_inst = malloc_inst;
	ctx->free_inst = free_inst;
	ctx->environ = envp;
	ctx->buildEnvironmentCalled = 0;
	ctx->fd_table.n_elts = 0;
	ctx->gai_strerror_buffer = 0;
	ctx->getenv_buffer = 0;
	ctx->getgrent_buffer = 0;
	ctx->getpwent_buffer = 0;
	ctx->tmzone_buffer = 0;
	ctx->LLVM_SAVEDSTACKS = NULL;
	ctx->LLVM_SAVEDSTACKS_sz = 0;
	ctx->tmtm_buffer = 0;
	ctx->sem_table.n_elts = 0;
	ctx->unfreed_pointers.n_elts = 0;

	/* EM_SIG_DFL == 0 */
	memset(ctx->sig_handlers, 0, sizeof(ctx->sig_handlers));

	set_signal_context(ctx);

	return 0;
}

int wasmjit_emscripten_build_environment(struct FuncInst *environ_constructor)
{
	int ret;
	if (environ_constructor) {
		struct FuncType errno_location_type;

		_wasmjit_create_func_type(&errno_location_type,
					  0, NULL,
					  0, NULL);

		if (!wasmjit_typecheck_func(&errno_location_type,
					    environ_constructor)) {
			return -1;
		}

		ret = wasmjit_invoke_function(environ_constructor, NULL, NULL);
		if (ret)
			return -1;
	}

	return 0;
}

int wasmjit_emscripten_invoke_main(struct MemInst *meminst,
				   struct FuncInst *stack_alloc_inst,
				   struct FuncInst *main_inst,
				   int argc,
				   char *argv[]) {
	uint32_t (*stack_alloc)(uint32_t);
	union ValueUnion out;
	int ret;

	if (!(stack_alloc_inst->type.n_inputs == 1 &&
	      stack_alloc_inst->type.input_types[0] == VALTYPE_I32 &&
	      stack_alloc_inst->type.output_type)) {
		return -1;
	}

	stack_alloc = stack_alloc_inst->compiled_code;

	if (main_inst->type.n_inputs == 0 &&
	    main_inst->type.output_type == VALTYPE_I32) {
		ret = wasmjit_invoke_function(main_inst, NULL, &out);
	} else if ((main_inst->type.n_inputs == 2 ||
		    (main_inst->type.n_inputs == 3 &&
		     main_inst->type.input_types[2] == VALTYPE_I32)) &&
		   main_inst->type.input_types[0] == VALTYPE_I32 &&
		   main_inst->type.input_types[1] == VALTYPE_I32 &&
		   main_inst->type.output_type == VALTYPE_I32) {
		uint32_t argv_i, zero = 0;
		int i;
		union ValueUnion args[3];

		argv_i = stack_alloc((argc + 1) * 4);

		for (i = 0; i < argc; ++i) {
			size_t len = strlen(argv[i]) + 1;
			uint32_t ret = stack_alloc(len);

			if (wasmjit_emscripten_copy_to_user(meminst,
							    ret,
							    argv[i],
							    len))
				return -1;

			ret = uint32_t_swap_bytes(ret);

			if (wasmjit_emscripten_copy_to_user(meminst,
							    argv_i + i * 4,
							    &ret,
							    4))
				return -1;
		}

		if (wasmjit_emscripten_copy_to_user(meminst,
						    argv_i + argc * 4,
						    &zero, 4))
			return -1;

		args[0].i32 = argc;
		args[1].i32 = argv_i;
		args[2].i32 = 0;

		ret = wasmjit_invoke_function(main_inst, args, &out);

		/* TODO: copy back mutations to argv? */
	} else {
		return -1;
	}

	if (ret) {
		if (WASMJIT_DECODE_TRAP_ERROR(ret) == WASMJIT_TRAP_EXIT) {
			ret &= 0xff;
		}

		return ret;
	}

	return 0xff & out.i32;
}

void wasmjit_emscripten_abortStackOverflow(uint32_t allocSize, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)allocSize;
	wasmjit_emscripten_internal_abort("Stack overflow!");
}

uint32_t wasmjit_emscripten_abortOnCannotGrowMemory(struct FuncInst *funcinst)
{
	(void)funcinst;
	wasmjit_emscripten_internal_abort("Cannot enlarge memory arrays.");
	return 0;
}

uint32_t wasmjit_emscripten_enlargeMemory(struct FuncInst *funcinst)
{
	(void)funcinst;
	return 0;
}

uint32_t wasmjit_emscripten_getTotalMemory(struct FuncInst *funcinst)
{
	(void)funcinst;
	return WASMJIT_EMSCRIPTEN_TOTAL_MEMORY;
}

void wasmjit_emscripten_nullFunc_ii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'ii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_iii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'iii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_iiii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'iiii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_iiiii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'iiiii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_iiiiii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'iiiiii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_vi(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'vi'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_vii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'vii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_viii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'viii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten_nullFunc_viiii(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
	wasmjit_emscripten_internal_abort("Invalid function pointer called with signature 'viiii'. Perhaps this is an invalid value (e.g. caused by calling a virtual method on a NULL pointer)? Or calling a function with an incorrect type, which will fail? (it is worth building your source files with -Werror (warnings are errors), as warnings can indicate undefined behavior which can cause this)");
}

void wasmjit_emscripten____lock(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
}

void wasmjit_emscripten____setErrNo(uint32_t value, struct FuncInst *funcinst)
{
	union ValueUnion out;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	if (ctx->errno_location_inst &&
	    !wasmjit_invoke_function(ctx->errno_location_inst, NULL, &out)) {
		value = uint32_t_swap_bytes(value);
		if (!_wasmjit_emscripten_copy_to_user(funcinst, out.i32, &value, sizeof(value)))
			return;
	}
	wasmjit_emscripten_internal_abort("failed to set errno from JS");
}

uint32_t wasmjit_emscripten____syscall3(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, fd,
		  uint32_t, buf,
		  uint32_t, count);

	(void)which;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, args.count))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	return check_ret(sys_read(args.fd, base + args.buf, args.count));
}

uint32_t wasmjit_emscripten____syscall42(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	int32_t pipes[2];
	int32_t ret;
	int sys_pipes[2];

	LOAD_ARGS(funcinst, varargs, 1,
		  uint32_t, pipefd);

	(void)which;

	ret = check_ret(sys_pipe(sys_pipes));
	if (ret < 0)
		return ret;

#if __INT_WIDTH__ > 32
	if (sys_pipes[0] > INT32_MAX ||
	    sys_pipes[0] < INT32_MIN ||
	    sys_pipes[1] > INT32_MAX ||
	    sys_pipes[1] < INT32_MIN) {
		/* Have to abort here because we've already allocated the pipes */
		wasmjit_emscripten_internal_abort("Pipe fds too large");
	}
#endif

	pipes[0] = uint32_t_swap_bytes((uint32_t) sys_pipes[0]);
	pipes[1] = uint32_t_swap_bytes((uint32_t) sys_pipes[1]);

	if (_wasmjit_emscripten_copy_to_user(funcinst, args.pipefd, pipes, sizeof(pipes)))
		return -EM_EFAULT;

	return 0;
}


/*  _llseek */
uint32_t wasmjit_emscripten____syscall140(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;
	int32_t ret;
	int64_t off;

	LOAD_ARGS(funcinst, varargs, 5,
		  uint32_t, fd,
		  uint32_t, offset_high,
		  uint32_t, offset_low,
		  uint32_t, result,
		  int32_t, whence);

	(void)which;

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (!_wasmjit_emscripten_check_range(funcinst, args.result, 4))
		return -EM_EFAULT;

	off = args.offset_low | (((uint64_t)args.offset_high) << 32);

#ifndef OFF_MAX
#define OFF_MAX ((off_t)((((uintmax_t)1) << (sizeof(off_t) * 8 - 1)) - 1))
#endif
#ifndef OFF_MIN
#define OFF_MIN ((off_t)(((uintmax_t)1) << (sizeof(off_t) * 8 - 1)))
#endif

	/* TODO: we should compile with _FILE_OFFSET_BITS == 64 on
	   32-bit linux */
	if (sizeof(off_t) < 8 &&
	    (off > OFF_MAX  ||
	     off < OFF_MIN))
		return -EM_EOVERFLOW;

#if !IS_LINUX
	{
		struct EmscriptenContext *ctx;
		uint32_t fds;
		int complete = 0;
		wasmjit_signal_block_ctx set;

		_wasmjit_block_signals(&set);

		ctx = _wasmjit_emscripten_get_context(funcinst);

		fds = args.fd;
		if (fds < ctx->fd_table.n_elts) {
			fds = wasmjit_array_index_nospec(fds, 1, ctx->fd_table.n_elts);
			struct EmFile *file = ctx->fd_table.elts[fds];
			if (file) {
				if (file->dirp) {
					seekdir(file->dirp, off);
					complete = 1;
				}
			}
		}

		_wasmjit_unblock_signals(&set);

		if (complete)
			return 0;
	}
#endif

	ret = check_ret(sys_lseek(args.fd, off, args.whence));

	if (ret) {
		return ret;
	} else {
		memcpy(base + args.result, &ret, sizeof(ret));
		return 0;
	}
}

struct em_iovec {
	uint32_t iov_base;
	uint32_t iov_len;
};

static long copy_iov(struct FuncInst *funcinst,
		     uint32_t iov_user,
		     uint32_t iov_len, struct iovec **out)
{
	struct iovec *liov = NULL;
	char *base;
	long ret;
	uint32_t i;

	base = wasmjit_emscripten_get_base_address(funcinst);

	/* TODO: do UIO_FASTIOV stack optimization */
	liov = wasmjit_alloc_vector(iov_len,
				    sizeof(struct iovec), NULL);
	if (!liov) {
		ret = -ENOMEM;
		goto error;
	}

	for (i = 0; i < iov_len; ++i) {
		struct em_iovec iov;

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &iov,
						       iov_user +
						       sizeof(struct em_iovec) * i,
						       sizeof(struct em_iovec))) {
			ret = -EFAULT;
			goto error;
		}

		iov.iov_base = uint32_t_swap_bytes(iov.iov_base);
		iov.iov_len = uint32_t_swap_bytes(iov.iov_len);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     iov.iov_base,
						     iov.iov_len)) {
			ret = -EFAULT;
			goto error;
		}

		liov[i].iov_base = base + iov.iov_base;
		liov[i].iov_len = iov.iov_len;
	}

	*out = liov;
	ret = 0;

	if (0) {
	error:
		free(liov);
	}

	return ret;
}

/* writev */
uint32_t wasmjit_emscripten____syscall146(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	long rret;
	struct iovec *liov = NULL;
	wasmjit_signal_block_ctx set;
	size_t ptr_idx_1 = 0;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, fd,
		  uint32_t, iov,
		  uint32_t, iovcnt);

	(void)which;

	_wasmjit_block_signals(&set);

	rret = copy_iov(funcinst, args.iov, args.iovcnt, &liov);
	if (rret)
		goto error;

	ptr_idx_1 = _wasmjit_add_unfreed_pointer(funcinst, liov);
	if (!ptr_idx_1) {
		rret = -ENOMEM;
		goto error;
	}

	_wasmjit_unblock_signals(&set);

	/* writev is not documented to be async-signal-safe anywhere
	   but since it can arbitrarily block and it's most likely async-signal-safe
	   it's better to not block signals while calling it, otherwise we may make the
	   process annoyingly resistant to signals (like SIGINT, i.e. Ctrl-C)
	   TODO: disable functionality on systems where it's not async-signal-safe
	*/
	rret = sys_writev(args.fd, liov, args.iovcnt);

	_wasmjit_block_signals(&set);

 error:
	_wasmjit_remove_unfreed_pointer(funcinst, ptr_idx_1);
	free(liov);

	_wasmjit_unblock_signals(&set);

	return check_ret(rret);
}

/* write */
uint32_t wasmjit_emscripten____syscall4(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, fd,
		  uint32_t, buf,
		  uint32_t, count);

	(void)which;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, args.count))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	return check_ret(sys_write(args.fd, base + args.buf, args.count));
}

/* ioctl */
uint32_t wasmjit_emscripten____syscall54(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	(void)funcinst;
	/* TODO: need to define non-no filesystem case */
	(void)which;
	(void)varargs;
	return 0;
}

/* close */
uint32_t wasmjit_emscripten____syscall6(uint32_t which, uint32_t varargs, struct FuncInst *funcinst)
{
	wasmjit_signal_block_ctx set;
	long closedret;

	/* TODO: need to define non-no filesystem case */
	LOAD_ARGS(funcinst, varargs, 1,
		  uint32_t, fd);

	(void)which;

	_wasmjit_block_signals(&set);

#if !IS_LINUX
	{
		struct EmscriptenContext *ctx;
		uint32_t fds;

		ctx = _wasmjit_emscripten_get_context(funcinst);

		fds = args.fd;
		if (fds < ctx->fd_table.n_elts) {
			fds = wasmjit_array_index_nospec(fds, 1, ctx->fd_table.n_elts);
			struct EmFile *file = ctx->fd_table.elts[fds];
			if (file) {
				int closed = !!file->dirp;
				if (file->dirp) {
					if (closedir(file->dirp) < 0) {
						closedret = -errno;
					} else {
						closedret = 0;
					}
				}
				free(file);
				ctx->fd_table.elts[fds] = NULL;
				if (closed)
					goto out;
			}
		}
	}
#endif

	closedret = sys_close(args.fd);

 out:
	_wasmjit_unblock_signals(&set);

	return check_ret(closedret);
}

void wasmjit_emscripten____unlock(uint32_t x, struct FuncInst *funcinst)
{
	(void)funcinst;
	(void)x;
}

uint32_t wasmjit_emscripten__emscripten_memcpy_big(uint32_t dest, uint32_t src, uint32_t num, struct FuncInst *funcinst)
{
	char *base = wasmjit_emscripten_get_base_address(funcinst);
	if (!_wasmjit_emscripten_check_range(funcinst, dest, num) ||
	    !_wasmjit_emscripten_check_range(funcinst, src, num)) {
		wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);
	}
	_wasmjit_memcpy_from_user(funcinst, dest + base, src, num);
	return dest;
}

__attribute__((noreturn))
void wasmjit_emscripten_abort(uint32_t what, struct FuncInst *funcinst)
{
	struct MemInst *meminst = wasmjit_emscripten_get_mem_inst(funcinst);
	char *abort_string;
	if (!_wasmjit_emscripten_check_string(funcinst, what, 256)) {
		abort_string = "Invalid abort string";
	} else {
		abort_string = meminst->data + what;
	}
	wasmjit_emscripten_internal_abort(abort_string);
}

static uint32_t getMemory(struct FuncInst *funcinst,
			  uint32_t amount)
{
	struct EmscriptenContext *ctx;
	union ValueUnion input, output;
	input.i32 = amount;

	ctx = _wasmjit_emscripten_get_context(funcinst);
	if (!ctx->malloc_inst)
		return 0;
	if (wasmjit_invoke_function(ctx->malloc_inst, &input, &output))
		return 0;

	/* check if userspace is malicious */
	if (!_wasmjit_emscripten_check_range(funcinst,
					     output.i32,
					     amount)) {
		return 0;
	}

	return output.i32;
}

static void freeMemory(struct EmscriptenContext *ctx,
		       uint32_t ptr)
{
	union ValueUnion input;
	input.i32 = ptr;
	if (!ctx->free_inst)
		wasmjit_emscripten_internal_abort("Failed to invoke deallocator");
	if (wasmjit_invoke_function(ctx->free_inst, &input, NULL))
		wasmjit_emscripten_internal_abort("Failed to invoke deallocator");
}

void wasmjit_emscripten____buildEnvironment(uint32_t environ_arg,
					    struct FuncInst *funcinst)
{
	char *base = wasmjit_emscripten_get_base_address(funcinst);
	uint32_t envPtr;
	uint32_t poolPtr;
	uint32_t total_pool_size;
	uint32_t n_envs;
	size_t i;
	char **env;
	struct EmscriptenContext *ctx = _wasmjit_emscripten_get_context(funcinst);
	wasmjit_signal_block_ctx set;

	_wasmjit_block_signals(&set);

	if (ctx->buildEnvironmentCalled) {
		/* free old stuff */
		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &envPtr,
						       environ_arg,
						       sizeof(envPtr)))
			wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

		envPtr = uint32_t_swap_bytes(envPtr);

		freeMemory(ctx, envPtr);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &poolPtr,
						       envPtr,
						       sizeof(poolPtr)))
			wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);

		poolPtr = uint32_t_swap_bytes(poolPtr);

		freeMemory(ctx, poolPtr);
	}

	n_envs = 0;
	total_pool_size = 0;
	for (env = ctx->environ; *env; ++env) {
		total_pool_size += strlen(*env) + 1;
		n_envs += 1;
	}

	poolPtr = getMemory(funcinst, total_pool_size);
	if (!poolPtr)
		wasmjit_emscripten_internal_abort("Failed to allocate memory in critical region");

	envPtr = getMemory(funcinst, (n_envs + 1) * 4);
	if (!envPtr)
		wasmjit_emscripten_internal_abort("Failed to allocate memory in critical region");

	{
		uint32_t ep;
		ep = uint32_t_swap_bytes(envPtr);
		if (_wasmjit_emscripten_copy_to_user(funcinst,
						     environ_arg,
						     &ep,
						     sizeof(envPtr)))
			wasmjit_trap(WASMJIT_TRAP_MEMORY_OVERFLOW);
	}

	for (env = ctx->environ, i = 0; *env; ++env, ++i) {
		size_t len = strlen(*env);
		uint32_t pp;
		/* NB: these memcpys are checked above */
		memcpy(base + poolPtr, *env, len + 1);
		pp = uint32_t_swap_bytes(poolPtr);
		memcpy(base + envPtr + i * sizeof(uint32_t),
		       &pp, sizeof(poolPtr));
		poolPtr += len + 1;
	}

	poolPtr = 0;
	memset(base + envPtr + i * sizeof(uint32_t),
	       0, sizeof(poolPtr));

	ctx->buildEnvironmentCalled = 1;

	_wasmjit_unblock_signals(&set);
}

uint32_t wasmjit_emscripten____syscall10(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 1,
		  uint32_t, pathname);

	(void)which;

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	return check_ret(sys_unlink(base + args.pathname));
}

#ifndef __INT_WIDTH__
#ifdef __LP64__
#define __INT_WIDTH__ 32
#else
#error Please define __INT_WIDTH__
#endif
#endif

#if (defined(__linux__) || defined(__KERNEL__)) && !defined(__mips__)

static int convert_socket_type_to_local(int32_t type)
{
	return type;
}

static int32_t convert_socket_type_to_em(int type)
{
	return type;
}

#else

static int convert_socket_type_to_local(int32_t type)
{
	int ltype, nonblock_type, cloexec_type;

#define EM_SOCK_NONBLOCK 2048
	nonblock_type = !!(type & EM_SOCK_NONBLOCK);
	type &= ~(int32_t) EM_SOCK_NONBLOCK;
#define EM_SOCK_CLOEXEC 524288
	cloexec_type = !!(type & EM_SOCK_CLOEXEC);
	type &= ~(int32_t) EM_SOCK_CLOEXEC;

	switch (type) {
	case 1: ltype = SOCK_STREAM; break;
	case 2: ltype = SOCK_DGRAM; break;
	case 5: ltype = SOCK_SEQPACKET; break;
	case 3: ltype = SOCK_RAW; break;
	case 4: ltype = SOCK_RDM; break;
#ifdef SOCK_PACKET
	case 10: ltype = SOCK_PACKET; break;
#endif
	default: return -1;
	}

	if (nonblock_type) {
#ifdef SOCK_NONBLOCK
		ltype |= SOCK_NONBLOCK;
#else
		/*
		  user requested SOCK_NONBLOCK but runtime doesn't support it,
		  return a type of -1, which will invoke a EINVAL on sys_socket()
		 */
		return -1;
#endif
	}

	if (cloexec_type) {
#ifdef SOCK_CLOEXEC
		ltype |= SOCK_CLOEXEC;
#else
		/*
		  user requested SOCK_CLOEXEC but runtime doesn't support it,
		  return a type of -1, which will invoke a EINVAL on sys_socket()
		 */
		return -1;
#endif
	}

	return ltype;
}

#define EM_SOCK_STREAM  1
#define EM_SOCK_DGRAM	2
#define EM_SOCK_SEQPACKET 5
#define EM_SOCK_RAW	3
#define EM_SOCK_RDM	4
#define EM_SOCK_PACKET	10

static int32_t convert_socket_type_to_em(int type)
{
	int nonblock_type, cloexec_type;
	int32_t ltype;

#ifdef SOCK_NONBLOCK
	nonblock_type = !!(type & SOCK_NONBLOCK);
	type &= ~(int) SOCK_NONBLOCK;
#else
	nonblock_type = 0;
#endif

#ifdef SOCK_CLOEXEC
	cloexec_type = !!(type & SOCK_CLOEXEC);
	type &= ~(int) SOCK_CLOEXEC;
#else
	cloexec_type = 0;
#endif

	switch (type) {
	case SOCK_STREAM: ltype = EM_SOCK_STREAM; break;
	case SOCK_DGRAM: ltype = EM_SOCK_DGRAM; break;
	case SOCK_SEQPACKET: ltype = EM_SOCK_SEQPACKET; break;
	case SOCK_RAW: ltype = EM_SOCK_RAW; break;
	case SOCK_RDM: ltype = EM_SOCK_RDM; break;
#ifdef SOCK_PACKET
	case SOCK_PACKET: ltype = EM_SOCK_PACKET; break;
#endif
	default: return -1;
	}

	if (nonblock_type) {
		ltype |= EM_SOCK_NONBLOCK;
	}

	if (cloexec_type) {
		ltype |= EM_SOCK_CLOEXEC;
	}

	return ltype;
}

#endif

#define EM_AF_UNIX 1
#define EM_AF_INET 2
#define EM_AF_INET6 10

#if defined(__linux__) || defined(__KERNEL__)

static int convert_socket_domain_to_local(int32_t domain)
{
	return domain;
}

static int32_t convert_socket_domain_to_em(int domain)
{
	return domain;
}

static int convert_proto_to_local(int domain, int32_t proto)
{
	(void)domain;
	return proto;
}

static int32_t convert_proto_to_em(int32_t domain, int proto)
{
	(void)domain;
	return proto;
}


#else

static int convert_socket_domain_to_local(int32_t domain)
{
	switch (domain) {
	case EM_AF_UNIX: return AF_UNIX;
	case EM_AF_INET: return AF_INET;
	case EM_AF_INET6: return AF_INET6;
	case 4: return AF_IPX;
#ifdef AF_NETLINK
	case 16: return AF_NETLINK;
#endif
#ifdef AF_X25
	case 9: return AF_X25;
#endif
#ifdef AF_AX25
	case 3: return AF_AX25;
#endif
#ifdef AF_ATMPVC
	case 8: return AF_ATMPVC;
#endif
	case 5: return AF_APPLETALK;
#ifdef AF_PACKET
	case 17: return AF_PACKET;
#endif
	default: return -1;
	}
}

static int32_t convert_socket_domain_to_em(int domain)
{
	switch (domain) {
	case AF_UNIX: return EM_AF_UNIX;
	case AF_INET: return EM_AF_INET;
	case AF_INET6: return EM_AF_INET6;
	case AF_IPX: return 4;
#ifdef AF_NETLINK
	case AF_NETLINK: return 16;
#endif
#ifdef AF_X25
	case AF_X25: return 9;
#endif
#ifdef AF_AX25
	case AF_AX25: return 3;
#endif
#ifdef AF_ATMPVC
	case AF_ATMPVC: return 8;
#endif
	case AF_APPLETALK: return 5;
#ifdef AF_PACKET
	case AF_PACKET: return 17;
#endif
	default: return -1;
	}
}

static int convert_proto_to_local(int domain, int32_t proto)
{
	if (domain == AF_INET || domain == AF_INET6) {
		switch (proto) {
		case 0: return IPPROTO_IP;
		case 1: return IPPROTO_ICMP;
		case 2: return IPPROTO_IGMP;
		case 4: return IPPROTO_IPIP;
		case 6: return IPPROTO_TCP;
		case 8: return IPPROTO_EGP;
		case 12: return IPPROTO_PUP;
		case 17: return IPPROTO_UDP;
		case 22: return IPPROTO_IDP;
		case 29: return IPPROTO_TP;
#ifdef IPPROTO_DCCP
		case 33: return IPPROTO_DCCP;
#endif
		case 41: return IPPROTO_IPV6;
		case 46: return IPPROTO_RSVP;
		case 47: return IPPROTO_GRE;
		case 50: return IPPROTO_ESP;
		case 51: return IPPROTO_AH;
#ifdef IPPROTO_MTP
		case 92: return IPPROTO_MTP;
#endif
#ifdef IPPROTO_BEETPH
		case 94: return IPPROTO_BEETPH;
#endif
		case 98: return IPPROTO_ENCAP;
		case 103: return IPPROTO_PIM;
#ifdef IPPROTO_COMP
		case 108: return IPPROTO_COMP;
#endif
#ifdef IPPROTO_SCTP
		case 132: return IPPROTO_SCTP;
#endif
#ifdef IPPROTO_UDPLITE
		case 136: return IPPROTO_UDPLITE;
#endif
#if IPPROTO_MPLS
		case 137: return IPPROTO_MPLS;
#endif
		case 255: return IPPROTO_RAW;
		default: return -1;
		}
	} else {
		if (proto)
			return -1;
		return 0;
	}
}

static int32_t convert_proto_to_em(int32_t domain, int proto)
{
	if (domain == EM_AF_INET || domain == EM_AF_INET6) {
		switch (proto) {
		case IPPROTO_IP: return 0;
		case IPPROTO_ICMP: return 1;
		case IPPROTO_IGMP: return 2;
		case IPPROTO_IPIP: return 4;
		case IPPROTO_TCP: return 6;
		case IPPROTO_EGP: return 8;
		case IPPROTO_PUP: return 12;
		case IPPROTO_UDP: return 17;
		case IPPROTO_IDP: return 22;
		case IPPROTO_TP: return 29;
#ifdef IPPROTO_DCCP
		case IPPROTO_DCCP: return 33;
#endif
		case IPPROTO_IPV6: return 41;
		case IPPROTO_RSVP: return 46;
		case IPPROTO_GRE: return 47;
		case IPPROTO_ESP: return 50;
		case IPPROTO_AH: return 51;
#ifdef IPPROTO_MTP
		case IPPROTO_MTP: return 92;
#endif
#ifdef IPPROTO_BEETPH
		case IPPROTO_BEETPH: return 94;
#endif
		case IPPROTO_ENCAP: return 98;
		case IPPROTO_PIM: return 103;
#ifdef IPPROTO_COMP
		case IPPROTO_COMP: return 108;
#endif
#ifdef IPPROTO_SCTP
		case IPPROTO_SCTP: return 132;
#endif
#ifdef IPPROTO_UDPLITE
		case IPPROTO_UDPLITE: return 136;
#endif
#if IPPROTO_MPLS
		case IPPROTO_MPLS: return 137;
#endif
		case IPPROTO_RAW: return 255;
		default: return -1;
		}
	} else {
		if (proto)
			return -1;
		return 0;
	}
}

#endif


#define FAS 2

#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ && (defined(__linux__) || defined(__KERNEL__)))
#define SAME_SOCKADDR
#endif

#ifndef SAME_SOCKADDR

static long read_sockaddr(struct FuncInst *funcinst,
			  struct sockaddr_storage *ss, size_t *size,
			  uint32_t addr, uint32_t len)
{
	uint16_t family;
	assert(sizeof(family) == FAS);

	if (len < FAS)
		return -1;

	_wasmjit_memcpy_from_user(funcinst, &family, addr, FAS);
	family = uint16_t_swap_bytes(family);

	switch (family) {
	case EM_AF_UNIX: {
		struct sockaddr_un sun;
		if (len - FAS > sizeof(sun.sun_path))
			return -1;
		memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_UNIX;
		_wasmjit_memcpy_from_user(funcinst, &sun.sun_path, addr + FAS, len - FAS);
		*size = offsetof(struct sockaddr_un, sun_path) + (len - FAS);
		memcpy(ss, &sun, *size);
		break;
	}
	case EM_AF_INET: {
		struct sockaddr_in sin;
		if (len < 8)
			return -1;
		memset(&sin, 0, sizeof(struct sockaddr_in));
		sin.sin_family = AF_INET;
		/* these are in network order so they don't need to be swapped */
		assert(sizeof(sin.sin_port) == 2);
		_wasmjit_memcpy_from_user(funcinst, &sin.sin_port, addr + FAS, 2);
		assert(sizeof(sin.sin_addr) == 4);
		_wasmjit_memcpy_from_user(funcinst, &sin.sin_addr, addr + FAS + 2, 4);
		*size = sizeof(struct sockaddr_in);
		memcpy(ss, &sin, *size);
		break;
	}
	case EM_AF_INET6: {
		struct sockaddr_in6 sin6;

		if (len < 28)
			return -1;

		memset(&sin6, 0, sizeof(struct sockaddr_in6));
		sin6.sin6_family = AF_INET6;

		/* this is in network order so it doesn't need to be swapped */
		assert(sizeof(sin6.sin6_port) == 2);
		_wasmjit_memcpy_from_user(funcinst, &sin6.sin6_port, addr + FAS, 2);

		assert(4 == sizeof(sin6.sin6_flowinfo));
		_wasmjit_memcpy_from_user(funcinst, &sin6.sin6_flowinfo, addr + FAS + 2, 4);
		sin6.sin6_flowinfo = uint32_t_swap_bytes(sin6.sin6_flowinfo);

		/* this is in network order so it doesn't need to be swapped */
		assert(16 == sizeof(sin6.sin6_addr));
		_wasmjit_memcpy_from_user(funcinst, &sin6.sin6_addr, addr + FAS + 2 + 4, 16);

		assert(4 == sizeof(sin6.sin6_scope_id));
		_wasmjit_memcpy_from_user(funcinst, &sin6.sin6_scope_id, addr + FAS + 2 + 4 + 16, 4);
		sin6.sin6_scope_id = uint32_t_swap_bytes(sin6.sin6_scope_id);

		*size = sizeof(struct sockaddr_in6);
		memcpy(ss, &sin6, *size);
		break;
	}
	default: {
		/* TODO: add more support */
		return -1;
		break;
	}
	}

	return 0;
}

/* need to byte swap and adapt sockaddr to current platform */
static long write_sockaddr(struct sockaddr *ss, socklen_t ssize,
			   char *addr, uint32_t addrlen, void *len)
{
	uint32_t newlen;

	if (!ssize) {
		memset(len, 0, sizeof(newlen));
		return 0;
	}

	switch (ss->sa_family) {
	case AF_UNIX: {
		struct sockaddr_un sun;
		uint16_t f = uint16_t_swap_bytes(EM_AF_UNIX);

		newlen = FAS + (ssize - offsetof(struct sockaddr_un, sun_path));

		if (addrlen < newlen)
			return -1;

		if (ssize > sizeof(sun))
			return -1;

		memcpy(&sun, ss, ssize);

		memcpy(addr, &f, FAS);
		memcpy(addr + FAS, sun.sun_path, ssize - offsetof(struct sockaddr_un, sun_path));
		break;
	}
	case AF_INET: {
		struct sockaddr_in sin;
		uint16_t f = uint16_t_swap_bytes(EM_AF_INET);

		newlen = FAS + 2 + 4;

		if (addrlen < newlen)
			return -1;

		if (ssize != sizeof(sin))
			return -1;

		memcpy(&sin, ss, sizeof(sin));

		memcpy(addr, &f, FAS);
		/* these are in network order so they don't need to be swapped */
		memcpy(addr + FAS, &sin.sin_port, 2);
		memcpy(addr + FAS + 2, &sin.sin_addr, 4);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 sin6;
		uint16_t f = uint16_t_swap_bytes(EM_AF_INET6);

		newlen = FAS + 2 + 4 + 16 + 4;

		if (addrlen < newlen)
			return -1;

		if (ssize != sizeof(sin6))
			return -1;

		memcpy(&sin6, ss, ssize);

		memcpy(addr, &f, FAS);

		/* this is stored in network order */
		memcpy(addr + FAS, &sin6.sin6_port, 2);

		sin6.sin6_flowinfo = uint32_t_swap_bytes(sin6.sin6_flowinfo);
		memcpy(addr + FAS + 2, &sin6.sin6_flowinfo, 4);

		/* this is stored in network order */
		memcpy(addr + FAS + 2 + 4, &sin6.sin6_addr, 16);

		sin6.sin6_scope_id = uint32_t_swap_bytes(sin6.sin6_scope_id);
		memcpy(addr + FAS + 2 + 4 + 16, &sin6.sin6_scope_id, 4);

		break;
	}
	default: {
		/* TODO: add more support */
		return -1;
		break;
	}
	}

	newlen = uint32_t_swap_bytes(newlen);
	memcpy(len, &newlen, sizeof(newlen));

	return 0;
}

#endif

#ifdef SAME_SOCKADDR

static long finish_bindlike(struct FuncInst *funcinst,
			    long (*bindlike)(int, const struct sockaddr *, socklen_t),
			    int fd, uint32_t addr, uint32_t len)
{
	char *base;

	/* no need to sanitize address, host kernel will do that for us */
	if (!_wasmjit_emscripten_check_range(funcinst, addr, len))
		return -EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	return bindlike(fd, (void *)(base + addr), len);
}

#else

/* need to byte swap and adapt sockaddr to current platform */
static long finish_bindlike(struct FuncInst *funcinst,
			    long (*bindlike)(int, const struct sockaddr *, socklen_t),
			    int fd, uint32_t uaddr, uint32_t len)
{
	struct sockaddr_storage ss;
	size_t ptr_size;

	if (!_wasmjit_emscripten_check_range(funcinst, uaddr,
					     len))
		return -EFAULT;

	if (read_sockaddr(funcinst, &ss, &ptr_size, uaddr, len))
		return -EINVAL;

	return bindlike(fd, (void *) &ss, ptr_size);
}

#endif

#if __INT_WIDTH__ == 32 && defined(SAME_SOCKADDR)

static long finish_acceptlike(long (*acceptlike)(int, struct sockaddr *, socklen_t *),
			      int fd, void *addr, uint32_t addrlen, void *len)
{
	/* socklen_t == uint32_t */
	(void) addrlen;
	return acceptlike(fd, addr, len);
}

#else

/* need to byte swap and adapt sockaddr to current platform */
static long finish_acceptlike(long (*acceptlike)(int, struct sockaddr *, socklen_t *),
			      int fd, char *addr, uint32_t addrlen, void *len)
{
	long rret;
	struct sockaddr_storage ss;
	socklen_t ssize = sizeof(ss);

	rret = acceptlike(fd, (void *) &ss, &ssize);
	if (rret < 0)
		return rret;

	if (write_sockaddr((struct sockaddr *) &ss, ssize, addr, addrlen, len)) {
		/* NB: we have to abort here because we can't undo the sys_accept() */
		wasmjit_emscripten_internal_abort("Failed to convert sockaddr");
	}

	return rret;
}

#endif

#define EM_MSG_OOB 1
#define EM_MSG_PEEK 2
#define EM_MSG_DONTROUTE 4
#define EM_MSG_CTRUNC 8
#define EM_MSG_TRUNC 32
#define EM_MSG_DONTWAIT 64
#define EM_MSG_EOR 128
#define EM_MSG_WAITALL 256
#define EM_MSG_CONFIRM 2048
#define EM_MSG_ERRQUEUE 8192
#define EM_MSG_NOSIGNAL 16384
#define EM_MSG_MORE 32768
#define EM_MSG_CMSG_CLOEXEC 1073741824

#if defined(__linux__) || defined(__KERNEL__)

static int convert_sendto_flags(int32_t flags)
{
	return flags;
}

static int has_bad_sendto_flag(int32_t flags)
{
	(void) flags;
	return 0;
}

static int convert_recvfrom_flags(int32_t flags)
{
	return flags;
}

static int has_bad_recvfrom_flag(int32_t flags)
{
	(void) flags;
	return 0;
}

static int32_t convert_recvmsg_msg_flags(int flags) {
	return flags;
}

#else

enum {
	ALLOWED_SENDTO_FLAGS =
#ifdef MSG_CONFIM
	EM_MSG_CONFIRM |
#endif
#ifdef MSG_DONTROUTE
	EM_MSG_DONTROUTE |
#endif
#ifdef MSG_DONTWAIT
	EM_MSG_DONTWAIT |
#endif
#ifdef MSG_EOR
	EM_MSG_EOR |
#endif
#ifdef MSG_MORE
	EM_MSG_MORE |
#endif
#ifdef MSG_NOSIGNAL
	EM_MSG_NOSIGNAL |
#endif
#ifdef MSG_OOB
	EM_MSG_OOB |
#endif
	0,
};

static int convert_sendto_flags(int32_t flags)
{
	int oflags = 0;

#define SETF(n)					\
	if (flags & EM_MSG_ ## n) {		\
		oflags |= MSG_ ## n;		\
	}

#ifdef MSG_CONFIGM
	SETF(CONFIRM);
#endif
	SETF(DONTROUTE);
	SETF(DONTWAIT);
	SETF(EOR);
#ifdef MSG_MORE
	SETF(MORE);
#endif
#ifdef MSG_NOSIGNAL
	SETF(NOSIGNAL);
#endif
	SETF(OOB);

#undef SETF

	return oflags;
}

static int has_bad_sendto_flag(int32_t flags)
{
	return flags & ~(int32_t) ALLOWED_SENDTO_FLAGS;
}

enum {
	ALLOWED_RECVFROM_FLAGS =
#ifdef MSG_CMSG_CLOEXEC
	EM_MSG_CMSG_CLOEXEC |
#endif
#ifdef MSG_DONTWAIT
	EM_MSG_DONTWAIT |
#endif
#ifdef MSG_ERRQUEUE
	EM_MSG_ERRQUEUE |
#endif
#ifdef MSG_OOB
	EM_MSG_OOB |
#endif
#ifdef MSG_PEEK
	EM_MSG_PEEK |
#endif
#ifdef MSG_TRUNC
	EM_MSG_TRUNC |
#endif
#ifdef MSG_WAITALL
	EM_MSG_WAITALL |
#endif
	0,
};

static int convert_recvfrom_flags(int32_t flags)
{
	int oflags = 0;

#define SETF(n)					\
	if (flags & EM_MSG_ ## n) {		\
		oflags |= MSG_ ## n;		\
	}

#ifdef MSG_CMSG_CLOEXEC
	SETF(CMSG_CLOEXEC);
#endif
	SETF(DONTWAIT);
#ifdef MSG_ERRQUEUE
	SETF(ERRQUEUE);
#endif
	SETF(OOB);
	SETF(PEEK);
	SETF(TRUNC);
	SETF(WAITALL);

#undef SETF

	return oflags;
}

static int has_bad_recvfrom_flag(int32_t flags)
{
	return flags & ~(int32_t) ALLOWED_RECVFROM_FLAGS;
}

static int32_t convert_recvmsg_msg_flags(int flags) {
	int32_t oflags = 0;
#define SETF(n)						\
	if (flags & MSG_ ## n) {			\
		oflags |= EM_MSG_ ## n;		\
	}
	SETF(EOR);
	SETF(TRUNC);
	SETF(CTRUNC);
	SETF(OOB);
#ifdef MSG_ERRQUEUE
	SETF(ERRQUEUE);
#endif
	return oflags;
}

#endif

#ifdef SAME_SOCKADDR

static long finish_sendto(struct FuncInst *funcinst,
			  int32_t fd,
			  const void *buf, uint32_t len,
			  int flags,
			  uint32_t dest_addr, uint32_t addrlen)
{
	void *real_dest_addr;

	if (dest_addr) {
		char *base;
		/* no need to sanitize address after jmp,
		   host kernel will do that for us */
		if (!_wasmjit_emscripten_check_range(funcinst,
						     dest_addr,
						     addrlen))
			return -EFAULT;
		base = wasmjit_emscripten_get_base_address(funcinst);
		real_dest_addr = base + dest_addr;
	} else {
		real_dest_addr = NULL;
	}

	return sys_sendto(fd, buf, len, flags, real_dest_addr, addrlen);
}

#else

static long finish_sendto(struct FuncInst *funcinst,
			  int32_t fd,
			  const void *buf, uint32_t len,
			  int flags2,
			  uint32_t dest_addr, uint32_t addrlen)
{

	struct sockaddr_storage ss;
	size_t ptr_size;
	void *saddr;

	if (dest_addr) {
		if (!_wasmjit_emscripten_check_range(funcinst,
						     dest_addr,
						     addrlen))
			return -EFAULT;
		/* convert dest_addr to form understood by sys_sendto */
		if (read_sockaddr(funcinst, &ss, &ptr_size, dest_addr, addrlen))
			return -EINVAL;
		saddr = &ss;
	} else {
		saddr = NULL;
		ptr_size = 0;
	}

	return sys_sendto(fd, buf, len, flags2, saddr, ptr_size);
}

#endif

#ifdef SAME_SOCKADDR

static long finish_recvfrom(int32_t fd,
			    void *buf, uint32_t len,
			    int flags,
			    void *dest_addr,
			    uint32_t addrlen,
			    void *addrlenp)
{
	(void)addrlen;
	return sys_recvfrom(fd, buf, len, flags, dest_addr, addrlenp);
}

#else

static long finish_recvfrom(int32_t fd,
			    void *buf, uint32_t len,
			    int flags2,
			    void *dest_addr,
			    uint32_t addrlen,
			    void *addrlenp)
{
	struct sockaddr_storage ss;
	socklen_t ssize = sizeof(ss);
	long rret;

	rret = sys_recvfrom(fd, buf, len, flags2, (void *) &ss, &ssize);
	if (rret < 0)
		return rret;

	if (dest_addr) {
		if (write_sockaddr((struct sockaddr *) &ss, ssize, dest_addr, addrlen, addrlenp)) {
			/* NB: we have to abort here because we can't undo the sys_accept() */
			wasmjit_emscripten_internal_abort("Failed to convert sockaddr");
		}
	}

	return rret;
}

#endif

struct em_linger {
	int32_t l_onoff, l_linger;
};

struct em_ucred {
	uint32_t pid, uid, gid;
};

struct em_timeval {
	uint32_t tv_sec, tv_usec;
};

struct em_timezone {
	int32_t tz_minuteswest;
	int32_t tz_dsttime;
};

#define EM_FD_SETSIZE 1024

typedef struct {
	uint32_t fds_bits[EM_FD_SETSIZE / 8 / sizeof(uint32_t)];
} em_fd_set;

#define EM_FD_ZERO(s) do { size_t __i; uint32_t *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (uint32_t); __i; __i--) *__b++=0; } while(0)
#define EM_FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(uint32_t))] |= (1UL<<((d)%(8*sizeof(uint32_t)))))
#define EM_FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(uint32_t))] &= ~(1UL<<((d)%(8*sizeof(uint32_t)))))
#define EM_FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(uint32_t))] & (1UL<<((d)%(8*sizeof(uint32_t)))))

struct em_pollfd {
	int32_t fd;
	int16_t events;
	int16_t revents;
};

struct em_rlimit {
	uint64_t rlim_cur;
	uint64_t rlim_max;
};

typedef uint32_t em_dev_t;
typedef uint32_t em_mode_t;
typedef uint32_t em_nlink_t;
typedef uint32_t em_uid_t;
typedef uint32_t em_gid_t;
typedef uint32_t em_rdev_t;
typedef int32_t em_off_t;
typedef uint32_t em_blksize_t;
typedef uint32_t em_blkcnt_t;
typedef uint32_t em_ino_t;
typedef uint32_t em_time_t;
typedef uint16_t em_unsigned_short;
typedef uint8_t em_unsigned_char;
typedef uint32_t em_unsigned_long;
typedef uint32_t em_fsblkcnt_t;
typedef uint32_t em_fsfilcnt_t;
typedef int32_t em_socklen_t;

struct em_timespec {
	em_time_t tv_sec;
	em_long tv_nsec;
};

struct em_stat64 {
	em_dev_t st_dev;
	em_int __st_dev_padding;
	em_long __st_ino_truncated;
	em_mode_t st_mode;
	em_nlink_t st_nlink;
	em_uid_t st_uid;
	em_gid_t st_gid;
	em_dev_t st_rdev;
	em_int __st_rdev_padding;
	em_off_t st_size;
	em_blksize_t st_blksize;
	em_blkcnt_t st_blocks;
	struct em_timespec st_atim;
	struct em_timespec st_mtim;
	struct em_timespec st_ctim;
	em_ino_t st_ino;
};

struct em_linux_dirent64 {
	em_ino_t d_ino;
	em_off_t d_off;
	em_unsigned_short d_reclen;
	em_unsigned_char d_type;
	char d_name[1];
};

typedef struct {
	em_int __val[2];
} em_fsid_t;

struct em_statfs64 {
	em_unsigned_long f_type, f_bsize;
	em_fsblkcnt_t f_blocks, f_bfree, f_bavail;
	em_fsfilcnt_t f_files, f_ffree;
	em_fsid_t f_fsid;
	em_unsigned_long f_namelen, f_frsize, f_flags, f_spare[4];
};

struct em_tm {
	em_int tm_sec;
	em_int tm_min;
	em_int tm_hour;
	em_int tm_mday;
	em_int tm_mon;
	em_int tm_year;
	em_int tm_wday;
	em_int tm_yday;
	em_int tm_isdst;
	em_long tm_gmtoff;
	uint32_t tm_zone;
};

struct em_itimerval {
	struct em_timeval it_interval;
	struct em_timeval it_value;
};

struct linux_ucred {
	uint32_t pid;
	uint32_t uid;
	uint32_t gid;
};

COMPILE_TIME_ASSERT(sizeof(socklen_t) == sizeof(unsigned));

#define EM_SOL_SOCKET 1
#define EM_SCM_RIGHTS 1
#define EM_SCM_CREDENTIALS 2

enum {
	OPT_TYPE_INT,
	OPT_TYPE_LINGER,
	OPT_TYPE_UCRED,
	OPT_TYPE_TIMEVAL,
	OPT_TYPE_STRING,
};

#define BINPOW(n) (1ULL << (n))

#define UINT_MAX_N(n) (BINPOW((n) * 8) - 1)
/* NB: assumes two's complement */
#define SINT_MIN_N(n) ((long long) (0 - BINPOW((n) * 8 - 1)))
#define SINT_MAX_N(n) ((long long) (BINPOW((n) * 8 - 1) - 1))

static int convert_sockopt(int32_t level,
			   int32_t optname,
			   int *level2,
			   int *optname2,
			   int *opttype)
{
	switch (level) {
	case EM_SOL_SOCKET: {
		switch (optname) {
#define SO(name, value, opt_type) case value: *optname2 = SO_ ## name; *opttype = OPT_TYPE_ ## opt_type; break;
#include <wasmjit/emscripten_runtime_sys_so_def.h>
#undef SO
		default: return -1;
		}
		*level2 = SOL_SOCKET;
		break;
	}
	default: return -1;
	}
	return 0;
}

static size_t read_timeval(struct FuncInst *funcinst,
			   struct timeval *tv,
			   uint32_t emtv)
{
	struct em_timeval wasm_timeval_optval;

	_wasmjit_memcpy_from_user(funcinst, &wasm_timeval_optval,
				  emtv, sizeof(struct em_timeval));
	wasm_timeval_optval.tv_sec =
		uint32_t_swap_bytes(wasm_timeval_optval.tv_sec);
	wasm_timeval_optval.tv_usec =
		uint32_t_swap_bytes(wasm_timeval_optval.tv_usec);

	if ((4 > sizeof(tv->tv_sec) &&
	     (wasm_timeval_optval.tv_sec > SINT_MAX_N(sizeof(tv->tv_sec)) ||
	      wasm_timeval_optval.tv_sec < SINT_MIN_N(sizeof(tv->tv_sec)))) ||
	    (4 > sizeof(tv->tv_usec) &&
	     (wasm_timeval_optval.tv_usec > SINT_MAX_N(sizeof(tv->tv_usec)) ||
	      wasm_timeval_optval.tv_usec < SINT_MIN_N(sizeof(tv->tv_usec)))))
		return 0;

	tv->tv_sec = wasm_timeval_optval.tv_sec;
	tv->tv_usec = wasm_timeval_optval.tv_usec;
	return sizeof(wasm_timeval_optval);
}

static size_t write_timeval(char *emtv,
			    const struct timeval *tv)
{
	struct em_timeval v;

	if ((sizeof(tv->tv_sec) > 4 &&
	     (tv->tv_sec > INT32_MAX ||
	      tv->tv_sec < INT32_MIN)) ||
	    (sizeof(tv->tv_usec) > 4 &&
	     (tv->tv_usec > INT32_MAX ||
	      tv->tv_usec < INT32_MIN)))
		return 0;

	v.tv_sec = int32_t_swap_bytes(tv->tv_sec);
	v.tv_usec = int32_t_swap_bytes(tv->tv_usec);
	memcpy(emtv, &v, sizeof(v));
	return sizeof(v);
}

static long finish_setsockopt(struct FuncInst *funcinst,
			      int32_t fd,
			      int32_t level,
			      int32_t optname,
			      uint32_t optval,
			      uint32_t optlen)
{
	int level2, optname2, opttype;
	union {
		int int_;
		struct linger linger;
		/* NB: this is a linux-only struct and it's defined using
		   constant bit-widths */
		struct linux_ucred ucred;
		struct timeval timeval;
	} real_optval;
	void *real_optval_p;
	socklen_t real_optlen;

	if (convert_sockopt(level, optname, &level2, &optname2, &opttype))
		return -EINVAL;

	switch (opttype) {
	case OPT_TYPE_INT: {
		int32_t wasm_int_optval;
		if (optlen != sizeof(wasm_int_optval))
			return -EINVAL;
		_wasmjit_memcpy_from_user(funcinst, &wasm_int_optval,
					  optval, sizeof(wasm_int_optval));
		wasm_int_optval = int32_t_swap_bytes(wasm_int_optval);
		real_optval.int_ = wasm_int_optval;
		real_optval_p = &real_optval.int_;
		real_optlen = sizeof(real_optval.int_);
		break;
	}
	case OPT_TYPE_LINGER: {
		struct em_linger wasm_linger_optval;
		if (optlen != sizeof(struct em_linger))
			return -EINVAL;
		_wasmjit_memcpy_from_user(funcinst, &wasm_linger_optval,
					  optval, sizeof(struct em_linger));
		wasm_linger_optval.l_onoff =
			int32_t_swap_bytes(wasm_linger_optval.l_onoff);
		wasm_linger_optval.l_linger =
			int32_t_swap_bytes(wasm_linger_optval.l_linger);
		real_optval.linger.l_onoff = wasm_linger_optval.l_onoff;
		real_optval.linger.l_linger = wasm_linger_optval.l_linger;
		real_optval_p = &real_optval.linger;
		real_optlen = sizeof(real_optval.linger);
		break;
	}
	case OPT_TYPE_UCRED: {
		struct em_ucred wasm_ucred_optval;
		if (optlen != sizeof(struct em_ucred))
			return -EINVAL;
		_wasmjit_memcpy_from_user(funcinst, &wasm_ucred_optval,
					  optval, sizeof(struct em_ucred));
		real_optval.ucred.pid = uint32_t_swap_bytes(wasm_ucred_optval.pid);
		real_optval.ucred.uid = uint32_t_swap_bytes(wasm_ucred_optval.uid);
		real_optval.ucred.gid = uint32_t_swap_bytes(wasm_ucred_optval.gid);
		real_optval_p = &real_optval.ucred;
		real_optlen = sizeof(real_optval.ucred);
		break;
	}
	case OPT_TYPE_TIMEVAL: {
		if (optlen != sizeof(struct em_timeval))
			return -EINVAL;
		if (!read_timeval(funcinst, &real_optval.timeval, optval))
			return -EINVAL;
		real_optval_p = &real_optval.timeval;
		real_optlen = sizeof(real_optval.timeval);
		break;
	}
	case OPT_TYPE_STRING: {
		char *base;
		base = wasmjit_emscripten_get_base_address(funcinst);
		real_optval_p = base + optval;
		assert(sizeof(real_optlen) >= sizeof(optlen));
		real_optlen = optlen;
		break;
	}
	default: assert(0); __builtin_unreachable();
	}

	return sys_setsockopt(fd, level2, optname2, real_optval_p, real_optlen);
}

static long finish_getsockopt(int32_t fd,
			      int32_t level,
			      int32_t optname,
			      char *optval,
			      uint32_t optlen,
			      char *optlenp)
{
	int level2, optname2, opttype;
	union {
		int int_;
		struct linger linger;
		/* NB: this is a linux-only struct and it's defined using
		   constant bit-widths */
		struct linux_ucred ucred;
		struct timeval timeval;
	} real_optval;
	void *real_optval_p;
	socklen_t real_optlen;
	long ret;
	uint32_t newlen;

	if (convert_sockopt(level, optname, &level2, &optname2, &opttype))
		return -EINVAL;

	switch (opttype) {
	case OPT_TYPE_INT: {
		if (optlen < sizeof(int32_t))
			return -EINVAL;
		real_optval_p = &real_optval.int_;
		real_optlen = sizeof(real_optval.int_);
		break;
	}
	case OPT_TYPE_LINGER: {
		if (optlen < sizeof(struct em_linger))
			return -EINVAL;
		real_optval_p = &real_optval.linger;
		real_optlen = sizeof(real_optval.linger);
		break;
	}
	case OPT_TYPE_UCRED: {
		if (optlen < sizeof(struct em_ucred))
			return -EINVAL;
		real_optval_p = &real_optval.ucred;
		real_optlen = sizeof(real_optval.ucred);
		break;
	}
	case OPT_TYPE_TIMEVAL: {
		if (optlen < sizeof(struct em_timeval))
			return -EINVAL;
		real_optval_p = &real_optval.timeval;
		real_optlen = sizeof(real_optval.timeval);
		break;
	}
	case OPT_TYPE_STRING: {
		real_optval_p = optval;
		real_optlen = optlen;
		break;
	}
	default: assert(0); __builtin_unreachable();
	}

	ret = sys_getsockopt(fd, level2, optname2, real_optval_p, &real_optlen);
	if (ret < 0)
		return ret;

	switch (opttype) {
	case OPT_TYPE_INT: {
		int32_t v;
#if __INT_WIDTH__ > 32
		if (real_optval.int_ > INT32_MAX ||
		    real_optval.int_ < INT32_MIN)
			wasmjit_emscripten_internal_abort("Failed to convert sockopt");
#endif
		v = int32_t_swap_bytes((int32_t) real_optval.int_);
		memcpy(optval, &v, sizeof(v));
		newlen = sizeof(v);
		break;
	}
	case OPT_TYPE_LINGER: {
		struct em_linger v;
#if __INT_WIDTH__ > 32
		if (real_optval.linger.l_onoff > INT32_MAX ||
		    real_optval.linger.l_onoff < INT32_MIN ||
		    real_optval.linger.l_linger > INT32_MAX ||
		    real_optval.linger.l_linger < INT32_MIN)
			wasmjit_emscripten_internal_abort("Failed to convert sockopt");
#endif
		v.l_onoff = int32_t_swap_bytes((int32_t) real_optval.linger.l_onoff);
		v.l_linger = int32_t_swap_bytes((int32_t) real_optval.linger.l_linger);
		memcpy(optval, &v, sizeof(v));
		newlen = sizeof(v);
		break;
	}
	case OPT_TYPE_UCRED: {
		struct em_ucred v;
		v.pid = uint32_t_swap_bytes(real_optval.ucred.pid);
		v.uid = uint32_t_swap_bytes(real_optval.ucred.uid);
		v.gid = uint32_t_swap_bytes(real_optval.ucred.gid);
		memcpy(optval, &v, sizeof(v));
		newlen = sizeof(v);
		break;
	}
	case OPT_TYPE_TIMEVAL: {
		newlen = write_timeval(optval, &real_optval.timeval);
		if (!newlen)
			wasmjit_emscripten_internal_abort("Failed to convert sockopt");
		break;
	}
	case OPT_TYPE_STRING: {
#if __INT_WIDTH__ > 32
		if (real_optlen > UINT32_MAX)
			wasmjit_emscripten_internal_abort("Failed to convert sockopt");
#endif
		newlen = real_optlen;
		break;
	}
	default: assert(0); __builtin_unreachable();
	}

	newlen = uint32_t_swap_bytes(newlen);
	memcpy(optlenp, &newlen, sizeof(newlen));

	return 0;
}

struct em_cmsghdr {
	uint32_t cmsg_len;
	uint32_t cmsg_level;
	uint32_t cmsg_type;
};

struct em_msghdr {
	uint32_t msg_name;
	uint32_t msg_namelen;
	uint32_t msg_iov;
	uint32_t msg_iovlen;
	uint32_t msg_control;
	uint32_t msg_controllen;
	uint32_t msg_flags;
};

#define EM_CMSG_ALIGN(len) (((len) + sizeof (uint32_t) - 1)		\
			     & (uint32_t) ~(sizeof (uint32_t) - 1))

/* NB: cmsg_len can be size_t or socklen_t depending on host kernel */
typedef typeof(((struct cmsghdr *)0)->cmsg_len) cmsg_len_t;

static long copy_cmsg(struct FuncInst *funcinst,
		      uint32_t control,
		      uint32_t controllen,
		      user_msghdr_t *msg)
{
	char *base;
	uint32_t controlptr;
	uint32_t controlmax;
	cmsg_len_t buf_offset;
	struct cmsghdr *cmsg;

	base = wasmjit_emscripten_get_base_address(funcinst);

	/* control and controllen are user-controlled,
	   check for overflow */
	if (__builtin_add_overflow(control, controllen, &controlmax))
		return -EFAULT;

	if (controlmax < EM_CMSG_ALIGN(sizeof(struct em_cmsghdr)))
		return -EINVAL;

	/* count up required space */
	buf_offset = 0;
	controlptr = control;
	while (!(controlptr > controlmax - EM_CMSG_ALIGN(sizeof(struct em_cmsghdr)))) {
		struct em_cmsghdr user_cmsghdr;
		size_t cur_len, buf_len;

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &user_cmsghdr,
						       controlptr,
						       sizeof(user_cmsghdr)))
			return -EFAULT;

		user_cmsghdr.cmsg_len = uint32_t_swap_bytes(user_cmsghdr.cmsg_len);
		user_cmsghdr.cmsg_level = uint32_t_swap_bytes(user_cmsghdr.cmsg_level);
		user_cmsghdr.cmsg_type = uint32_t_swap_bytes(user_cmsghdr.cmsg_type);

		/* kernel says must check this */
		{
			uint32_t sum;
			/* controlptr and cmsg_len are user-controlled,
			   check for overflow */
			if (EM_CMSG_ALIGN(user_cmsghdr.cmsg_len) < user_cmsghdr.cmsg_len)
				return -EFAULT;

			if (__builtin_add_overflow(EM_CMSG_ALIGN(user_cmsghdr.cmsg_len),
						   controlptr,
						   &sum))
				return -EFAULT;

			if (sum > controlmax)
				break;
		}

		/* check if control data is in range */
		if (!_wasmjit_emscripten_check_range(funcinst,
						     controlptr,
						     EM_CMSG_ALIGN(user_cmsghdr.cmsg_len)))
			return -EFAULT;

		if (user_cmsghdr.cmsg_len < EM_CMSG_ALIGN(sizeof(struct em_cmsghdr)))
			return -EFAULT;
		buf_len = user_cmsghdr.cmsg_len - EM_CMSG_ALIGN(sizeof(struct em_cmsghdr));

		switch (user_cmsghdr.cmsg_level) {
		case EM_SOL_SOCKET: {
			switch (user_cmsghdr.cmsg_type) {
			case EM_SCM_RIGHTS:
				/* convert int size from wasm to host */
				if (buf_len % sizeof(int32_t))
					return -EINVAL;
				cur_len = (buf_len / sizeof(int32_t)) * sizeof(int);
				break;
			case EM_SCM_CREDENTIALS:
#ifdef SCM_CREDENTIALS
				/* passes a struct ucred which is the same across
				   all archs */
				if (buf_len != sizeof(struct linux_ucred))
					return -EINVAL;
				cur_len = buf_len;
				break;
#else
				/* TODO: convert to host's version of SCM_CREDENTIALS */
				return -EFAULT;
#endif
			default:
				return -EFAULT;
			}
			break;
		}
		default:
			return -EFAULT;
		}

		/* it's not exactly clear that this can't overflow at
		   this point given the varying conditions in which we
		   operate (e.g. sizeof(cmsg_len_t) depends on
		   architecture and host kernel)
		 */
		if (__builtin_add_overflow(buf_offset, CMSG_SPACE(cur_len),
					   &buf_offset))
			return -EFAULT;
		/* the safety of this was checked above */
		controlptr += EM_CMSG_ALIGN(user_cmsghdr.cmsg_len);
	}

	msg->msg_control = calloc(buf_offset, 1);
	if (!msg->msg_control)
		return -ENOMEM;

	msg->msg_controllen = buf_offset;

	cmsg = CMSG_FIRSTHDR(msg);

	/* now convert each control message */
	controlptr = control;
	while (!(controlptr > controlmax - EM_CMSG_ALIGN(sizeof(struct em_cmsghdr)))) {
		struct em_cmsghdr user_cmsghdr;
		size_t buf_len, new_len;
		unsigned char *dest_buf_base;
		uint32_t cur_len;

		assert(cmsg);

		_wasmjit_memcpy_from_user(funcinst, &user_cmsghdr,
					  controlptr, sizeof(struct em_cmsghdr));
		user_cmsghdr.cmsg_len = uint32_t_swap_bytes(user_cmsghdr.cmsg_len);
		user_cmsghdr.cmsg_level = uint32_t_swap_bytes(user_cmsghdr.cmsg_level);
		user_cmsghdr.cmsg_type = uint32_t_swap_bytes(user_cmsghdr.cmsg_type);

		/* kernel says must check this */
		if (EM_CMSG_ALIGN(user_cmsghdr.cmsg_len) + controlptr > controlmax)
			break;

		cur_len = EM_CMSG_ALIGN(sizeof(struct em_cmsghdr));
		buf_len = user_cmsghdr.cmsg_len - cur_len;

		cur_len += controlptr;

		/* kernel sources differ from libc sources on where
		   the buffer starts, but in any case the correct code
		   is the aligned offset from the beginning,
		   i.e. what (struct cmsghdr *)a + 1 means */
		dest_buf_base = CMSG_DATA(cmsg);

		switch (user_cmsghdr.cmsg_level) {
		case EM_SOL_SOCKET: {
			switch (user_cmsghdr.cmsg_type) {
			case EM_SCM_RIGHTS: {
				size_t i;
				for (i = 0; i < buf_len / sizeof(int32_t); ++i) {
					int32_t fd;
					int destfd;
					_wasmjit_memcpy_from_user(funcinst,
								  &fd,
								  cur_len + i * sizeof(int32_t),
								  sizeof(int32_t));
					fd = int32_t_swap_bytes(fd);
					destfd = fd;
					memcpy(dest_buf_base + i * sizeof(int),
					       &destfd, sizeof(int));
				}

				new_len = (buf_len / sizeof(int32_t)) * sizeof(int);
				cmsg->cmsg_type = SCM_RIGHTS;
				break;
				}
#ifdef SCM_CREDENTIALS
			case EM_SCM_CREDENTIALS: {
				/* struct ucred is same across all archs,
				   just flip bytes if necessary
				 */
				size_t i;
				for (i = 0; i < 3; ++i) {
					uint32_t tmp;
					_wasmjit_memcpy_from_user(funcinst,
								  &tmp,
								  cur_len + i * 4,
								  sizeof(tmp));
					tmp = uint32_t_swap_bytes(tmp);
					memcpy(dest_buf_base + i * 4, &tmp, sizeof(tmp));
				}

				new_len = buf_len;
				cmsg->cmsg_type = SCM_CREDENTIALS;
				break;
			}
#endif
			default:
				assert(0);
				__builtin_unreachable();
				break;

			}
			cmsg->cmsg_level = SOL_SOCKET;
			break;
		}
		default:
			assert(0);
			__builtin_unreachable();
			break;
		}

		cmsg->cmsg_len = CMSG_LEN(new_len);

		cmsg = SYS_CMSG_NXTHDR(msg, cmsg);
		controlptr += EM_CMSG_ALIGN(user_cmsghdr.cmsg_len);
	}

	return 0;
}

static long write_cmsg(char *base,
		       struct em_msghdr *emmsg,
		       user_msghdr_t *msg)
{
	/* copy data back to emmsg.control */
	struct cmsghdr *cmsg;
	uint32_t controlptr, controlmax;

	/* the validity of the control range
	   should have been checked before here */
	controlmax = emmsg->msg_control + emmsg->msg_controllen;

	controlptr = emmsg->msg_control;
	for (cmsg = CMSG_FIRSTHDR(msg);
	     cmsg;
	     cmsg = SYS_CMSG_NXTHDR(msg, cmsg)) {
		struct em_cmsghdr user_cmsghdr;
		unsigned char *src_buf_base;
		char *dest_buf_base;
		cmsg_len_t buf_len, new_len;
		uint32_t controlptrextent;

		/* kernel is malfunctioning if this is not true */
		assert(CMSG_LEN(0) <= cmsg->cmsg_len);
		buf_len = cmsg->cmsg_len - CMSG_LEN(0);

		/* compute required len */
		switch (cmsg->cmsg_level) {
		case SOL_SOCKET:
			switch (cmsg->cmsg_type) {
			case SCM_RIGHTS: {
				/* kernel is malfunctioning if this is not true */
				assert(!(buf_len % sizeof(int)));
				new_len = (buf_len / sizeof(int)) * sizeof(int32_t);
				break;
			}
#ifdef SCM_CREDENTIALS
			case SCM_CREDENTIALS: {
				/* kernel is malfunctioning if this is not true */
				assert(buf_len != sizeof(struct linux_ucred));
				new_len = buf_len;
				break;
			}
#endif
			default:
				return -1;
			}
			break;
		default:
			return -1;
		}

		new_len += EM_CMSG_ALIGN(sizeof(struct em_cmsghdr));

		user_cmsghdr.cmsg_len = new_len;

		/* this is always true because EM_CMSG_ALIGN aligns
		   to int32_t, and new_len is always a multiple of int32_t,
		   this assert allows us to avoid checking if aligning overflows
		*/
		assert(EM_CMSG_ALIGN(new_len) == new_len);

		/* we got more cmsgs than we can fit,
		   flag EM_MSG_CTRUNC and give up */
		if (__builtin_add_overflow(controlptr,
					   EM_CMSG_ALIGN(new_len),
					   &controlptrextent) ||
		    controlptrextent > controlmax) {
			emmsg->msg_flags |= EM_MSG_CTRUNC;
			break;
		}

		src_buf_base = CMSG_DATA(cmsg);
		dest_buf_base = base + controlptr + EM_CMSG_ALIGN(sizeof(struct em_cmsghdr));

		switch (cmsg->cmsg_level) {
		case SOL_SOCKET:
			switch (cmsg->cmsg_type) {
			case SCM_RIGHTS: {
				size_t i;

				for (i = 0; i < buf_len / sizeof(int); ++i) {
					int srcfd;
					int32_t destfd;
					memcpy(&srcfd,
					       src_buf_base + i * sizeof(int),
					       sizeof(int));

#if __INT_WIDTH__ > 32
					if (srcfd > INT32_MAX)
						return -1;
#endif

					destfd = srcfd;
					destfd = int32_t_swap_bytes(destfd);

					memcpy(dest_buf_base + i * sizeof(int32_t),
					       &destfd,
					       sizeof(int32_t));
				}

				user_cmsghdr.cmsg_type = EM_SCM_RIGHTS;
				break;
			}
#ifdef SCM_CREDENTIALS
			case SCM_CREDENTIALS: {
				size_t i;

				for (i = 0; i < 3; ++i) {
					uint32_t tmp;
					memcpy(&tmp, src_buf_base + i * 4,
					       sizeof(tmp));
					tmp = uint32_t_swap_bytes(tmp);
					memcpy(dest_buf_base + i * 4, &tmp,
					       sizeof(tmp));
				}

				user_cmsghdr.cmsg_type = EM_SCM_CREDENTIALS;
				break;
			}
#endif
			default:
				assert(0);
				__builtin_unreachable();
			}
			user_cmsghdr.cmsg_level = EM_SOL_SOCKET;
			break;
		default:
			assert(0);
			__builtin_unreachable();
		}

		user_cmsghdr.cmsg_len = uint32_t_swap_bytes(user_cmsghdr.cmsg_len);
		user_cmsghdr.cmsg_level = uint32_t_swap_bytes(user_cmsghdr.cmsg_level);
		user_cmsghdr.cmsg_type = uint32_t_swap_bytes(user_cmsghdr.cmsg_type);
		memcpy(base + controlptr, &user_cmsghdr,
		       sizeof(user_cmsghdr));

		controlptr = controlptrextent;
	}
	emmsg->msg_controllen = controlptr - emmsg->msg_control;
	return 0;
}

#ifdef SAME_SOCKADDR

static long finish_sendmsg(struct FuncInst *funcinst,
			   int fd, user_msghdr_t *msg, int flags)
{
	uint32_t msg_name = (uintptr_t) msg->msg_name;

	/* host kernel will take care of sanitizing msg_name */
	if (!_wasmjit_emscripten_check_range(funcinst,
					     msg_name,
					     msg->msg_namelen)) {
		return -EFAULT;
	}

	msg->msg_name = wasmjit_emscripten_get_base_address(funcinst) + msg_name;

	return sys_sendmsg(fd, msg, flags);
}

#else

static long finish_sendmsg(struct FuncInst *funcinst,
			   int fd, user_msghdr_t *msg, int flags)
{
	struct sockaddr_storage ss;
	size_t ptr_size;

	/* convert msg_name to form understood by sys_sendmsg */
	if (msg->msg_name) {
		uint32_t msg_name = (uintptr_t) msg->msg_name;

		/* host kernel will take care of sanitizing msg_name */
		if (!_wasmjit_emscripten_check_range(funcinst,
						     msg_name,
						     msg->msg_namelen)) {
			return -EFAULT;
		}

		if (read_sockaddr(funcinst, &ss, &ptr_size, msg_name, msg->msg_namelen))
			return -EINVAL;
		msg->msg_name = (void *)&ss;
		msg->msg_namelen = ptr_size;
	}

	return sys_sendmsg(fd, msg, flags);
}

#endif

#ifdef SAME_SOCKADDR

static long finish_recvmsg(int fd, user_msghdr_t *msg, int flags)
{
	return sys_recvmsg(fd, msg, flags);
}

#else

static long finish_recvmsg(int fd, user_msghdr_t *msg, int flags)
{

	struct sockaddr_storage ss;
	char *wasm_msg_name;
	uint32_t wasm_msg_namelen;
	long ret;

	/* convert msg_name to form understood by sys_recvmsg */
	if (msg->msg_name) {
		wasm_msg_name = msg->msg_name;
		wasm_msg_namelen = msg->msg_namelen;

		msg->msg_name = &ss;
		msg->msg_namelen = sizeof(ss);
	} else {
		wasm_msg_name = NULL;
		wasm_msg_namelen = 0;
	}

	ret = sys_recvmsg(fd, msg, flags);

	if (ret >= 0 && wasm_msg_name) {
		if (write_sockaddr(msg->msg_name, msg->msg_namelen,
				   wasm_msg_name, wasm_msg_namelen, &wasm_msg_namelen))
			/* NB: we have to abort here because we can't undo the sys_recvmsg() */
			wasmjit_emscripten_internal_abort("Failed to convert sockaddr");

		msg->msg_name = wasm_msg_name;
		msg->msg_namelen = wasm_msg_namelen;
	}

	return ret;
}

#endif

uint32_t wasmjit_emscripten____syscall102(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	long ret;
	uint32_t ivargs, icall;
	wasmjit_signal_block_ctx set;

	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, call,
		  uint32_t, varargs);

	(void) which;
	ivargs = args.varargs;
	icall = args.call;

	switch (icall) {
	case 1: { // socket
		int domain, type, protocol;

		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, domain,
			  int32_t, type,
			  int32_t, protocol);

		domain = convert_socket_domain_to_local(args.domain);
		type = convert_socket_type_to_local(args.type);
		protocol = convert_proto_to_local(domain, args.protocol);

		ret = sys_socket(domain, type, protocol);
		break;
	}
	case 2: // bind
	case 3: { // connect
		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, fd,
			  uint32_t, addrp,
			  uint32_t, addrlen);

		if (icall == 2) {
			ret = finish_bindlike(funcinst,
					      sys_bind,
					      args.fd,
					      args.addrp, args.addrlen);
		} else {
			assert(icall == 3);
			ret = finish_bindlike(funcinst,
					      sys_connect,
					      args.fd,
					      args.addrp, args.addrlen);
		}
		break;
	}
	case 4: { // listen
		LOAD_ARGS(funcinst, ivargs, 2,
			  int32_t, fd,
			  int32_t, backlog);

		ret = sys_listen(args.fd, args.backlog);
		break;
	}
	case 5: // accept
	case 6: // getsockname
	case 7: { // getpeername
		char *base;
		uint32_t addrlen;

		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, fd,
			  uint32_t, addrp,
			  uint32_t, addrlenp);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &addrlen,
						       args.addrlenp,
						       sizeof(addrlen)))
			return -EM_EFAULT;

		addrlen = uint32_t_swap_bytes(addrlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.addrp,
						     addrlen))
			return -EM_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		switch (icall) {
		case 5:
			ret = finish_acceptlike(sys_accept,
						args.fd, base + args.addrp,
						addrlen,
						base + args.addrlenp);
			break;
		case 6:
			ret = finish_acceptlike(sys_getsockname,
						args.fd, base + args.addrp,
						addrlen,
						base + args.addrlenp);
			break;
		case 7:
			ret = finish_acceptlike(sys_getpeername,
						args.fd, base + args.addrp,
						addrlen,
						base + args.addrlenp);
			break;
		default:
			assert(0);
			__builtin_unreachable();
		}

		break;
	}
	case 11: { // sendto
		char *base;
		int flags2;

		LOAD_ARGS(funcinst, ivargs, 6,
			  int32_t, fd,
			  uint32_t, message,
			  uint32_t, length,
			  int32_t, flags,
			  uint32_t, addrp,
			  uint32_t, addrlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.message,
						     args.length))
			return -EM_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		/* if there are flags we don't understand, then return invalid flag */
		if (has_bad_sendto_flag(args.flags))
			return -EM_EINVAL;

		flags2 = convert_sendto_flags(args.flags);

		ret = finish_sendto(funcinst,
				    args.fd,
				    base + args.message,
				    args.length,
				    flags2,
				    args.addrp,
				    args.addrlen);
		break;
	}
	case 12: { // recvfrom
		char *base;
		uint32_t addrlen;
		int flags2;

		LOAD_ARGS(funcinst, ivargs, 6,
			  int32_t, fd,
			  uint32_t, buf,
			  uint32_t, len,
			  uint32_t, flags,
			  uint32_t, addrp,
			  uint32_t, addrlenp);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &addrlen,
						       args.addrlenp,
						       sizeof(uint32_t)))
			return -EM_EFAULT;

		addrlen = uint32_t_swap_bytes(addrlen);

		if (args.addrp) {
			if (!_wasmjit_emscripten_check_range(funcinst,
							     args.addrp,
							     addrlen))
				return -EM_EFAULT;
		}

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.buf,
						     args.len))
			return -EM_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		/* if there are flags we don't understand, then return invalid flag */
		if (has_bad_recvfrom_flag(args.flags))
			return -EM_EINVAL;

		flags2 = convert_recvfrom_flags(args.flags);

		ret = finish_recvfrom(args.fd,
				      base + args.buf,
				      args.len,
				      flags2,
				      args.addrp ? base + args.addrp : NULL,
				      addrlen,
				      base + args.addrlenp);
		break;
	}
	case 14: { // setsockopt
		LOAD_ARGS(funcinst, ivargs, 5,
			  int32_t, fd,
			  int32_t, level,
			  int32_t, optname,
			  uint32_t, optval,
			  uint32_t, optlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.optval,
						     args.optlen))
			return -EM_EFAULT;

		ret = finish_setsockopt(funcinst,
					args.fd,
					args.level,
					args.optname,
					args.optval,
					args.optlen);
		break;
	}
	case 15: { // getsockopt
		char *base;
		uint32_t optlen;

		LOAD_ARGS(funcinst, ivargs, 5,
			  int32_t, fd,
			  int32_t, level,
			  int32_t, optname,
			  uint32_t, optval,
			  uint32_t, optlenp);

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &optlen,
						       args.optlenp,
						       sizeof(optlen)))
			return -EM_EFAULT;

		optlen = uint32_t_swap_bytes(optlen);

		if (!_wasmjit_emscripten_check_range(funcinst,
						     args.optval,
						     optlen))
			return -EM_EFAULT;

		base = wasmjit_emscripten_get_base_address(funcinst);

		ret = finish_getsockopt(args.fd,
					args.level,
					args.optname,
					base + args.optval,
					optlen,
					base + args.optlenp);
		break;
	}
	case 16: { // sendmsg
		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, fd,
			  uint32_t, msg,
			  int32_t, flags);

		if (has_bad_sendto_flag(args.flags))
			return -EM_EINVAL;

		{
			user_msghdr_t msg;
			size_t ptr_idx_1 = 0, ptr_idx_2 = 0;

			LOAD_ARGS_CUSTOM(emmsg, funcinst, args.msg, 7,
					 uint32_t, name,
					 uint32_t, namelen,
					 uint32_t, iov,
					 uint32_t, iovlen,
					 uint32_t, control,
					 uint32_t, controllen,
					 uint32_t, flags);

			msg.msg_iov = NULL;
			msg.msg_control = NULL;

			if (emmsg.name) {
				msg.msg_name = (void *) (uintptr_t) emmsg.name;
			} else {
				msg.msg_name = NULL;
			}

			msg.msg_namelen = emmsg.namelen;

			_wasmjit_block_signals(&set);

			ret = copy_iov(funcinst, emmsg.iov, emmsg.iovlen, &msg.msg_iov);
			if (ret) {
				goto error;
			}
			msg.msg_iovlen = emmsg.iovlen;

			ptr_idx_2 = _wasmjit_add_unfreed_pointer(funcinst, msg.msg_iov);
			if (!ptr_idx_2) {
				ret = -ENOMEM;
				goto error;
			}

			if (emmsg.control) {
				ret = copy_cmsg(funcinst, emmsg.control, emmsg.controllen,
						&msg);
				if (ret)
					goto error;

				ptr_idx_1 = _wasmjit_add_unfreed_pointer(funcinst, msg.msg_control);
				if (!ptr_idx_1) {
					ret = -ENOMEM;
					goto error;
				}
			} else {
				if (emmsg.controllen) {
					ret = -EINVAL;
					goto error;
				}
				msg.msg_control = NULL;
				msg.msg_controllen = 0;
			}

			_wasmjit_unblock_signals(&set);

			/* unused in sendmsg */
			msg.msg_flags = 0;

			ret = finish_sendmsg(funcinst, args.fd, &msg,
					     convert_sendto_flags(args.flags));

			_wasmjit_block_signals(&set);

		error:
			_wasmjit_remove_unfreed_pointer(funcinst, ptr_idx_1);
			_wasmjit_remove_unfreed_pointer(funcinst, ptr_idx_2);
			free(msg.msg_control);
			free(msg.msg_iov);

			_wasmjit_unblock_signals(&set);

		}

		break;
	}
	case 17: { // recvmsg
		char *base;
		user_msghdr_t msg;
		struct em_msghdr emmsg;
		size_t ptr_idx_1 = 0, ptr_idx_2 = 0;

		LOAD_ARGS(funcinst, ivargs, 3,
			  int32_t, fd,
			  uint32_t, msg,
			  int32_t, flags);

		/* if there are flags we don't understand, then return invalid flag */
		if (has_bad_recvfrom_flag(args.flags))
			return -EM_EINVAL;

		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &emmsg,
						       args.msg,
						       sizeof(emmsg)))
			return -EM_EINVAL;

		emmsg.msg_name = uint32_t_swap_bytes(emmsg.msg_name);
		emmsg.msg_namelen = uint32_t_swap_bytes(emmsg.msg_namelen);
		emmsg.msg_iov = uint32_t_swap_bytes(emmsg.msg_iov);
		emmsg.msg_iovlen = uint32_t_swap_bytes(emmsg.msg_iovlen);
		emmsg.msg_control = uint32_t_swap_bytes(emmsg.msg_control);
		emmsg.msg_controllen = uint32_t_swap_bytes(emmsg.msg_controllen);
		emmsg.msg_flags = uint32_t_swap_bytes(emmsg.msg_flags);

		memset(&msg, 0, sizeof(msg));

		_wasmjit_block_signals(&set);

		base = wasmjit_emscripten_get_base_address(funcinst);

		if (emmsg.msg_name) {
			if (!_wasmjit_emscripten_check_range(funcinst,
							     emmsg.msg_name,
							     emmsg.msg_namelen)) {
				ret = -EFAULT;
				goto error2;
			}

			msg.msg_name = base + emmsg.msg_name;
		} else {
			msg.msg_name = NULL;
		}

		msg.msg_namelen = emmsg.msg_namelen;

		ret = copy_iov(funcinst, emmsg.msg_iov, emmsg.msg_iovlen, &msg.msg_iov);
		if (ret)
			goto error2;

		msg.msg_iovlen = emmsg.msg_iovlen;

		ptr_idx_2 = _wasmjit_add_unfreed_pointer(funcinst, msg.msg_iov);
		if (!ptr_idx_2) {
			ret = -ENOMEM;
			goto error2;
		}

		if (emmsg.msg_control) {
			size_t to_malloc;
			if (!_wasmjit_emscripten_check_range(funcinst,
							     emmsg.msg_control,
							     emmsg.msg_controllen)) {
				ret = -EFAULT;
				goto error2;
			}

			/*
			  allocating a right size buffer is difficult,
			  for simplicity's sake we assume the control buffer
			  was meant to be large enough for a single control
			  message. for situations where this goes wrong,
			  rely on MSG_CTRUNC.
			*/
			to_malloc = CMSG_SPACE(emmsg.msg_controllen -
					       EM_CMSG_ALIGN(sizeof(struct em_cmsghdr)));

			msg.msg_control = malloc(to_malloc);
			if (!msg.msg_control) {
				ret = -ENOMEM;
				goto error2;
			}
			msg.msg_controllen = to_malloc;

			ptr_idx_1 = _wasmjit_add_unfreed_pointer(funcinst, msg.msg_control);
			if (!ptr_idx_1) {
				ret = -ENOMEM;
				goto error2;
			}
		} else {
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
		}

		_wasmjit_unblock_signals(&set);

		ret = finish_recvmsg(args.fd, &msg,
				     convert_recvfrom_flags(args.flags));

		_wasmjit_block_signals(&set);
		if (ret < 0)
			goto error2;

		/* write changed msg values back to wasm msg */
		if (msg.msg_name) {
			assert(emmsg.msg_name == (char *)msg.msg_name - base);
			emmsg.msg_namelen = msg.msg_namelen;
		} else {
			assert(!msg.msg_namelen);
		}

		if (msg.msg_control) {
			if (write_cmsg(base, &emmsg, &msg)) {
				ret = LONG_MIN;
				goto error2;
			}
		} else {
			assert(!msg.msg_controllen);
		}

		emmsg.msg_flags = convert_recvmsg_msg_flags(msg.msg_flags);

		// we actually only need to copy back msg_namelen and msg_controllen
		// but for sake of clarity...
		emmsg.msg_name = uint32_t_swap_bytes(emmsg.msg_name);
		emmsg.msg_namelen = uint32_t_swap_bytes(emmsg.msg_namelen);
		emmsg.msg_iov = uint32_t_swap_bytes(emmsg.msg_iov);
		emmsg.msg_iovlen = uint32_t_swap_bytes(emmsg.msg_iovlen);
		emmsg.msg_control = uint32_t_swap_bytes(emmsg.msg_control);
		emmsg.msg_controllen = uint32_t_swap_bytes(emmsg.msg_controllen);
		emmsg.msg_flags = uint32_t_swap_bytes(emmsg.msg_flags);
		memcpy(base + args.msg, &emmsg, sizeof(emmsg));

		error2:
		_wasmjit_remove_unfreed_pointer(funcinst, ptr_idx_1);
		_wasmjit_remove_unfreed_pointer(funcinst, ptr_idx_2);
		free(msg.msg_control);
		free(msg.msg_iov);
		_wasmjit_unblock_signals(&set);

		if (ret == LONG_MIN) {
			wasmjit_emscripten_internal_abort("Unknown cmsg type!");
		}

		break;
	}
	default: {
		char buf[64];
		snprintf(buf, sizeof(buf),
			 "unsupported socketcall syscall %d", args.call);
		wasmjit_emscripten_internal_abort(buf);
		ret = -EINVAL;
		break;
	}
	}
	return check_ret(ret);
}

/* fcntl64 */
uint32_t wasmjit_emscripten____syscall221(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	LOAD_ARGS(funcinst, varargs, 2,
		  int32_t, fd,
		  int32_t, cmd);

	(void) which;

	/* TODO: implement */
	(void) args;

	return -EM_EINVAL;
}

/* chdir */
uint32_t wasmjit_emscripten____syscall12(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 1,
		  uint32_t, pathname);

	(void) which;

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	return check_ret(sys_chdir(base + args.pathname));
}

/* uname */
uint32_t wasmjit_emscripten____syscall122(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 1,
		  uint32_t, buf);

	(void) which;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, 390))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	strcpy(base + args.buf + 0, "Emscripten");
	strcpy(base + args.buf + 65, "emscripten");
	strcpy(base + args.buf + 130, "1.0");
	strcpy(base + args.buf + 195, "#1");
	strcpy(base + args.buf + 260, "x86-JS");

	return 0;
}

static int read_fdset(struct FuncInst *funcinst,
		      fd_set *dest,
		      uint32_t user_addr)
{
	em_fd_set emfds;
	size_t i;

	FD_ZERO(dest);

	if (_wasmjit_emscripten_copy_from_user(funcinst,
					       &emfds,
					       user_addr,
					       sizeof(emfds)))
		return -1;

	for (i = 0; i < ARRAY_LEN(emfds.fds_bits); ++i) {
		emfds.fds_bits[i] = uint32_t_swap_bytes(emfds.fds_bits[i]);
	}

	for (i = 0; i < EM_FD_SETSIZE; ++i) {
		if (EM_FD_ISSET(i, &emfds)) {
			FD_SET(i, dest);
		}
	}

	return 0;
}

static void write_fdset_nocheck(struct FuncInst *funcinst,
				uint32_t user_addr,
				fd_set *src)
{
	char *base;
	size_t i;
	em_fd_set emfds;

	EM_FD_ZERO(&emfds);

	for (i = 0; i < EM_FD_SETSIZE; ++i) {
		if (FD_ISSET(i, src)) {
			EM_FD_SET(i, &emfds);
		}
	}

	for (i = 0; i < ARRAY_LEN(emfds.fds_bits); ++i) {
		emfds.fds_bits[i] = uint32_t_swap_bytes(emfds.fds_bits[i]);
	}

	base = wasmjit_emscripten_get_base_address(funcinst);
	memcpy(base + user_addr, &emfds, sizeof(emfds));
}

/* select */
uint32_t wasmjit_emscripten____syscall142(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	long ret;
	struct timeval tv, *tvp;
	fd_set readfds, writefds, exceptfds;
	fd_set *readfdsp, *writefdsp, *exceptfdsp;

	LOAD_ARGS(funcinst, varargs, 5,
		  int32_t, nfds,
		  uint32_t, readfds,
		  uint32_t, writefds,
		  uint32_t, exceptfds,
		  uint32_t, timeout);

	(void) which;

	if (args.readfds) {
		if (read_fdset(funcinst, &readfds, args.readfds))
			return -EM_EFAULT;
		readfdsp = &readfds;
	} else {
		readfdsp = NULL;
	}

	if (args.writefds) {
		if (read_fdset(funcinst, &writefds, args.writefds))
			return -EM_EFAULT;
		writefdsp = &writefds;
	} else {
		writefdsp = NULL;
	}

	if (args.exceptfds) {
		if (read_fdset(funcinst, &exceptfds, args.exceptfds))
			return -EM_EFAULT;
		exceptfdsp = &exceptfds;
	} else {
		exceptfdsp = NULL;
	}

	if (args.timeout) {
		struct em_timeval etv;
		if (_wasmjit_emscripten_copy_from_user(funcinst, &etv, args.timeout, sizeof(etv)))
			return -EM_EFAULT;
		tv.tv_sec = uint32_t_swap_bytes(etv.tv_sec);
		tv.tv_usec = uint32_t_swap_bytes(etv.tv_usec);
		tvp = &tv;
	} else {
		tvp = NULL;
	}

	ret = sys_select(args.nfds,
			 readfdsp, writefdsp, exceptfdsp,
			 tvp);
	if (ret >= 0) {
		if (readfdsp) {
			assert(args.readfds);
			write_fdset_nocheck(funcinst, args.readfds, readfdsp);
		}

		if (writefdsp) {
			assert(args.writefds);
			write_fdset_nocheck(funcinst, args.writefds, writefdsp);
		}

		if (exceptfdsp) {
			assert(args.exceptfds);
			write_fdset_nocheck(funcinst, args.exceptfds, exceptfdsp);
		}
	}

	return check_ret(ret);
}

/* readv */
uint32_t wasmjit_emscripten____syscall145(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	struct iovec *liov;
	long rret;
	wasmjit_signal_block_ctx set;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, fd,
		  uint32_t, iov_user,
		  uint32_t, iovcnt);

	(void)which;

	_wasmjit_block_signals(&set);

	rret = copy_iov(funcinst, args.iov_user, args.iovcnt, &liov);
	if (rret >= 0) {
		rret = sys_readv(args.fd, liov, args.iovcnt);
		free(liov);
	}

	_wasmjit_unblock_signals(&set);

	return check_ret(rret);
}

/* chmod */
uint32_t wasmjit_emscripten____syscall15(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, pathname,
		  uint32_t, mode);

	(void)which;

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);
	return check_ret(sys_chmod(base + args.pathname, args.mode));
}

/* these are specified by posix */
#define EM_POLLIN     0x001
#define EM_POLLPRI    0x002
#define EM_POLLOUT    0x004
#define EM_POLLERR    0x008
#define EM_POLLHUP    0x010
#define EM_POLLNVAL   0x020
#define EM_POLLRDNORM 0x040
#define EM_POLLRDBAND 0x080
#define EM_POLLWRNORM 0x100
#define EM_POLLWRBAND 0x200

/* these are linux extensions */
#define EM_POLLREMOVE 0x1000
#define EM_POLLMSG    0x400
#define EM_POLLRDHUP  0x2000

static int check_poll_events(int16_t events) {
	int16_t allevents =
		EM_POLLIN |
		EM_POLLPRI |
		EM_POLLOUT |
		EM_POLLERR |
		EM_POLLHUP |
		EM_POLLNVAL |
		EM_POLLRDNORM |
		EM_POLLRDBAND |
		EM_POLLWRNORM |
		EM_POLLWRBAND |
#if (defined(__KERNEL__) || defined(__linux__))
		EM_POLLREMOVE |
		EM_POLLMSG |
		EM_POLLRDHUP |
#endif
		0;

	/* if event specifies anything we don't support
	   return false */
	return !(events & ~allevents);
}

static short convert_poll_events(int16_t events)
{
	short out = 0;

#define CS(c)					\
	if (events & EM_ ## c)			\
		out |= c

	CS(POLLIN);
	CS(POLLPRI);
	CS(POLLOUT);
	CS(POLLERR);
	CS(POLLHUP);
	CS(POLLNVAL);
	CS(POLLRDNORM);
	CS(POLLRDBAND);
	CS(POLLWRNORM);
	CS(POLLWRBAND);

#if (defined(__KERNEL__) || defined(__linux__))
	CS(POLLREMOVE);
	CS(POLLMSG);
	CS(POLLRDHUP);
#endif

#undef CS

	return out;
}

static int16_t back_convert_poll_events(short events)
{
	int16_t out = 0;

#define CS(c)				\
	if (events & c)			\
		out |= EM_ ## c

	CS(POLLIN);
	CS(POLLPRI);
	CS(POLLOUT);
	CS(POLLERR);
	CS(POLLHUP);
	CS(POLLNVAL);
	CS(POLLRDNORM);
	CS(POLLRDBAND);
	CS(POLLWRNORM);
	CS(POLLWRBAND);

#if (defined(__KERNEL__) || defined(__linux__))
	CS(POLLREMOVE);
	CS(POLLMSG);
	CS(POLLRDHUP);
#endif

#undef CS

	return out;
}

/* poll */
uint32_t wasmjit_emscripten____syscall168(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	char *base;
	size_t range;

	LOAD_ARGS(funcinst, varargs, 3,
		  uint32_t, fds,
		  uint32_t, nfds,
		  int32_t, timeout);

	(void)which;

	if (__builtin_mul_overflow(args.nfds,
				   sizeof(struct em_pollfd),
				   &range))
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.fds, range))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	/* this should be computable at compile time */
	if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ &&
	    sizeof(struct pollfd) == sizeof(struct em_pollfd) &&
	    sizeof(int) == sizeof(int32_t) &&
	    sizeof(short) == sizeof(int16_t) &&
	    POLLIN == EM_POLLIN &&
	    POLLRDNORM == EM_POLLRDNORM &&
	    POLLRDBAND == EM_POLLRDBAND &&
	    POLLPRI == EM_POLLPRI &&
	    POLLOUT == EM_POLLOUT &&
	    POLLWRNORM == EM_POLLWRNORM &&
	    POLLWRBAND == EM_POLLWRBAND &&
	    POLLERR == EM_POLLERR &&
	    POLLHUP == EM_POLLHUP &&
	    POLLNVAL == EM_POLLNVAL &&
#if (defined(__KERNEL__) || defined(__linux__))
	    POLLREMOVE == EM_POLLREMOVE &&
	    POLLMSG == EM_POLLMSG &&
	    POLLRDHUP == EM_POLLRDHUP &&
#endif
	    1) {
#if !(defined(__KERNEL__) || defined(__linux__))
		/* if not on linux, check that only posix flags are specfied */
		{
			uint32_t i;
			for (i = 0; i < args.nfds; ++i) {
				struct em_pollfd epfd;
				_wasmjit_memcpy_from_user(funcinst, &epfd,
							  args.fds + i * sizeof(epfd),
							  sizeof(epfd));
				if (!check_poll_events(epfd.events)) {
					return -EM_EINVAL;
				}
			}
		}
#endif
		return check_ret(sys_poll((struct pollfd *)(base + args.fds),
					  args.nfds, args.timeout));
	} else {
		uint32_t i, ret;
		struct pollfd *fds;
		wasmjit_signal_block_ctx set;
		size_t ptr_idx_1 = 0;

		_wasmjit_block_signals(&set);

		fds = wasmjit_alloc_vector(args.nfds,
					   sizeof(struct pollfd),
					   NULL);
		if (!fds) {
			ret = -ENOMEM;
			goto err;
		}

		ptr_idx_1 = _wasmjit_add_unfreed_pointer(funcinst, fds);
		if (!ptr_idx_1) {
			ret = -ENOMEM;
			goto err;
		}

		_wasmjit_unblock_signals(&set);

		for (i = 0; i < args.nfds; ++i) {
			struct em_pollfd epfd;

			_wasmjit_memcpy_from_user(funcinst, &epfd,
						  args.fds + i * sizeof(epfd),
						  sizeof(epfd));

			epfd.fd = uint32_t_swap_bytes(epfd.fd);
			epfd.events = uint16_t_swap_bytes(epfd.events);

			if (!check_poll_events(epfd.events)) {
				ret = -EINVAL;
				goto err;
			}

			fds[i].fd = epfd.fd;
			fds[i].events = convert_poll_events(epfd.events);
		}

		ret = check_ret(sys_poll(fds, args.nfds, args.timeout));

		/* write revents back to args.fds */
		for (i = 0; i < args.nfds; ++i) {
			struct em_pollfd epfd;

			_wasmjit_memcpy_from_user(funcinst, &epfd,
						  args.fds + i * sizeof(epfd),
						  sizeof(epfd));

			/* NB: we trust the flags given by revents,
			   e.g. we don't expect the flags to be larger
			   than 16 bits, or something the user doesn't expect */
			epfd.revents = uint16_t_swap_bytes(back_convert_poll_events(fds[i].revents));

			memcpy(base + args.fds + i * sizeof(epfd),
			       &epfd,
			       sizeof(epfd));
		}

		_wasmjit_block_signals(&set);

		err:
		_wasmjit_remove_unfreed_pointer(funcinst, ptr_idx_1);
		free(fds);

		_wasmjit_unblock_signals(&set);

		return ret;
	}
}

/* pread64 */
uint32_t wasmjit_emscripten____syscall180(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 5,
		  int32_t, fd,
		  uint32_t, buf,
		  uint32_t, count,
		  uint32_t, counthigh,
		  uint64_t, offset);

	(void)which;

	if (args.counthigh)
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, args.count))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	return check_ret(sys_pread(args.fd, base + args.buf, args.count, args.offset));
}

/* pwrite64 */
uint32_t wasmjit_emscripten____syscall181(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 5,
		  int32_t, fd,
		  uint32_t, buf,
		  uint32_t, count,
		  uint32_t, counthigh,
		  uint64_t, offset);

	(void)which;

	if (args.counthigh)
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, args.count))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	return check_ret(sys_pwrite(args.fd, base + args.buf, args.count, args.offset));
}

#define EM_RLIM_INFINITY  ~((uint64_t)0)

#define EM_RLIMIT_CPU     0
#define EM_RLIMIT_FSIZE   1
#define EM_RLIMIT_DATA    2
#define EM_RLIMIT_STACK   3
#define EM_RLIMIT_CORE    4
#define EM_RLIMIT_RSS     5
#define EM_RLIMIT_NPROC   6
#define EM_RLIMIT_NOFILE  7
#define EM_RLIMIT_MEMLOCK 8
#define EM_RLIMIT_AS      9
#define EM_RLIMIT_LOCKS   10
#define EM_RLIMIT_SIGPENDING 11
#define EM_RLIMIT_MSGQUEUE 12
#define EM_RLIMIT_NICE    13
#define EM_RLIMIT_RTPRIO  14

static int convert_resource(int32_t resource)
{
#if (defined(__KERNEL__) || defined(__linux__))
	return resource;
#endif

	switch (resource) {
#ifdef RLIMIT_CPU
	case EM_RLIMIT_CPU: return RLIMIT_CPU;
#endif
#ifdef RLIMIT_FSIZE
	case EM_RLIMIT_FSIZE: return RLIMIT_FSIZE;
#endif
#ifdef RLIMIT_DATA
	case EM_RLIMIT_DATA: return RLIMIT_DATA;
#endif
#ifdef RLIMIT_STACK
	case EM_RLIMIT_STACK: return RLIMIT_STACK;
#endif
#ifdef RLIMIT_CORE
	case EM_RLIMIT_CORE: return RLIMIT_CORE;
#endif
#ifdef RLIMIT_RSS
	case EM_RLIMIT_RSS: return RLIMIT_RSS;
#endif
#ifdef RLIMIT_NPROC
	case EM_RLIMIT_NPROC: return RLIMIT_NPROC;
#endif
#ifdef RLIMIT_NOFILE
	case EM_RLIMIT_NOFILE: return RLIMIT_NOFILE;
#endif
#ifdef RLIMIT_MEMLOCK
	case EM_RLIMIT_MEMLOCK: return RLIMIT_MEMLOCK;
#endif
#ifdef RLIMIT_AS
	case EM_RLIMIT_AS: return RLIMIT_AS;
#endif
#ifdef RLIMIT_LOCKS
	case EM_RLIMIT_LOCKS: return RLIMIT_LOCKS;
#endif
#ifdef RLIMIT_SIGPENDING
	case EM_RLIMIT_SIGPENDING: return RLIMIT_SIGPENDING;
#endif
#ifdef RLIMIT_MSGQUEUE
	case EM_RLIMIT_MSGQUEUE: return RLIMIT_MSGQUEUE;
#endif
#ifdef RLIMIT_NICE
	case EM_RLIMIT_NICE: return RLIMIT_NICE;
#endif
#ifdef RLIMIT_RTPRIO
	case EM_RLIMIT_RTPRIO: return RLIMIT_RTPRIO;
#endif
	default: return -1;
	}
}

static int32_t read_rlimit(struct FuncInst *funcinst,
			   struct rlimit *limit,
			   uint32_t user_addr)
{
	struct em_rlimit em_new_limit;
	if (_wasmjit_emscripten_copy_from_user(funcinst,
					       &em_new_limit,
					       user_addr,
					       sizeof(em_new_limit)))
		return -EM_EFAULT;
	em_new_limit.rlim_cur = uint64_t_swap_bytes(em_new_limit.rlim_cur);
	em_new_limit.rlim_max = uint64_t_swap_bytes(em_new_limit.rlim_max);

	limit->rlim_cur = em_new_limit.rlim_cur;
	limit->rlim_max = em_new_limit.rlim_max;
	return 0;
}

static void write_rlimit_nocheck(struct FuncInst *funcinst,
				 uint32_t user_addr,
				 struct rlimit *lrlim)
{
	char *base;
	struct em_rlimit emrlim;

	emrlim.rlim_cur = MMIN(EM_RLIM_INFINITY, lrlim->rlim_cur);
	emrlim.rlim_max = MMIN(EM_RLIM_INFINITY, lrlim->rlim_max);

	emrlim.rlim_cur = uint64_t_swap_bytes(emrlim.rlim_cur);
	emrlim.rlim_max = uint64_t_swap_bytes(emrlim.rlim_max);

	base = wasmjit_emscripten_get_base_address(funcinst);
	memcpy(base + user_addr, &emrlim, sizeof(emrlim));
}

/* ugetrlimit */
uint32_t wasmjit_emscripten____syscall191(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	int32_t ret;
	struct rlimit lrlim;
	int sys_resource;
	wasmjit_signal_block_ctx set;

	LOAD_ARGS(funcinst, varargs, 2,
		  int32_t, resource,
		  uint32_t, rlim);

	(void)which;

	if (!_wasmjit_emscripten_check_range(funcinst, args.rlim,
					     sizeof(struct em_rlimit)))
		return -EM_EFAULT;

	sys_resource = convert_resource(args.resource);

	_wasmjit_block_signals(&set);
	ret = check_ret(sys_getrlimit(sys_resource, &lrlim));
	_wasmjit_unblock_signals(&set);
	if (ret < 0)
		return ret;

	write_rlimit_nocheck(funcinst, args.rlim, &lrlim);

	return ret;
}

/* prlimit64 */
uint32_t wasmjit_emscripten____syscall340(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	int sys_resource;
	int32_t ret;
	struct rlimit *new_limitp, *old_limitp, new_limit, old_limit;
	wasmjit_signal_block_ctx set;

	LOAD_ARGS(funcinst, varargs, 4,
		  uint32_t, pid,
		  int32_t, resource,
		  uint32_t, new_limit,
		  uint32_t, old_limit);

	(void)which;

	if (!_wasmjit_emscripten_check_range(funcinst, args.old_limit,
					     sizeof(struct em_rlimit)))
		return -EM_EFAULT;

	if (args.new_limit) {
		ret = read_rlimit(funcinst, &new_limit, args.new_limit);
		if (ret < 0)
			return ret;
		new_limitp = &new_limit;
	} else {
		new_limitp = NULL;
	}

	if (args.old_limit) {
		old_limitp = &old_limit;
	} else {
		old_limitp = NULL;
	}

	sys_resource = convert_resource(args.resource);

	_wasmjit_block_signals(&set);
	ret = check_ret(sys_prlimit(args.pid, sys_resource, new_limitp, old_limitp));
	_wasmjit_unblock_signals(&set);
	if (ret < 0)
		return ret;

	if (old_limitp) {
		assert(args.old_limit);
		write_rlimit_nocheck(funcinst, args.old_limit, old_limitp);
	}

	return ret;
}

/* mmap2 */
uint32_t wasmjit_emscripten____syscall192(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	/* mmap()'s semantics in a WASM context are currently not clear.
	   additionally, an in-kernel implementation for file mapping requires
	   more invasive changes to the kernel. */
	return -EM_ENOSYS;
}

/* ftruncate64 */
uint32_t wasmjit_emscripten____syscall194(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	LOAD_ARGS(funcinst, varargs, 2,
		  int32_t, fd,
		  uint64_t, length)

	(void)which;

	return check_ret(sys_ftruncate(args.fd, args.length));
}


#if defined(__GNUC__)
#define ISUNSIGNED(a) ((typeof(a))0 - 1 > 0)
#else
#define ISUNSIGNED(a) (((a) - (a)) - 1 > 0)
#endif

#define OVERFLOWSN(a, n) (						\
	sizeof(a) > n &&						\
	(ISUNSIGNED(a)							\
	 ? (a) > UINT_MAX_N(n)						\
	 : ((a) < SINT_MIN_N(n) || (a) > SINT_MAX_N(n)))		\
	 )

#define OVERFLOWS(a) OVERFLOWSN((a), 4)

static int32_t write_stat(char *base,
			  uint32_t dest_addr,
			  sys_stat_t *st)
{
	uint32_t scratch;
	char *base2 = base + dest_addr;

	if (OVERFLOWS(st->st_ino))
		return -EM_EOVERFLOW;
	scratch = uint32_t_swap_bytes(st->st_ino);
	memcpy(base2 + offsetof(struct em_stat64, __st_ino_truncated),
	       &scratch, sizeof(scratch));

#define SETST(__e)						\
	if (OVERFLOWS(st->st_ ## __e))				\
		return -EM_EOVERFLOW;				\
	scratch = uint32_t_swap_bytes(st->st_ ## __e);		\
	memcpy(base2 + offsetof(struct em_stat64, st_ ## __e),	\
	       &scratch, sizeof(scratch))

	SETST(dev);
	SETST(mode);
	SETST(nlink);
	SETST(uid);
	SETST(gid);
	SETST(rdev);
	SETST(size);
	SETST(blksize);
	SETST(blocks);
	SETST(ino);

#undef SETST

	/* NB: st_get_nsec() is a custom portable macro we define */
#define STSTTIM(__e)							\
	do {								\
		if (OVERFLOWS(st->st_ ## __e ## time))			\
			return -EM_EOVERFLOW;				\
		scratch =						\
			uint32_t_swap_bytes(st->st_ ## __e ## time);	\
		memcpy(base2 + offsetof(struct em_stat64, st_ ## __e ## tim) + \
		       offsetof(struct em_timespec, tv_sec), &scratch,	\
		       sizeof(scratch));				\
		if (OVERFLOWS(st_get_nsec(__e, st)))			\
			return -EM_EOVERFLOW;				\
		scratch =						\
			uint32_t_swap_bytes(st_get_nsec(__e, st));	\
		memcpy(base2 + offsetof(struct em_stat64, st_ ## __e ## tim) + \
		       offsetof(struct em_timespec, tv_nsec), &scratch, \
		       sizeof(scratch));				\
	} while (0)

	STSTTIM(a);
	STSTTIM(m);
	STSTTIM(c);

#undef STSTTIM
#undef CAST

	return 0;
}

/* stat64 */
uint32_t wasmjit_emscripten____syscall195(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	int32_t ret;
	sys_stat_t st;
	char *base;

	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, pathname,
		  uint32_t, buf);

	(void)which;

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, sizeof(struct em_stat64)))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	ret = check_ret(sys_stat(base + args.pathname, &st));
	if (ret >= 0) {
		ret = write_stat(base, args.buf, &st);
	}

	return ret;
}

/* lstat64 */
uint32_t wasmjit_emscripten____syscall196(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	int32_t ret;
	sys_stat_t st;
	char *base;

	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, pathname,
		  uint32_t, buf);

	(void)which;

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, sizeof(struct em_stat64)))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	ret = check_ret(sys_lstat(base + args.pathname, &st));
	if (ret >= 0) {
		ret = write_stat(base, args.buf, &st);
	}

	return ret;
}

/* fstat64 */
uint32_t wasmjit_emscripten____syscall197(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	int32_t ret;
	sys_stat_t st;
	char *base;

	LOAD_ARGS(funcinst, varargs, 2,
		  int32_t, fd,
		  uint32_t, buf);

	(void)which;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, sizeof(struct em_stat64)))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	ret = check_ret(sys_fstat(args.fd, &st));
	if (ret >= 0) {
		ret = write_stat(base, args.buf, &st);
	}

	return ret;
}

/* getuid32 */
uint32_t wasmjit_emscripten____syscall199(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	return check_ret_signed(sys_getuid(), 0);
}

/* getgid32 */
uint32_t wasmjit_emscripten____syscall200(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	return check_ret_signed(sys_getgid(), 0);
}

/* getegid32 */
uint32_t wasmjit_emscripten____syscall202(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	return check_ret_signed(sys_getegid(), 0);
}

/* getpid */
uint32_t wasmjit_emscripten____syscall20(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	return check_ret_signed(sys_getpid(), 0);
}

/* geteuid32 */
uint32_t wasmjit_emscripten____syscall201(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	return check_ret_signed(sys_geteuid(), 0);
}

/* chown */
uint32_t wasmjit_emscripten____syscall212(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 3,
		  uint32_t, pathname,
		  uint32_t, owner,
		  uint32_t, group);

	(void)which;

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	return check_ret(sys_chown(base + args.pathname, args.owner, args.group));
}

#define EM_DT_UNKNOWN 0
#define EM_DT_FIFO 1
#define EM_DT_CHR 2
#define EM_DT_DIR 4
#define EM_DT_BLK 6
#define EM_DT_REG 8
#define EM_DT_LNK 10
#define EM_DT_SOCK 12

em_unsigned_char convert_dtype(em_unsigned_char d_type)
{
#if IS_LINUX
	return d_type;
#endif
	switch (d_type) {
	case EM_DT_FIFO: return DT_FIFO;
	case EM_DT_CHR: return DT_CHR;
	case EM_DT_DIR: return DT_DIR;
	case EM_DT_BLK: return DT_BLK;
	case EM_DT_REG: return DT_REG;
	case EM_DT_LNK: return DT_LNK;
	case EM_DT_SOCK: return DT_SOCK;
	default: return DT_UNKNOWN;
	}
}

/* getdents64 */

#if IS_LINUX

STATIC_ASSERT(sizeof(struct em_linux_dirent64) <= sizeof(struct linux_dirent64),
	      em_linux_dirent64_too_large);

uint32_t wasmjit_emscripten____syscall220(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	char *base;
	int32_t ret;
	char *dirbuf;
	size_t dirbufsz;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, fd,
		  uint32_t, dirent,
		  uint32_t, count);

	(void)which;

	if (!_wasmjit_emscripten_check_range(funcinst, args.dirent, args.count))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	dirbuf = base + args.dirent;
	dirbufsz = args.count;

	/* NB: on Linux this is async-signal-safe */
	ret = check_ret(sys_getdents64(args.fd, (void *) dirbuf, dirbufsz));
	if (ret >= 0) {
		/* if there was success, then we may need to do some conversion */
		char *srcbuf = dirbuf;
		char *dstbuf = base + args.dirent;
		long amt_read = ret;

		ret = 0;
		while (srcbuf < dirbuf + amt_read) {
			struct linux_dirent64 kdirentv;
			struct linux_dirent64 *kdirent;
			char *namebuf;
			struct em_linux_dirent64 towrite;
			unsigned short src_namelen;
			unsigned short newdstsize;

			memcpy(&kdirentv, srcbuf, offsetof(struct linux_dirent64, d_name));
			kdirent = &kdirentv;

			namebuf = srcbuf + offsetof(struct linux_dirent64, d_name);

			src_namelen = kdirent->d_reclen -
				offsetof(struct linux_dirent64, d_name);
			newdstsize =
				offsetof(struct em_linux_dirent64, d_name) +
				src_namelen;

			/* if there is no more space to write dirents, break */
			if (dstbuf + newdstsize > base + args.dirent + args.count) {
				break;
			}

			srcbuf += kdirent->d_reclen;

			/* dont need to write if struct is exactly the same */
			if (!(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ &&
			      sizeof(towrite) == sizeof(*kdirent) &&
			      sizeof(towrite.d_ino) == sizeof(kdirent->d_ino) &&
			      sizeof(towrite.d_off) == sizeof(kdirent->d_off) &&
			      sizeof(towrite.d_reclen) == sizeof(kdirent->d_reclen) &&
			      sizeof(towrite.d_type) == sizeof(kdirent->d_type) &&
			      offsetof(struct em_linux_dirent64, d_ino) == offsetof(struct linux_dirent64, d_ino) &&
			      offsetof(struct em_linux_dirent64, d_off) == offsetof(struct linux_dirent64, d_off) &&
			      offsetof(struct em_linux_dirent64, d_reclen) == offsetof(struct linux_dirent64, d_reclen) &&
			      offsetof(struct em_linux_dirent64, d_type) == offsetof(struct linux_dirent64, d_type))) {
				if (/* we have to remove this because emscripten goofed and made its
				       d_off not unconditionally 64-bit, like getdents64 requires.
				       in practice, this means telldir() in the user program will be totally broken
				       TODO: fix this upstream
				    */
				    /* OVERFLOWS(kdirent->d_ino) || */
				    /* OVERFLOWS(kdirent->d_off) || */
				    0) {
					wasmjit_emscripten_internal_abort("overflow while filling dirent");
				}
				towrite.d_ino = uint32_t_swap_bytes(kdirent->d_ino);
				towrite.d_off = int32_t_swap_bytes(kdirent->d_off);
				towrite.d_reclen = uint16_t_swap_bytes(newdstsize);
				towrite.d_type = convert_dtype(kdirent->d_type);

				memmove(dstbuf, &towrite,
				       offsetof(struct em_linux_dirent64, d_name));
				memmove(dstbuf + offsetof(struct em_linux_dirent64, d_name),
					namebuf,
					src_namelen);
			}

			dstbuf += newdstsize;
			ret += newdstsize;
		}
	}

	return ret;
}

#else

/* NB: this doesn't work in kernel context */

uint32_t wasmjit_emscripten____syscall220(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	char *base;
	struct EmscriptenContext *ctx;
	struct EmFile *file;
	uint32_t fds;
	char *dstbuf;
	uint32_t res;
	wasmjit_signal_block_ctx set;

	LOAD_ARGS(funcinst, varargs, 3,
		  uint32_t, fd,
		  uint32_t, dirent,
		  uint32_t, count);

	(void)which;

	/* NB: we must block signals while invoking libc *dir() functions
	   if reading a directory blocks indefinitely (like NFS or FUSE),
	   the process will be unresponsive to signals, but usually this is
	   the case anyway, since disk IO usually makes processes uninterruptible
	 */
	_wasmjit_block_signals(&set);

	if (!_wasmjit_emscripten_check_range(funcinst, args.dirent, args.count)) {
		res = -(int32_t) EM_EFAULT;
		goto err;
	}

	base = wasmjit_emscripten_get_base_address(funcinst);

	ctx = _wasmjit_emscripten_get_context(funcinst);

	fds = args.fd;
	if (fds >= ctx->fd_table.n_elts) {
		size_t oldlen = ctx->fd_table.n_elts;

		/* check that the descriptor is valid */
		if (fcntl(args.fd, F_GETFD) < 0) {
			res = check_ret(-errno);
			goto err;
		}

		if (!VECTOR_GROW(&ctx->fd_table, args.fd + 1 - ctx->fd_table.n_elts)) {
			res = -(int32_t) EM_ENOMEM;
			goto err;
		}

		memset(&ctx->fd_table.elts[oldlen], 0, sizeof(file) * (args.fd + 1 - oldlen));
	}

	file = ctx->fd_table.elts[fds];
	if (!file) {
		file = calloc(1, sizeof(*file));
		if (!file) {
			res = -(int32_t) EM_ENOMEM;
			goto err;
		}
		ctx->fd_table.elts[args.fd] = file;
	}

	if (!file->dirp) {
		file->dirp = fdopendir(args.fd);
		if (!file->dirp) {
			res = check_ret(-errno);
			goto err;
		}
	}

	dstbuf = base + args.dirent;
	res = 0;
	while (1) {
		struct em_linux_dirent64 towrite;
		struct dirent *kdirent;
		long curoff;
		uint16_t newdstsz;
		size_t namelen;

		curoff = telldir(file->dirp);

		kdirent = readdir(file->dirp);
		if (!kdirent)
			break;

		namelen = strlen(kdirent->d_name);

		if (offsetof(struct em_linux_dirent64, d_name) + namelen + 1 > 65535 ||
		    /* we have to remove this because emscripten goofed and made its
		       d_off not unconditionally 64-bit, like getdents64 requires.
		       in practice, this means telldir() in the user program will be totally broken
		       TODO: fix this upstream
		    */
		    /* OVERFLOWS(kdirent->d_ino) || */
		    /* OVERFLOWS(curoff) || */
		    0) {
			wasmjit_emscripten_internal_abort("overflow while filling dirent");
		}

		newdstsz = offsetof(struct em_linux_dirent64, d_name) + namelen + 1;

		if (res + newdstsz > args.count) {
			/* we read one too many entries, seek backward */
			seekdir(file->dirp, curoff);
			break;
		}

		towrite.d_ino = uint32_t_swap_bytes(kdirent->d_ino);
		towrite.d_off = int32_t_swap_bytes(curoff);
		towrite.d_reclen = uint16_t_swap_bytes(newdstsz);
		towrite.d_type = EM_DT_UNKNOWN;

		memcpy(dstbuf, &towrite, offsetof(struct em_linux_dirent64, d_name));
		memcpy(dstbuf + offsetof(struct em_linux_dirent64, d_name),
		       kdirent->d_name, namelen + 1);

		dstbuf += newdstsz;
		res += newdstsz;
	}

 err:
	_wasmjit_unblock_signals(&set);

	return res;
}

#endif

#define EM_O_RDONLY 0
#define EM_O_WRONLY 1
#define EM_O_RDWR 2
#define EM_O_APPEND 1024
#define EM_O_ASYNC 8192
#define EM_O_CLOEXEC 524288
#define EM_O_CREAT 64
#define EM_O_DIRECTORY 65536
#define EM_O_DSYNC 4096
#define EM_O_EXCL 128
#define EM_O_LARGEFILE 32768
#define EM_O_NOATIME 262144
#define EM_O_NOCTTY 256
#define EM_O_NOFOLLOW 131072
#define EM_O_NONBLOCK 2048
#define EM_O_PATH 2097152
#define EM_O_SYNC 1052672
#define EM_O_TMPFILE 4194304
#define EM_O_TRUNC 512

static int check_flags(uint32_t flags)
{
	uint32_t all =
		EM_O_RDONLY |
		EM_O_WRONLY |
		EM_O_RDWR |
		EM_O_APPEND |
#ifdef O_ASYNC
		EM_O_ASYNC |
#endif
		EM_O_CLOEXEC |
		EM_O_CREAT |
		EM_O_DIRECTORY |
		EM_O_DSYNC |
		EM_O_EXCL |
		/* we check O_LARGEFILE manually,
		   so unconditionally allow it */
		EM_O_LARGEFILE |
#ifdef O_NOATIME
		EM_O_NOATIME |
#endif
		EM_O_NOCTTY |
		EM_O_NOFOLLOW |
		EM_O_NONBLOCK |
#ifdef O_PATH
		EM_O_PATH |
#endif
		EM_O_SYNC |
#ifdef O_TMPFILE
		EM_O_TMPFILE |
#endif
		EM_O_TRUNC |
		0
		;
	return !(flags & ~all);
}

static int convert_sys_flags(uint32_t flags)
{
#if IS_LINUX && defined(__x86_64__)
	return flags;
#else

	int oflags = 0;

#define CHK(_n)					\
	if (flags & EM_O_ ## _n)		\
		oflags |= O_ ## _n

	CHK(RDONLY);
	CHK(WRONLY);
	CHK(RDWR);
	CHK(APPEND);
#ifdef O_ASYNC
	CHK(ASYNC);
#endif
	CHK(CLOEXEC);
	CHK(CREAT);
	CHK(DIRECTORY);
	CHK(DSYNC);
	CHK(EXCL);
#ifdef O_LARGEFILE
	CHK(LARGEFILE);
#endif
#ifdef O_NOATIME
	CHK(NOATIME);
#endif
	CHK(NOCTTY);
	CHK(NOFOLLOW);
	CHK(NONBLOCK);
#ifdef O_PATH
	CHK(PATH);
#endif
	CHK(SYNC);
#ifdef O_TMPFILE
	CHK(TMPFILE);
#endif
	CHK(TRUNC);

	return oflags;
#endif
}

#define EM_AT_FDCWD (-100)

uint32_t finish_openat(struct FuncInst *funcinst,
		       int32_t dirfd, uint32_t pathname,
		       uint32_t flags, uint32_t mode)
{
	char *base;
	mode_t sys_mode;
	int sys_flags;
	int sys_dirfd;
	int32_t ret;
	uint32_t had_large_file;

	if (!_wasmjit_emscripten_check_string(funcinst, pathname, PATH_MAX))
		return -EM_EFAULT;

	had_large_file = flags & EM_O_LARGEFILE;

	if (!check_flags(flags))
		return -EM_EINVAL;

	sys_dirfd = dirfd == EM_AT_FDCWD ? AT_FDCWD : dirfd;

	sys_flags = convert_sys_flags(flags);
	/* POSIX requires specific mode values */
	sys_mode = mode;

	base = wasmjit_emscripten_get_base_address(funcinst);

	ret = check_ret(sys_openat(sys_dirfd, base + pathname, sys_flags, sys_mode));
	if (ret >= 0) {
		if (!had_large_file && sizeof(off_t) != 32) {
			sys_stat_t st;
			long rret;
			rret = sys_fstat(ret, &st);
			if (rret < 0)
				wasmjit_emscripten_internal_abort("Stat failed in __syscall5()");
			if (st.st_size > INT32_MAX) {
				sys_close(ret);
				ret = -EM_EOVERFLOW;
			}
		}
	}

	return ret;
}

/* open */
uint32_t wasmjit_emscripten____syscall5(uint32_t which, uint32_t varargs,
					struct FuncInst *funcinst)
{
	LOAD_ARGS(funcinst, varargs, 3,
		  uint32_t, pathname,
		  uint32_t, flags,
		  uint32_t, mode);

	(void)which;

	return finish_openat(funcinst, EM_AT_FDCWD, args.pathname, args.flags, args.mode);
}

#define EM_ST_RDONLY 1
#define EM_ST_NOSUID 2
#define EM_ST_NODEV  4
#define EM_ST_NOEXEC 8
#define EM_ST_SYNCHRONOUS 16
#define EM_ST_MANDLOCK    64
#define EM_ST_WRITE       128
#define EM_ST_APPEND      256
#define EM_ST_IMMUTABLE   512
#define EM_ST_NOATIME     1024
#define EM_ST_NODIRATIME  2048

static int check_statvfs_flags(unsigned long flag)
{
	unsigned long all =
		ST_RDONLY |
		ST_NOSUID |
#ifdef ST_NODEV
		ST_NODEV |
#endif
#ifdef ST_NOEXEC
		ST_NOEXEC |
#endif
#ifdef ST_SYNCHRONOUS
		ST_SYNCHRONOUS |
#endif
#ifdef ST_MANDLOCK
		ST_MANDLOCK |
#endif
#ifdef ST_WRITE
		ST_WRITE |
#endif
#ifdef ST_APPEND
		ST_APPEND |
#endif
#ifdef ST_IMMUTABLE
		ST_IMMUTABLE |
#endif
#ifdef ST_NOATIME
		ST_NOATIME |
#endif
#ifdef ST_NODIRATIME
		ST_NODIRATIME |
#endif
		0;

#if IS_LINUX
	if (sizeof(flag) <= 4 || !(flag >> 32))
		return 1;
#endif

	return !(~all & flag);
}


static uint32_t convert_statvfs_flags(unsigned long flag)
{
	uint32_t out = 0;

#if IS_LINUX
	if (sizeof(flag) <= 4 || !(flag >> 32))
		return flag;
#endif

#define p(n) \
	if (flag & EM_ST_ ## n)			\
		out |= ST_ ## n

	p(RDONLY);
	p(NOSUID);

#ifdef ST_NODEV
	p(NODEV);
#endif
#ifdef ST_NOEXEC
	p(NOEXEC);
#endif
#ifdef ST_SYNCHRONOUS
	p(SYNCHRONOUS);
#endif
#ifdef ST_MANDLOCK
	p(MANDLOCK);
#endif
#ifdef ST_WRITE
	p(WRITE);
#endif
#ifdef ST_APPEND
	p(APPEND);
#endif
#ifdef ST_IMMUTABLE
	p(IMMUTABLE);
#endif
#ifdef ST_NOATIME
	p(NOATIME);
#endif
#ifdef ST_NODIRATIME
	p(NODIRATIME);
#endif

#undef p

	return out;
}

/* statfs64 */
uint32_t wasmjit_emscripten____syscall268(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	char *base;
	user_statvfs stvfs;
	int32_t ret;
	wasmjit_signal_block_ctx set;

	LOAD_ARGS(funcinst, varargs, 3,
		  uint32_t, pathname,
		  uint32_t, size,
		  uint32_t, buf);

	(void)which;

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, args.size))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	_wasmjit_block_signals(&set);
	ret = check_ret(sys_statvfs(base + args.pathname, &stvfs));
	_wasmjit_unblock_signals(&set);
	if (ret >= 0) {
		struct em_statfs64 out;

		if (OVERFLOWS(stvfs.f_bsize) ||
		    OVERFLOWS(stvfs.f_blocks) ||
		    OVERFLOWS(stvfs.f_bfree) ||
		    OVERFLOWS(stvfs.f_bavail) ||
		    OVERFLOWS(stvfs.f_files) ||
		    OVERFLOWS(stvfs.f_ffree) ||
		    OVERFLOWS(statvfs_get_low_fsid(&stvfs)) ||
		    OVERFLOWS(statvfs_get_high_fsid(&stvfs)) ||
		    OVERFLOWS(statvfs_get_namemax(&stvfs)) ||
		    OVERFLOWS(stvfs.f_frsize) ||
		    !check_statvfs_flags(statvfs_get_flags(&stvfs)) ||
		    0) {
			return -EM_EOVERFLOW;
		}

		out.f_type = statvfs_get_type(&stvfs);
		out.f_bsize = uint32_t_swap_bytes(stvfs.f_bsize);
		out.f_blocks = uint32_t_swap_bytes(stvfs.f_blocks);
		out.f_bfree = uint32_t_swap_bytes(stvfs.f_bfree);
		out.f_bavail = uint32_t_swap_bytes(stvfs.f_bavail);
		out.f_files = uint32_t_swap_bytes(stvfs.f_files);
		out.f_ffree = uint32_t_swap_bytes(stvfs.f_ffree);
		out.f_fsid.__val[0] = uint32_t_swap_bytes(statvfs_get_low_fsid(&stvfs));
		out.f_fsid.__val[1] = uint32_t_swap_bytes(statvfs_get_high_fsid(&stvfs));
		out.f_namelen = uint32_t_swap_bytes(statvfs_get_namemax(&stvfs));
		out.f_frsize = uint32_t_swap_bytes(stvfs.f_frsize);
		out.f_flags = uint32_t_swap_bytes(convert_statvfs_flags(statvfs_get_flags(&stvfs)));

		memcpy(base + args.buf, &out, sizeof(out));
	}
	return ret;
}

#define EM_POSIX_FADV_NORMAL	0 /* No further special treatment.  */
#define EM_POSIX_FADV_RANDOM	1 /* Expect random page references.  */
#define EM_POSIX_FADV_SEQUENTIAL	2 /* Expect sequential page references.  */
#define EM_POSIX_FADV_WILLNEED	3 /* Will need these pages.  */
#define EM_POSIX_FADV_DONTNEED	4 /* Don't need these pages.  */
#define EM_POSIX_FADV_NOREUSE	5 /* Data will be accessed once.  */

int check_advice(uint32_t advice)
{
#if 0 && IS_LINUX && !defined(__s390x__)
	(void)advice;
	return 1;
#else
	uint32_t all =
		EM_POSIX_FADV_NORMAL |
		EM_POSIX_FADV_RANDOM |
		EM_POSIX_FADV_SEQUENTIAL |
		EM_POSIX_FADV_WILLNEED |
		EM_POSIX_FADV_DONTNEED |
		EM_POSIX_FADV_NOREUSE |
		0;


	return !(advice & ~all);
#endif
}

int convert_advice(uint32_t advice)
{
#if 0 && IS_LINUX && !defined(__s390x__)
	return advice;
#else
	int out = 0;

#define p(n)					\
	if (advice & EM_POSIX_FADV_ ## n)	\
		out |= POSIX_FADV_ ## n

	p(NORMAL);
	p(RANDOM);
	p(SEQUENTIAL);
	p(WILLNEED);
	p(DONTNEED);
	p(NOREUSE);

#undef p

	return out;
#endif
}

/* fadvise64_64 */
uint32_t wasmjit_emscripten____syscall272(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	int advice;
	int32_t res;
	wasmjit_signal_block_ctx set;

	LOAD_ARGS(funcinst, varargs, 4,
		  uint32_t, fd,
		  int32_t, offset,
		  int32_t, len,
		  uint32_t, advice);

	(void)which;

	if (!check_advice(args.advice)) {
		return -EM_EINVAL;
	}

	advice = convert_advice(args.advice);

	_wasmjit_block_signals(&set);
	res = check_ret(sys_posix_fadvise(args.fd, args.offset, args.len, advice));
	_wasmjit_unblock_signals(&set);

	return res;
}

/* openat */
uint32_t wasmjit_emscripten____syscall295(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	LOAD_ARGS(funcinst, varargs, 4,
		  int32_t, dirfd,
		  uint32_t, pathname,
		  uint32_t, flags,
		  uint32_t, mode);

	(void) which;

	return finish_openat(funcinst, args.dirfd, args.pathname, args.flags, args.mode);
}

#define EM_AT_EMPTY_PATH 0x1000
#define EM_AT_NO_AUTOMOUNT 0x800
#define EM_AT_SYMLINK_NOFOLLOW 0x400

static int check_fstatat_flags(uint32_t flags)
{
	uint32_t all =
#ifdef AT_EMPTY_PATH
		EM_AT_EMPTY_PATH |
#endif
#ifdef AT_NO_AUTOMOUNT
		EM_AT_NO_AUTOMOUNT |
#endif
		EM_AT_SYMLINK_NOFOLLOW |
		0;

	return ~(flags & ~all);
}

static int convert_fstatat_flags(uint32_t flags)
{
	int out = 0;

#define p(n)					\
	if (flags & EM_AT_ ## n)		\
		out |= AT_ ## n

#ifdef AT_EMPTY_PATH
	p(EMPTY_PATH);
#endif

#ifdef AT_NO_AUTOMOUNT
	p(NO_AUTOMOUNT);
#endif

	p(SYMLINK_NOFOLLOW);

#undef p

	return out;
}

/* fstatat64 */
uint32_t wasmjit_emscripten____syscall300(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	sys_stat_t st;
	char *base;
	int sys_dirfd, sys_flags;
	int32_t ret;

	LOAD_ARGS(funcinst, varargs, 4,
		  int32_t, dirfd,
		  uint32_t, pathname,
		  uint32_t, buf,
		  uint32_t, flags);

	(void) which;

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, sizeof(struct em_stat64)))
		return -EM_EFAULT;

	if (!check_fstatat_flags(args.flags))
		return -EM_EINVAL;

	sys_flags = convert_fstatat_flags(args.flags);

	base = wasmjit_emscripten_get_base_address(funcinst);

	sys_dirfd = args.dirfd == EM_AT_FDCWD ? AT_FDCWD : args.dirfd;

	ret = check_ret(sys_fstatat(sys_dirfd, base + args.pathname, &st, sys_flags));
	if (ret >= 0) {
		ret = write_stat(base, args.buf, &st);
	}

	return ret;
}

/* pwritev */
uint32_t wasmjit_emscripten____syscall334(uint32_t which, uint32_t varargs,
					  struct FuncInst *funcinst)
{
	int32_t ret;
	struct iovec *liov = NULL;
	wasmjit_signal_block_ctx set;
	size_t ptr_idx_1 = 0;

	LOAD_ARGS(funcinst, varargs, 4,
		  uint32_t, fd,
		  uint32_t, iov,
		  uint32_t, iovcnt,
		  uint32_t, offset);

	(void)which;

	_wasmjit_block_signals(&set);

	ret = check_ret(copy_iov(funcinst, args.iov, args.iovcnt, &liov));
	if (ret < 0)
		goto error;

	ptr_idx_1 = _wasmjit_add_unfreed_pointer(funcinst, liov);
	if (!ptr_idx_1) {
		ret = -EM_ENOMEM;
		goto error;
	}

	_wasmjit_unblock_signals(&set);
	/* NB: We have to assume that pwritev() is async-signal-safe because
	   it may block indefinitely and we don't want to block signals during that */
	ret = check_ret(sys_pwritev(args.fd, liov, args.iovcnt, args.offset));
	_wasmjit_block_signals(&set);

error:
	_wasmjit_remove_unfreed_pointer(funcinst, ptr_idx_1);
	free(liov);
	_wasmjit_unblock_signals(&set);

	return ret;
}

/* rename */
uint32_t wasmjit_emscripten____syscall38(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, old_path,
		  uint32_t, new_path);

	(void)which;

	if (!_wasmjit_emscripten_check_string(funcinst, args.old_path, PATH_MAX))
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_string(funcinst, args.new_path, PATH_MAX))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);
	return check_ret(sys_rename(base + args.old_path, base + args.new_path));
}

/* mkdir */
uint32_t wasmjit_emscripten____syscall39(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, pathname,
		  uint32_t, mode);

	(void)which;

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);
	return check_ret(sys_mkdir(base + args.pathname, args.mode));
}

/* umask */
uint32_t wasmjit_emscripten____syscall60(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	LOAD_ARGS(funcinst, varargs, 1,
		  uint32_t, mask);

	(void)which;

	return check_ret_signed(sys_umask(args.mask), 0);
}

/* dup2 */
uint32_t wasmjit_emscripten____syscall63(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	LOAD_ARGS(funcinst, varargs, 2,
		  uint32_t, oldfd,
		  uint32_t, newfd);

	(void)which;

	return check_ret(sys_dup2(args.oldfd, args.newfd));
}

/* getppid */
uint32_t wasmjit_emscripten____syscall64(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	return check_ret_signed(sys_getppid(), 0);
}

/* setsid */
uint32_t wasmjit_emscripten____syscall66(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	return check_ret(sys_setsid());
}

/* setrlimit */
uint32_t wasmjit_emscripten____syscall75(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	struct rlimit lrlim;
	int sys_resource;
	uint32_t ret;
	wasmjit_signal_block_ctx set;

	LOAD_ARGS(funcinst, varargs, 2,
		  int32_t, resource,
		  uint32_t, rlim);

	(void)which;

	if (read_rlimit(funcinst, &lrlim, args.rlim) < 0)
		return -EM_EFAULT;

	sys_resource = convert_resource(args.resource);

	_wasmjit_block_signals(&set);
	ret = check_ret(sys_setrlimit(sys_resource, &lrlim));
	_wasmjit_unblock_signals(&set);

	return ret;
}

/* readlink */
uint32_t wasmjit_emscripten____syscall85(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	char *base;

	LOAD_ARGS(funcinst, varargs, 3,
		  uint32_t, pathname,
		  uint32_t, buf,
		  uint32_t, bufsize);

	(void)which;

	if (!_wasmjit_emscripten_check_string(funcinst, args.pathname, PATH_MAX))
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, args.buf, args.bufsize))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);
	return check_ret(sys_readlink(base + args.pathname, base + args.buf, args.bufsize));
}

/* munmap */
uint32_t wasmjit_emscripten____syscall91(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	(void)which;
	(void)varargs;
	(void)funcinst;
	/* we don't currently support munmap for the same reason we don't currently support
	   mmap2 */
	return -EM_ENOSYS;
}

#define EM_PRIO_PROCESS 0
#define EM_PRIO_PGRP 1
#define EM_PRIO_USER 2

/* setpriority */
uint32_t wasmjit_emscripten____syscall97(uint32_t which, uint32_t varargs,
					 struct FuncInst *funcinst)
{
	int sys_which;
	wasmjit_signal_block_ctx set;
	uint32_t ret;

	LOAD_ARGS(funcinst, varargs, 3,
		  int32_t, which,
		  int32_t, who,
		  int32_t, niceval);

	(void)which;

#if IS_LINUX
	sys_which = args.niceval;
#else
	switch (args.who) {
	case EM_PRIO_PROCESS: sys_which = PRIO_PROCESS; break;
	case EM_PRIO_PGRP: sys_which = PRIO_PGRP; break;
	case EM_PRIO_USER: sys_which = PRIO_USER; break;
	default: return -EM_EINVAL;
	}
#endif
	_wasmjit_block_signals(&set);
	ret = check_ret(sys_setpriority(sys_which, args.who, args.niceval));
	_wasmjit_unblock_signals(&set);

	return ret;
}

__attribute__((noreturn))
void wasmjit_emscripten__abort(struct FuncInst *funcinst)
{
	(void) funcinst;
	wasmjit_trap(WASMJIT_TRAP_ABORT);
}

__attribute__ ((unused))
static int copy_string_array(struct FuncInst *funcinst,
			     uint32_t string_array,
			     char ***largvp)
{
	int ret;
	char **largv = NULL, *base;
	uint32_t local_argv;
	size_t i = 0, n_args;

	base = wasmjit_emscripten_get_base_address(funcinst);

	local_argv = string_array;
	for (i = 0; 1; ++i) {
		uint32_t argp;
		if (_wasmjit_emscripten_copy_from_user(funcinst, &argp, local_argv, sizeof(argp))) {
			ret = EFAULT;
			goto error;
		}
		if (!argp) break;
		/* sizeof(char *) in emscripten space */
		local_argv += 4;
	}

	n_args = i;

	largv = wasmjit_alloc_vector(n_args + 1, sizeof(char *), NULL);
	if (!largv) {
		ret = ENOMEM;
		goto error;
	}

	local_argv = string_array;
	for (i = 0; 1; ++i) {
		uint32_t argp;
		if (_wasmjit_emscripten_copy_from_user(funcinst, &argp, local_argv, sizeof(argp))) {
			ret = EFAULT;
			goto error;
		}
		if (!argp) break;

		argp = uint32_t_swap_bytes(argp);

		if (!_wasmjit_emscripten_check_string(funcinst, argp, MAX_ARG_STRLEN)) {
			ret = EFAULT;
			goto error;
		}

		largv[i] = base + argp;
		/* sizeof(char *) in emscripten space */
		local_argv += 4;
	}

	largv[n_args] = NULL;

	*largvp = largv;
	ret = 0;

	if (0) {
	error:
		free(largv);
	}

	return ret;
}

uint32_t wasmjit_emscripten__execve(uint32_t pathname,
				    uint32_t argv,
				    uint32_t envp,
				    struct FuncInst *funcinst)
{
#ifdef __KERNEL__
	/* TODO: implement for kernel */
	wasmjit_emscripten____setErrNo(EM_ENOEXEC, funcinst);
	return -1;
#else
	char **largv = NULL, **lenvp = NULL, *base;
	int32_t ret;
	wasmjit_signal_block_ctx set;

	_wasmjit_block_signals(&set);

	if (!_wasmjit_emscripten_check_string(funcinst, pathname, PATH_MAX)) {
		errno = EFAULT;
		ret = -1;
		goto err;
	}

	if ((errno = copy_string_array(funcinst, argv, &largv))) {
		ret = -1;
		goto err;
	}

	if ((errno = copy_string_array(funcinst, envp, &lenvp))) {
		ret = -1;
		goto err;
	}

	_wasmjit_unblock_signals(&set);

	base = wasmjit_emscripten_get_base_address(funcinst);
	ret = execve(base + pathname, largv, lenvp);

	_wasmjit_block_signals(&set);
	if (ret < 0) {
 err:
		assert(errno >= 0);
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
	}

	free(largv);
	free(lenvp);

	_wasmjit_unblock_signals(&set);

	return ret;
#endif
}

#define EM_CLOCK_REALTIME           0
#define EM_CLOCK_MONOTONIC          1
#define EM_CLOCK_PROCESS_CPUTIME_ID 2
#define EM_CLOCK_THREAD_CPUTIME_ID  3
#define EM_CLOCK_MONOTONIC_RAW      4
#define EM_CLOCK_REALTIME_COARSE    5
#define EM_CLOCK_MONOTONIC_COARSE   6
#define EM_CLOCK_BOOTTIME           7
#define EM_CLOCK_REALTIME_ALARM     8
#define EM_CLOCK_BOOTTIME_ALARM     9
#define EM_CLOCK_SGI_CYCLE         10
#define EM_CLOCK_TAI               11

uint32_t wasmjit_emscripten__clock_gettime(uint32_t clk_id, uint32_t tp,
					   struct FuncInst *funcinst)
{
	struct em_timespec emtspec;
	struct timespec tspec;
	int32_t ret;
	clockid_t sys_clk_id;

#if IS_LINUX
	sys_clk_id = clk_id;
#else
	switch (clk_id) {
#define p(n) case EM_CLOCK_ ## n: sys_clk_id = CLOCK_ ##n; break
		p(REALTIME);
		p(MONOTONIC);
#ifdef CLOCK_PROCESS_CPUTIME_ID
		p(PROCESS_CPUTIME_ID);
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
		p(THREAD_CPUTIME_ID);
#endif
#ifdef CLOCK_MONOTONIC_RAW
		p(MONOTONIC_RAW);
#endif
#ifdef CLOCK_REALTIME_COARSE
		p(REALTIME_COARSE);
#endif
#ifdef CLOCK_MONOTONIC_COARSE
		p(MONOTONIC_COARSE);
#endif
#ifdef CLOCK_BOOTTIME
		p(BOOTTIME);
#endif
#ifdef CLOCK_REALTIME_ALARM
		p(REALTIME_ALARM);
#endif
#ifdef CLOCK_BOOTTIME_ALARM
		p(BOOTTIME_ALARM);
#endif
#ifdef CLOCK_SGI_CYCLE
		p(SGI_CYCLE);
#endif
#ifdef CLOCK_TAI
		p(TAI);
#endif
#undef p
	default: ret = -EINVAL; goto err;
	}
#endif

	ret = sys_clock_gettime(sys_clk_id, &tspec);
	if (ret >= 0) {
		if (OVERFLOWS(tspec.tv_sec) ||
		    OVERFLOWS(tspec.tv_nsec)) {
			ret = -EOVERFLOW;
			goto err;
		}

		emtspec.tv_sec = uint32_t_swap_bytes(tspec.tv_sec);
		emtspec.tv_nsec = uint32_t_swap_bytes(tspec.tv_nsec);

		if (_wasmjit_emscripten_copy_to_user(funcinst, tp, &emtspec, sizeof(struct em_timespec))) {
			ret = -EFAULT;
			goto err;
		}

		return ret;
	} else {
	err:
		wasmjit_emscripten____setErrNo(convert_errno(-ret), funcinst);
		return (int32_t) -1;
	}
}

__attribute__((noreturn))
void wasmjit_emscripten__exit(uint32_t status,
			      struct FuncInst *funcinst)
{
	(void) funcinst;
	wasmjit_exit(status);
}

uint32_t wasmjit_emscripten__fork(struct FuncInst *funcinst)
{
#ifdef __KERNEL__
	/* TODO: implement for kernel */
	wasmjit_emscripten____setErrNo(EM_EAGAIN, funcinst);
	return -1;
#else
	int ret;
	ret = fork();
	if (ret < 0) {
		assert(errno >= 0);
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
	}
	return (int32_t) ret;
#endif
}

#define EM_EAI_BADFLAGS   (-1)
#define EM_EAI_NONAME     (-2)
#define EM_EAI_AGAIN      (-3)
#define EM_EAI_FAIL       (-4)
#define EM_EAI_FAMILY     (-6)
#define EM_EAI_SOCKTYPE   (-7)
#define EM_EAI_SERVICE    (-8)
#define EM_EAI_MEMORY     (-10)
#define EM_EAI_SYSTEM     (-11)
#define EM_EAI_OVERFLOW   (-12)

#define EM_AI_PASSIVE      0x01
#define EM_AI_CANONNAME    0x02
#define EM_AI_NUMERICHOST  0x04
#define EM_AI_V4MAPPED     0x08
#define EM_AI_ALL          0x10
#define EM_AI_ADDRCONFIG   0x20
#define EM_AI_NUMERICSERV  0x400

struct em_addrinfo {
	int32_t ai_flags;
	int32_t ai_family;
	int32_t ai_socktype;
	int32_t ai_protocol;
	em_socklen_t ai_addrlen;
	uint32_t ai_addr;
	uint32_t ai_canonname;
	uint32_t ai_next;
};

#ifdef __KERNEL__

#define EAI_BADFLAGS   (-1)
#define EAI_NONAME     (-2)
#define EAI_AGAIN      (-3)
#define EAI_FAIL       (-4)
#define EAI_FAMILY     (-6)
#define EAI_SOCKTYPE   (-7)
#define EAI_SERVICE    (-8)
#define EAI_MEMORY     (-10)
#define EAI_SYSTEM     (-11)
#define EAI_OVERFLOW   (-12)

#define AI_PASSIVE      0x01
#define AI_CANONNAME    0x02
#define AI_NUMERICHOST  0x04
#define AI_V4MAPPED     0x08
#define AI_ALL          0x10
#define AI_ADDRCONFIG   0x20
#define AI_NUMERICSERV  0x400

struct addrinfo {
	int              ai_flags;
	int              ai_family;
	int              ai_socktype;
	int              ai_protocol;
	socklen_t        ai_addrlen;
	struct sockaddr *ai_addr;
	char            *ai_canonname;
	struct addrinfo *ai_next;
};


/* TODO: implement this */

int getaddrinfo(const char *node, const char *service,
		const struct addrinfo *hints,
		struct addrinfo **res)
{
	(void) node;
	(void) service;
	(void) hints;
	(void) res;
	errno = ENOSYS;
	return EAI_SYSTEM;
}

void freeaddrinfo(struct addrinfo *res)
{
	(void) res;
}

const char *gai_strerror(int errcode)
{
	return "Unknown error";
}

char *getenv(const char *name)
{
	(void) name;
	return NULL;
}

void endgrent(void)
{
	/* NB: Kernel doesn't have endgrent, it's a higher level thing
	   so we have to re-implement it in kernel space. In the future
	   we have to move this functionality into the module */
}

struct group {
	char *gr_name;
	char *gr_passwd;
	gid_t gr_gid;
	char **gr_mem;
};

struct group *getgrent(void)
{
	errno = ENOMEM;
	return NULL;
}

struct group *getgrnam(const char *name)
{
	errno = ENOSYS;
	return NULL;
}

struct passwd {
	char   *pw_name;
	char   *pw_passwd;
	uid_t   pw_uid;
	gid_t   pw_gid;
	char   *pw_gecos;
	char   *pw_dir;
	char   *pw_shell;
};

struct passwd *getpwnam(const char *name)
{
	errno = ENOSYS;
	return NULL;
}

typedef long time_t;

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
	long tm_gmtoff;
	char *tm_zone;
};

struct tm *gmtime_r(const time_t *clock, struct tm *result)
{
	(void) clock;
	(void) result;
	errno = ENOSYS;
	return NULL;
}

time_t time(time_t *tmloc)
{
	errno = ENOSYS;
	return (time_t) -1;
}

struct tm *localtime_r(const time_t *clock, struct tm *result)
{
	(void) clock;
	(void) result;
	errno = ENOSYS;
	return NULL;
}

time_t mktime(struct tm *timeptr)
{
	return (time_t) -1;
}

int raise(int sig)
{
	errno = ENOSYS;
	return -1;
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	(void) sem;
	(void) pshared;
	(void) value;
	errno = ENOSYS;
	return -1;
}

int sem_post(sem_t *sem)
{
	(void) sem;
	errno = ENOSYS;
	return -1;
}

int sem_wait(sem_t *sem)
{
	(void) sem;
	errno = ENOSYS;
	return -1;
}

void setgrent(void)
{
	return;
}

int sigemptyset(sigset_t *set)
{
	/* TODO: implement */
	(void) set;
	return -1;
}

int sigaddset(sigset_t *set, int sig)
{
	/* TODO: implement */
	(void) set;
	(void) sig;
	return -1;
}

int sigismember(sigset_t *set, int sig)
{
	/* TODO: implement */
	(void) set;
	(void) sig;
	return -1;
}

#elif __APPLE__

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	/* TODO: use dispatch/dispatch.h */
	(void) sem;
	(void) pshared;
	(void) value;
	errno = ENOSYS;
	return -1;
}

int sem_post(sem_t *sem)
{
	/* TODO: use dispatch/dispatch.h */
	(void) sem;
	errno = ENOSYS;
	return -1;
}

int sem_wait(sem_t *sem)
{
	/* TODO: use dispatch/dispatch.h */
	(void) sem;
	errno = ENOSYS;
	return -1;
}

#endif

static int check_ai_flags(int32_t ai_flags)
{
	uint32_t all =
		EM_AI_PASSIVE |
		EM_AI_CANONNAME |
		EM_AI_NUMERICHOST |
#ifdef AI_V4MAPPED
		EM_AI_V4MAPPED |
#endif
#ifdef AI_ALL
		EM_AI_ALL |
#endif
		EM_AI_ADDRCONFIG |
		EM_AI_NUMERICSERV |
		0;
	return !(ai_flags & ~all);
}

static int convert_ai_flags(int32_t ai_flags)
{
	int out = 0;
#define p(n)					\
	if (ai_flags & EM_AI_ ## n)		\
		out |= AI_ ## n

	p(PASSIVE);
	p(CANONNAME);
	p(NUMERICHOST);
#ifdef AI_V4MAPPED
	p(V4MAPPED);
#endif
#ifdef AI_ALL
	p(ALL);
#endif
	p(ADDRCONFIG);
	p(NUMERICSERV);

#undef p

	return out;
}

static int32_t back_convert_ai_flags(int ai_flags)
{
	int32_t out = 0;
#define p(n)					\
	if (ai_flags & AI_ ## n)		\
		out |= EM_AI_ ## n

	p(PASSIVE);
	p(CANONNAME);
	p(NUMERICHOST);
#ifdef AI_V4MAPPED
	p(V4MAPPED);
#endif
#ifdef AI_ALL
	p(ALL);
#endif
	p(ADDRCONFIG);
	p(NUMERICSERV);

#undef p

	return out;
}

static int32_t convert_getaddrinfo_return(int ret)
{
	switch (ret) {
#define p(n) case EAI_ ## n: return EM_EAI_ ## n
		p(BADFLAGS);
		p(NONAME);
		p(AGAIN);
		p(FAIL);
		p(FAMILY);
		p(SOCKTYPE);
		p(SERVICE);
		p(MEMORY);
		p(SYSTEM);
		p(OVERFLOW);
	case 0: return 0;
	default: return -1;
#undef p
	}
}

static int back_convert_getaddrinfo_return(int32_t ret)
{
	switch (ret) {
#define p(n) case EM_EAI_ ## n: return EAI_ ## n
		p(BADFLAGS);
		p(NONAME);
		p(AGAIN);
		p(FAIL);
		p(FAMILY);
		p(SOCKTYPE);
		p(SERVICE);
		p(MEMORY);
		p(SYSTEM);
		p(OVERFLOW);
	case 0: return 0;
	default: return -1;
#undef p
	}
}

uint32_t wasmjit_emscripten__getaddrinfo(uint32_t node,
					 uint32_t service,
					 uint32_t emhintp,
					 uint32_t out,
					 struct FuncInst *funcinst)
{
	char *base;
	struct em_addrinfo emhint;
	int ret;
	struct addrinfo hint, *hintp, *res;
#ifndef SAME_SOCKADDR
	struct sockaddr_storage ss;
#endif
	int32_t filter_protocol = 0;
	wasmjit_signal_block_ctx set;

	if (!_wasmjit_emscripten_check_string(funcinst, node, PATH_MAX)) {
		ret = EAI_SYSTEM;
		errno = EFAULT;
		goto err;
	}

	if (!_wasmjit_emscripten_check_string(funcinst, service, PATH_MAX)) {
		ret = EAI_SYSTEM;
		errno = EFAULT;
		goto err;
	}

	/* check that we can write a pointer to out */
	if (!_wasmjit_emscripten_check_range(funcinst, out, sizeof(uint32_t))) {
		ret = EAI_SYSTEM;
		errno = EFAULT;
		goto err;
	}

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (emhintp) {
		if (_wasmjit_emscripten_copy_from_user(funcinst,
						       &emhint,
						       emhintp,
						       sizeof(emhint))) {
			ret = EAI_SYSTEM;
			errno = EFAULT;
			goto err;
		}

		emhint.ai_flags = int32_t_swap_bytes(emhint.ai_flags);
		emhint.ai_family = int32_t_swap_bytes(emhint.ai_family);
		emhint.ai_socktype = int32_t_swap_bytes(emhint.ai_socktype);
		emhint.ai_protocol = int32_t_swap_bytes(emhint.ai_protocol);
		emhint.ai_addrlen = int32_t_swap_bytes(emhint.ai_addrlen);
		emhint.ai_addr = uint32_t_swap_bytes(emhint.ai_addr);
		emhint.ai_canonname = uint32_t_swap_bytes(emhint.ai_canonname);
		emhint.ai_next = uint32_t_swap_bytes(emhint.ai_next);

		if (!check_ai_flags(emhint.ai_flags)) {
			ret = EAI_BADFLAGS;
			goto err;

		}

		hint.ai_flags = convert_ai_flags(emhint.ai_flags);
		hint.ai_family = emhint.ai_family
			? convert_socket_domain_to_local(emhint.ai_family)
			: 0;
		if (hint.ai_family == -1) {
			ret = EAI_FAMILY;
			goto err;
		}
		hint.ai_socktype = emhint.ai_socktype
			? convert_socket_type_to_local(emhint.ai_socktype)
			: 0;
		if (hint.ai_socktype == -1) {
			ret = EAI_SOCKTYPE;
			goto err;
		}
		if (hint.ai_family) {
			hint.ai_protocol = convert_proto_to_local(hint.ai_family, emhint.ai_protocol);
			if (hint.ai_protocol == -1) {
				ret = EAI_FAMILY;
				goto err;
			}
		} else {
			hint.ai_protocol = 0;
			filter_protocol = emhint.ai_protocol;
		}

		if (emhint.ai_addr) {
			uint32_t ai_addr_2 = emhint.ai_addr;
			if (!_wasmjit_emscripten_check_range(funcinst,
							     ai_addr_2,
							     emhint.ai_addrlen)) {
				ret = EAI_SYSTEM;
				errno = EFAULT;
				goto err;
			}

#ifdef SAME_SOCKADDR
			hint.ai_addrlen = emhint.ai_addrlen;
			hint.ai_addr = (struct sockaddr *) (base + ai_addr_2);
#else
			size_t ss_size;

			if (read_sockaddr(funcinst, &ss, &ss_size, ai_addr_2, emhint.ai_addrlen)) {
				ret = EAI_SYSTEM;
				errno = EFAULT;
				goto err;
			}
			hint.ai_addrlen = ss_size;
			hint.ai_addr = (struct sockaddr *) &ss;
#endif
		} else {
			hint.ai_addrlen = 0;
			hint.ai_addr = NULL;
		}

		if (emhint.ai_canonname) {
			if (!_wasmjit_emscripten_check_string(funcinst, emhint.ai_canonname, PATH_MAX)) {
				ret = EAI_SYSTEM;
				errno = EFAULT;
				goto err;
			}

			hint.ai_canonname = base + emhint.ai_canonname;
		} else {
			hint.ai_canonname = NULL;
		}

		/* hints shouldn't have ai_next set */
		if (emhint.ai_next) {
			ret = EAI_SYSTEM;
			errno = EINVAL;
			goto err;
		}

		hint.ai_next = NULL;

		hintp = &hint;
	} else {
		hintp = NULL;
	}

	_wasmjit_block_signals(&set);

	ret = getaddrinfo(base + node, base + service, hintp, &res);
	if (ret) {
	err:
		if (ret == EAI_SYSTEM) {
			assert(errno >= 0);
			wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
		}
	} else {
		uint32_t outp = out;
		struct addrinfo *resp = res;
		while (resp) {
			/* allocate an em_addrinfo */
			struct em_addrinfo towrite;
			uint32_t ai;
			int32_t ai_family, ai_protocol;

			ai_family = convert_socket_domain_to_em(resp->ai_family);
			if (ai_family == -1) {
				break;
			}

			ai_protocol = convert_proto_to_em(ai_family, resp->ai_protocol);
			if (ai_protocol == -1) {
				break;
			}

			if (filter_protocol && ai_protocol != filter_protocol) {
				resp = resp->ai_next;
				continue;
			}

			ai = getMemory(funcinst, sizeof(towrite));
			if (!ai) {
				break;
			}

			towrite.ai_flags = back_convert_ai_flags(resp->ai_flags);
			towrite.ai_family = ai_family;
			towrite.ai_socktype = convert_socket_type_to_em(resp->ai_socktype);
			if (towrite.ai_socktype == -1) {
				break;
			}
			towrite.ai_protocol = ai_protocol;

			if (resp->ai_canonname) {
				size_t canonlen = strlen(resp->ai_canonname) + 1;
				towrite.ai_canonname = getMemory(funcinst, canonlen);
				if (!towrite.ai_canonname) {
					break;
				}
				memcpy(base + towrite.ai_canonname,
				       resp->ai_canonname,
				       canonlen);
			} else {
				towrite.ai_canonname = 0;
			}

#ifdef SAME_SOCKADDR
			towrite.ai_addr = getMemory(funcinst, resp->ai_addrlen);
			if (!towrite.ai_addr) {
				break;
			}

			memcpy(base + towrite.ai_addr, resp->ai_addr, resp->ai_addrlen);
			towrite.ai_addrlen = uint32_t_swap_bytes(resp->ai_addrlen);
#else
			if (resp->ai_family == AF_INET) {
				towrite.ai_addrlen = 16;
			} else if (resp->ai_family == AF_INET6) {
				towrite.ai_addrlen = 28;
			} else if (resp->ai_family == AF_UNIX) {
				towrite.ai_addrlen =
					2 + resp->ai_addrlen -
					offsetof(struct sockaddr_un, sun_path);
			} else {
				/* TODO: add support for more sockaddr types */
				break;
			}

			towrite.ai_addr = getMemory(funcinst, towrite.ai_addrlen);
			if (!towrite.ai_addr) {
				break;
			}

			write_sockaddr(resp->ai_addr, resp->ai_addrlen,
				       base + towrite.ai_addr, towrite.ai_addrlen,
				       &towrite.ai_addrlen);
#endif

			towrite.ai_flags = uint32_t_swap_bytes(towrite.ai_flags);
			towrite.ai_family = uint32_t_swap_bytes(towrite.ai_family);
			towrite.ai_socktype = uint32_t_swap_bytes(towrite.ai_socktype);
			towrite.ai_protocol = uint32_t_swap_bytes(towrite.ai_protocol);
			towrite.ai_addr = uint32_t_swap_bytes(towrite.ai_addr);
			towrite.ai_canonname = uint32_t_swap_bytes(towrite.ai_canonname);
			towrite.ai_next = 0;

			memcpy(base + ai, &towrite, sizeof(towrite));

			{
				uint32_t ai2 = uint32_t_swap_bytes(ai);
				memcpy(base + outp, &ai2, sizeof(ai2));
			}

			outp = ai + offsetof(struct em_addrinfo, ai_next);
			resp = resp->ai_next;
		}

		freeaddrinfo(res);
		if (resp) {
			wasmjit_emscripten_internal_abort("failed to allocate memory");
		}
	}

	_wasmjit_unblock_signals(&set);

	return convert_getaddrinfo_return(ret);
}

uint32_t wasmjit_emscripten__gai_strerror(uint32_t errcode,
					  struct FuncInst *funcinst)
{
#define GAI_STRERROR_BUF_SIZE 256

	const char *msg;
	char *base;
	int sys_errcode;
	wasmjit_signal_block_ctx set;
	uint32_t ret;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	/* NB: also protect setting ctx->gai_strerror_buffer */
	_wasmjit_block_signals(&set);

	if (!ctx->gai_strerror_buffer) {
		ctx->gai_strerror_buffer = getMemory(funcinst, GAI_STRERROR_BUF_SIZE);
		if (!ctx->gai_strerror_buffer) {
			wasmjit_emscripten_internal_abort("couldn't allocate gai_strerror buffer");
		}
	}

	sys_errcode = back_convert_getaddrinfo_return(errcode);

	msg = gai_strerror(sys_errcode);

	base = wasmjit_emscripten_get_base_address(funcinst);
	strncpy(base + ctx->gai_strerror_buffer, msg, GAI_STRERROR_BUF_SIZE);
	(base + ctx->gai_strerror_buffer)[GAI_STRERROR_BUF_SIZE - 1] = '\0';

	ret = ctx->gai_strerror_buffer;

	_wasmjit_unblock_signals(&set);

	return ret;
}

uint32_t wasmjit_emscripten__getenv(uint32_t name,
				    struct FuncInst *funcinst)
{
	char *base, *env;
	uint32_t ret;
	wasmjit_signal_block_ctx set;

	if (!_wasmjit_emscripten_check_string(funcinst, name, PATH_MAX)) {
		return 0;
	}

	base = wasmjit_emscripten_get_base_address(funcinst);

	/* NB: also protect setting ctx->getenv_buffer */
	_wasmjit_block_signals(&set);

	env = getenv(base + name);

	if (env) {
		struct EmscriptenContext *ctx =
			_wasmjit_emscripten_get_context(funcinst);
		if (ctx->getenv_buffer) {
			freeMemory(ctx, ctx->getenv_buffer);
		}
		ret = ctx->getenv_buffer = getMemory(funcinst, strlen(env) + 1);
		if (ret) {
			memcpy(base + ret, env, strlen(env) + 1);
		}
	} else {
		ret = 0;
	}

	_wasmjit_unblock_signals(&set);

	return ret;
}

void wasmjit_emscripten__endgrent(struct FuncInst *funcinst)
{
	(void) funcinst;
	endgrent();
}

struct em_group {
	uint32_t gr_name;
	uint32_t gr_passwd;
	em_gid_t gr_gid;
	uint32_t gr_mem;
};

static uint32_t convert_group(struct FuncInst *funcinst, struct group *gr)
{
	char *base;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (ctx->getgrent_buffer) {
		freeMemory(ctx, ctx->getgrent_buffer);
		ctx->getgrent_buffer = 0;
	}

	{
		uint32_t string_offset, ptr_offset;
		size_t num_groups = 0, total_string_size = 0;
		char **gr_mem_iter;
		for (gr_mem_iter = gr->gr_mem; *gr_mem_iter; ++gr_mem_iter) {
			total_string_size += strlen(*gr_mem_iter) + 1;
			num_groups += 1;
		}

		total_string_size += strlen(gr->gr_name) + 1;
		total_string_size += strlen(gr->gr_passwd) + 1;

		ctx->getgrent_buffer = getMemory(funcinst, sizeof(struct em_group) + (num_groups + 1) * 4 + total_string_size);
		if (!ctx->getgrent_buffer) {
			wasmjit_emscripten____setErrNo(EM_ENOMEM, funcinst);
			return 0;
		}

		ptr_offset = ctx->getgrent_buffer + sizeof(struct em_group);
		string_offset = ctx->getgrent_buffer + sizeof(struct em_group) + (num_groups + 1) * 4;

		{
			uint32_t swapped_ptr_offset = uint32_t_swap_bytes(ptr_offset);
			memcpy(base + ctx->getgrent_buffer + offsetof(struct em_group, gr_mem),
			       &swapped_ptr_offset,
			       sizeof(swapped_ptr_offset));
		}

		for (gr_mem_iter = gr->gr_mem; *gr_mem_iter; ++gr_mem_iter) {
			uint32_t swapped_string_offset = uint32_t_swap_bytes(string_offset);
			size_t group_size = strlen(*gr_mem_iter) + 1;
			memcpy(base + ptr_offset, &swapped_string_offset, sizeof(swapped_string_offset));
			memcpy(base + string_offset, *gr_mem_iter, group_size);
			ptr_offset += 4;
			string_offset += group_size;
		}

		/* add null terminator */
		memset(base + ptr_offset, 0, 4);

		{
			uint32_t swapped_string_offset = uint32_t_swap_bytes(string_offset);
			size_t name_size = strlen(gr->gr_name) + 1;
			memcpy(base + ctx->getgrent_buffer + offsetof(struct em_group, gr_name),
			       &swapped_string_offset,
			       sizeof(swapped_string_offset));
			memcpy(base + string_offset, gr->gr_name, name_size);
			string_offset += name_size;
		}

		{
			uint32_t swapped_string_offset = uint32_t_swap_bytes(string_offset);
			memcpy(base + ctx->getgrent_buffer + offsetof(struct em_group, gr_passwd),
			       &swapped_string_offset,
			       sizeof(swapped_string_offset));
			memcpy(base + string_offset, gr->gr_passwd, strlen(gr->gr_passwd) + 1);
		}

		{
			uint32_t swapped_gid = uint32_t_swap_bytes((uint32_t) gr->gr_gid);
			memcpy(base + ctx->getgrent_buffer + offsetof(struct em_group, gr_gid),
			       &swapped_gid,
			       sizeof(swapped_gid));
		}
	}

	return ctx->getgrent_buffer;
}

uint32_t wasmjit_emscripten__getgrent(struct FuncInst *funcinst)
{
	struct group *gr;
	wasmjit_signal_block_ctx set;
	uint32_t ret;

	/* NB: also protect setting ctx->getgrent_buffer */
	_wasmjit_block_signals(&set);

	errno = 0;
	gr = getgrent();
	if (!gr) {
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
		return 0;
	}

	ret = convert_group(funcinst, gr);

	_wasmjit_unblock_signals(&set);

	return ret;
}

uint32_t wasmjit_emscripten__getgrnam(uint32_t name,
				      struct FuncInst *funcinst)
{
	struct group *gr;
	char *base;
	wasmjit_signal_block_ctx set;
	uint32_t ret;

	if (!_wasmjit_emscripten_check_string(funcinst, name, PATH_MAX)) {
		return 0;
	}

	base = wasmjit_emscripten_get_base_address(funcinst);

	/* NB: also protect setting ctx->getgrent_buffer */
	_wasmjit_block_signals(&set);

	errno = 0;
	gr = getgrnam(base + name);
	if (!gr) {
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
		return 0;
	}

	ret = convert_group(funcinst, gr);

	_wasmjit_unblock_signals(&set);

	return ret;
}

uint32_t wasmjit_emscripten__getpagesize(struct FuncInst *funcinst) {
	(void) funcinst;
	return getpagesize();
}

struct em_passwd {
	uint32_t pw_name;
	uint32_t pw_passwd;
	em_uid_t pw_uid;
	em_gid_t pw_gid;
	uint32_t pw_gecos;
	uint32_t pw_dir;
	uint32_t pw_shell;
};

static void write_uint32_t(char *base, uint32_t n)
{
	n = uint32_t_swap_bytes(n);
	memcpy(base, &n, sizeof(n));
}

static uint32_t convert_passwd(struct FuncInst *funcinst, struct passwd *pw)
{
	uint32_t string_offset;
	size_t name_sz, passwd_sz, gecos_sz, dir_sz, shell_sz, total_string_size = 0;
	char *base;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (ctx->getpwent_buffer) {
		freeMemory(ctx, ctx->getpwent_buffer);
		ctx->getpwent_buffer = 0;
	}

	total_string_size += (name_sz = strlen(pw->pw_name) + 1);
	total_string_size += (passwd_sz = strlen(pw->pw_passwd) + 1);
	total_string_size += (gecos_sz = strlen(pw->pw_gecos) + 1);
	total_string_size += (dir_sz = strlen(pw->pw_dir) + 1);
	total_string_size += (shell_sz = strlen(pw->pw_shell) + 1);

	ctx->getpwent_buffer = getMemory(funcinst, sizeof(struct em_passwd) + total_string_size);
	if (!ctx->getpwent_buffer) {
		wasmjit_emscripten____setErrNo(EM_ENOMEM, funcinst);
		return 0;
	}

	string_offset = ctx->getpwent_buffer + sizeof(struct em_passwd);

	write_uint32_t(base + ctx->getpwent_buffer +
		       offsetof(struct em_passwd, pw_name),
		       string_offset);
	memcpy(base + string_offset, pw->pw_name, name_sz);
	string_offset += name_sz;

	write_uint32_t(base + ctx->getpwent_buffer +
		       offsetof(struct em_passwd, pw_passwd),
		       string_offset);
	memcpy(base + string_offset, pw->pw_passwd, passwd_sz);
	string_offset += passwd_sz;

	write_uint32_t(base + ctx->getpwent_buffer +
		       offsetof(struct em_passwd, pw_gecos),
		       string_offset);
	memcpy(base + string_offset, pw->pw_gecos, gecos_sz);
	string_offset += gecos_sz;

	write_uint32_t(base + ctx->getpwent_buffer +
		       offsetof(struct em_passwd, pw_dir),
		       string_offset);
	memcpy(base + string_offset, pw->pw_dir, dir_sz);
	string_offset += dir_sz;

	write_uint32_t(base + ctx->getpwent_buffer +
		       offsetof(struct em_passwd, pw_shell),
		       string_offset);
	memcpy(base + string_offset, pw->pw_shell, shell_sz);
	string_offset += shell_sz;


	write_uint32_t(base + ctx->getpwent_buffer +
		       offsetof(struct em_passwd, pw_uid),
		       pw->pw_uid);

	write_uint32_t(base + ctx->getpwent_buffer +
		       offsetof(struct em_passwd, pw_gid),
		       pw->pw_gid);

	return ctx->getpwent_buffer;
}

uint32_t wasmjit_emscripten__getpwnam(uint32_t name,
				      struct FuncInst *funcinst)
{
	struct passwd *pw;
	char *base;
	uint32_t ret;
	wasmjit_signal_block_ctx set;

	if (!_wasmjit_emscripten_check_string(funcinst, name, PATH_MAX)) {
		return 0;
	}

	base = wasmjit_emscripten_get_base_address(funcinst);

	/* NB: also protect setting ctx->getpwent_buffer */
	_wasmjit_block_signals(&set);

	errno = 0;
	pw = getpwnam(base + name);
	if (!pw) {
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
		return 0;
	}

	ret = convert_passwd(funcinst, pw);

	_wasmjit_unblock_signals(&set);

	return ret;
}

uint32_t wasmjit_emscripten__gettimeofday(uint32_t emtv, uint32_t emtz,
					  struct FuncInst *funcinst)
{
	char *base;
	struct timeval tv;
	struct timezone tz;
	long rret;
	wasmjit_signal_block_ctx set;
	uint32_t ret;

	if (!_wasmjit_emscripten_check_range(funcinst, emtv, sizeof(struct em_timeval)))
		return -EM_EFAULT;

	if (!_wasmjit_emscripten_check_range(funcinst, emtz, sizeof(struct em_timezone)))
		return -EM_EFAULT;

	base = wasmjit_emscripten_get_base_address(funcinst);

	_wasmjit_block_signals(&set);

	rret = sys_gettimeofday(&tv, &tz);
	if (rret < 0) {
		goto err;
	} else {
		struct em_timezone emtzl;

		if (!write_timeval(base + emtv, &tv)) {
			rret = -EM_EOVERFLOW;
			goto err;
		}

#if __INT_WIDTH__ > 32
		if (tz.tz_minuteswest > INT32_MAX ||
		    tz.tz_minuteswest < INT32_MIN ||
		    tz.tz_dsttime > INT32_MAX ||
		    tz.tz_dsttime < INT32_MIN) {
			rret = -EM_EOVERFLOW;
			goto err;
		}
#endif

		emtzl.tz_minuteswest = int32_t_swap_bytes((int32_t) tz.tz_minuteswest);
		emtzl.tz_dsttime = int32_t_swap_bytes((int32_t) tz.tz_dsttime);

		memcpy(base + emtz, &emtzl, sizeof(emtzl));
	}

	ret = 0;

	if (0) {
	err:
		ret = -1;
		wasmjit_emscripten____setErrNo(convert_errno(-rret), funcinst);
	}

	_wasmjit_unblock_signals(&set);

	return ret;
}

static uint32_t time_exploder(struct tm *(*exploder)(const time_t *, struct tm *),
			      uint32_t timePtr,
			      uint32_t tmPtr,
			      struct FuncInst *funcinst)
{
	char *base;
	em_time_t em_time;
	time_t time_;
	struct tm tm_, *sys_tmPtr;
	struct em_tm em_tm_;
	wasmjit_signal_block_ctx set;
	uint32_t ret;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	_wasmjit_block_signals(&set);

	if (_wasmjit_emscripten_copy_from_user(funcinst, &em_time, timePtr, sizeof(em_time))) {
		errno = EFAULT;
		goto err;
	}

	if (!_wasmjit_emscripten_check_range(funcinst, tmPtr, sizeof(struct em_tm))) {
		errno = EFAULT;
		goto err;
	}

	time_ = em_time;

	sys_tmPtr = exploder(&time_, &tm_);
	if (!sys_tmPtr) {
		goto err;
	}

#define p(n)								\
	if (OVERFLOWSN(tm_.tm_ ## n, sizeof(em_tm_.tm_ ## n))) {	\
		errno = EOVERFLOW;					\
		goto err;						\
	}								\
	em_tm_.tm_ ## n = int32_t_swap_bytes(tm_.tm_ ## n)

	p(sec);
	p(min);
	p(hour);
	p(mday);
	p(mon);
	p(year);
	p(wday);
	p(yday);
	p(isdst);
	p(gmtoff);

#undef p

	base = wasmjit_emscripten_get_base_address(funcinst);

	if (ctx->tmzone_buffer) {
		freeMemory(ctx, ctx->tmzone_buffer);
		ctx->tmzone_buffer = 0;
	}

	ctx->tmzone_buffer = getMemory(funcinst, strlen(tm_.tm_zone) + 1);
	if (!ctx->tmzone_buffer) {
		errno = ENOMEM;
		goto err;
	}

	memcpy(base + ctx->tmzone_buffer, tm_.tm_zone, strlen(tm_.tm_zone) + 1);
	em_tm_.tm_zone = uint32_t_swap_bytes(ctx->tmzone_buffer);

	memcpy(base + tmPtr, &em_tm_, sizeof(em_tm_));

	ret = tmPtr;

	if (0) {
	err:
		ret = 0;
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
	}

	_wasmjit_unblock_signals(&set);

	return ret;
}

uint32_t wasmjit_emscripten__gmtime_r(uint32_t timePtr,
				      uint32_t tmPtr,
				      struct FuncInst *funcinst)
{
	return time_exploder(&gmtime_r, timePtr, tmPtr, funcinst);
}

uint32_t wasmjit_emscripten__time(uint32_t timePtr, struct FuncInst *funcinst)
{
	time_t ret;
	em_time_t em_ret;

	ret = time(NULL);
	if (ret == (time_t) -1)
		goto err;

	if (OVERFLOWSN(ret, sizeof(em_ret))) {
		errno = EOVERFLOW;
		goto err;
	}

	em_ret = int32_t_swap_bytes(ret);
	if (timePtr &&
	    _wasmjit_emscripten_copy_to_user(funcinst, timePtr, &em_ret, sizeof(em_ret))) {
		errno = EFAULT;
		goto err;
	}

	return (int32_t) ret;

 err:
	wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
	return -1;
}

uint32_t wasmjit_emscripten__kill(uint32_t pid, uint32_t sig,
				  struct FuncInst *funcinst)
{
	long rret;
	rret = sys_kill(pid, sig);
	if (rret < 0) {
		wasmjit_emscripten____setErrNo(convert_errno(-rret), funcinst);
		rret = -1;
	}
	return (int32_t) rret;
}

static void _stackRestore(struct FuncInst *funcinst, uint32_t foo) {
	union ValueUnion input;
	struct FuncInst *callfuncinst;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	callfuncinst = wasmjit_get_export(ctx->asm_, "stackRestore",
					       IMPORT_DESC_TYPE_FUNC).func;
	if (!callfuncinst)
		wasmjit_emscripten_internal_abort("stackRestore not available");

	/* check type of function */
	{
		struct FuncType functype;
		wasmjit_valtype_t input_types[] = {VALTYPE_I32};
		wasmjit_valtype_t return_types[] = {};

		_wasmjit_create_func_type(&functype,
					  ARRAY_LEN(input_types), input_types,
					  ARRAY_LEN(return_types), return_types);

		if (!wasmjit_typecheck_func(&functype, callfuncinst))
			wasmjit_emscripten_internal_abort("stackRestore had back functype");
	}

	input.i32 = foo;
	if (wasmjit_invoke_function(callfuncinst, &input, NULL))
		wasmjit_emscripten_internal_abort("failed to invoke stackRestore");
}

static uint32_t _stackSave(struct FuncInst *funcinst) {
	union ValueUnion output;
	struct FuncInst *callfuncinst;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);

	callfuncinst = wasmjit_get_export(ctx->asm_, "stackSave",
					       IMPORT_DESC_TYPE_FUNC).func;
	if (!callfuncinst)
		wasmjit_emscripten_internal_abort("stackSave not available");

	{
		struct FuncType functype;
		wasmjit_valtype_t input_types[] = {};
		wasmjit_valtype_t return_types[] = {VALTYPE_I32};

		_wasmjit_create_func_type(&functype,
					  ARRAY_LEN(input_types), input_types,
					  ARRAY_LEN(return_types), return_types);

		if (!wasmjit_typecheck_func(&functype, callfuncinst))
			wasmjit_emscripten_internal_abort("stackSave had back functype");
	}

	if (wasmjit_invoke_function(callfuncinst, NULL, &output))
		wasmjit_emscripten_internal_abort("failed to invoke stackSave");

	return output.i32;
}

void wasmjit_emscripten__llvm_stackrestore(uint32_t p,
					   struct FuncInst *funcinst)
{
	uint32_t foo;
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);
	void *newsavedstacks;
	wasmjit_signal_block_ctx set;

	_wasmjit_block_signals(&set);

	if (p >= ctx->LLVM_SAVEDSTACKS_sz)
		wasmjit_emscripten_internal_abort("bad stack restore index");

	p = wasmjit_array_index_nospec(p, 1, ctx->LLVM_SAVEDSTACKS_sz);
	foo = ctx->LLVM_SAVEDSTACKS[p];

	/* splice out p */
	memmove(&ctx->LLVM_SAVEDSTACKS[p], &ctx->LLVM_SAVEDSTACKS[p + 1],
		(ctx->LLVM_SAVEDSTACKS_sz - (p + 1)) * sizeof(ctx->LLVM_SAVEDSTACKS[p]));
	ctx->LLVM_SAVEDSTACKS_sz -= 1;
	newsavedstacks = realloc(ctx->LLVM_SAVEDSTACKS,
				 ctx->LLVM_SAVEDSTACKS_sz * sizeof(ctx->LLVM_SAVEDSTACKS[p]));
	if (!newsavedstacks)
		wasmjit_emscripten_internal_abort("failed to realloc LLVM_SAVEDSTACKS");

	ctx->LLVM_SAVEDSTACKS = newsavedstacks;

	_wasmjit_unblock_signals(&set);

	_stackRestore(funcinst, foo);
}

uint32_t wasmjit_emscripten__llvm_stacksave(struct FuncInst *funcinst)
{
	struct EmscriptenContext *ctx =
		_wasmjit_emscripten_get_context(funcinst);
	void *newsavedstacks;
	uint32_t foo;
	wasmjit_signal_block_ctx set;
	uint32_t ret;

	foo = _stackSave(funcinst);

	_wasmjit_block_signals(&set);

	ctx->LLVM_SAVEDSTACKS_sz += 1;

	newsavedstacks = realloc(ctx->LLVM_SAVEDSTACKS,
				 ctx->LLVM_SAVEDSTACKS_sz * sizeof(ctx->LLVM_SAVEDSTACKS[0]));
	if (!newsavedstacks)
		wasmjit_emscripten_internal_abort("failed to realloc LLVM_SAVEDSTACKS");

	ctx->LLVM_SAVEDSTACKS = newsavedstacks;
	ctx->LLVM_SAVEDSTACKS[ctx->LLVM_SAVEDSTACKS_sz - 1] = foo;

	ret = ctx->LLVM_SAVEDSTACKS_sz - 1;

	_wasmjit_unblock_signals(&set);

	return ret;
}

uint32_t wasmjit_emscripten__localtime_r(uint32_t timePtr, uint32_t tmPtr,
					 struct FuncInst *funcinst)
{
	return time_exploder(&localtime_r, timePtr, tmPtr, funcinst);
}

uint32_t wasmjit_emscripten__localtime(uint32_t timePtr,
				       struct FuncInst *funcinst)
{
	struct EmscriptenContext *ctx = _wasmjit_emscripten_get_context(funcinst);
	wasmjit_signal_block_ctx set;
	uint32_t ret;

	_wasmjit_block_signals(&set);

	if (!ctx->tmtm_buffer) {
		ctx->tmtm_buffer = getMemory(funcinst, sizeof(struct em_tm));
		if (!ctx->tmtm_buffer) {
			errno = ENOMEM;
			goto err;
		}
	}

	ret = wasmjit_emscripten__localtime_r(timePtr, ctx->tmtm_buffer, funcinst);

	if (0) {
	err:
		ret = 0;
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
	}

	_wasmjit_unblock_signals(&set);

	return ret;
}

uint32_t wasmjit_emscripten__mktime(uint32_t tmPtr,
				    struct FuncInst *funcinst)
{
	struct em_tm emtm;
	struct tm tm;
	time_t toret;
	wasmjit_signal_block_ctx set;

	if (_wasmjit_emscripten_copy_from_user(funcinst, &emtm, tmPtr, sizeof(emtm))) {
		errno = EFAULT;
		goto err;
	}

	memset(&tm, 0, sizeof(tm));

#define p(n) tm.tm_ ## n = int32_t_swap_bytes(emtm.tm_ ## n)

	p(sec);
	p(min);
	p(hour);
	p(mday);
	p(mon);
	p(year);
	p(wday);
	p(yday);
	p(isdst);
	p(gmtoff);

	_wasmjit_block_signals(&set);
	toret = mktime(&tm);
	_wasmjit_unblock_signals(&set);

	if (OVERFLOWS(toret)) {
		errno = EOVERFLOW;
		goto err;
	}

	return (int32_t) toret;

#undef p

 err:
	wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
	return 0;
}

uint32_t wasmjit_emscripten__raise(uint32_t sig,
				   struct FuncInst *funcinst)
{
	int ret;
	ret = raise(sig);
	if (ret < 0) {
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
	}
	return (int32_t) ret;
}

uint32_t wasmjit_emscripten__sched_yield(struct FuncInst *funcinst)
{
	long rret;

	/* NB: we assume this is async-signal-safe because it wouldn't make
	   sense otherwise */
	rret = sys_sched_yield();
	if (rret < 0) {
		wasmjit_emscripten____setErrNo(convert_errno(-rret), funcinst);
		rret = -1;
	}
	return (int32_t) rret;
}

typedef struct {
	em_int __val[4*sizeof(em_long)/sizeof(em_int)];
} em_sem_t;

uint32_t wasmjit_emscripten__sem_init(uint32_t sem,
				      uint32_t pshared,
				      uint32_t value,
				      struct FuncInst *funcinst)
{
	char *base;
	sem_t *real_sem = NULL;
	size_t idx;
	int sem_ret;
	struct EmscriptenContext *ctx = _wasmjit_emscripten_get_context(funcinst);
	wasmjit_signal_block_ctx set;
	int32_t ret;

	_wasmjit_block_signals(&set);

	/* cross-process semaphores can't be supported because we don't
	   know where to allocate cross-process memory */
	if (pshared) {
		errno = ENOSYS;
		goto err;
	}

	if (!_wasmjit_emscripten_check_range(funcinst, sem, sizeof(em_sem_t))) {
		errno = EFAULT;
		goto err;
	}

	/* allocate a semaphore */
	real_sem = calloc(1, sizeof(sem_t));
	if (!real_sem) {
		errno = ENOSYS;
		goto err;
	}

	sem_ret = sem_init(real_sem, 0, value);
	if (sem_ret) {
		goto err;
	}

	/* find a place in the sem table for the real sem */
	for (idx = 0; idx < ctx->sem_table.n_elts; ++idx) {
		if (!ctx->sem_table.elts[idx].real_sem) {
			break;
		}
	}

	if (idx == ctx->sem_table.n_elts) {
		/* no space available, expand semaphore table */
		if (!VECTOR_GROW(&ctx->sem_table, 1)) {
			errno = ENOMEM;
			goto err;
		}
	}

	ctx->sem_table.elts[idx].user_addr = sem;
	ctx->sem_table.elts[idx].real_sem = real_sem;

	base = wasmjit_emscripten_get_base_address(funcinst);
	assert(sizeof(idx) <= sizeof(em_sem_t));
	memcpy(base + sem, &idx, sizeof(idx));

	ret = 0;

	if (0) {
	err:
		free(real_sem);
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
		ret = (int32_t) -1;
	}

	_wasmjit_unblock_signals(&set);

	return ret;
}

static uint32_t wasmjit_emscripten_sem_op(uint32_t sem,
					  struct FuncInst *funcinst,
					  int (*sem_op)(sem_t *))
{
	size_t idx;
	int sem_ret;
	struct EmscriptenContext *ctx = _wasmjit_emscripten_get_context(funcinst);

	if (!_wasmjit_emscripten_check_range(funcinst, sem, sizeof(em_sem_t))) {
		errno = EFAULT;
		goto err;
	}

	assert(sizeof(idx) <= sizeof(em_sem_t));
	_wasmjit_memcpy_from_user(funcinst, &idx, sem, sizeof(idx));

	if (idx >= ctx->sem_table.n_elts) {
		errno = EINVAL;
		goto err;
	}

	idx = wasmjit_array_index_nospec(idx, 1, ctx->sem_table.n_elts);

	if (ctx->sem_table.elts[idx].user_addr != sem) {
		errno = EINVAL;
		goto err;
	}

	assert(ctx->sem_table.elts[idx].real_sem);

	sem_ret = sem_op(ctx->sem_table.elts[idx].real_sem);
	if (sem_ret)
		goto err;

	return 0;

 err:
	wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
	return (int32_t) -1;
}

uint32_t wasmjit_emscripten__sem_post(uint32_t sem,
				      struct FuncInst *funcinst)
{
	return wasmjit_emscripten_sem_op(sem, funcinst, &sem_post);
}

uint32_t wasmjit_emscripten__sem_wait(uint32_t sem,
				      struct FuncInst *funcinst)
{
	/* sem_wait is not guaranteed to be async-signal-safe but since
	   it can block for arbitrarily long, we don't block signals */
	return wasmjit_emscripten_sem_op(sem, funcinst, &sem_wait);
}

void wasmjit_emscripten__setgrent(struct FuncInst *funcinst)
{
	wasmjit_signal_block_ctx set;

	(void) funcinst;

	_wasmjit_block_signals(&set);
	(void) setgrent();
	_wasmjit_unblock_signals(&set);
}

uint32_t wasmjit_emscripten__setgroups(uint32_t ngroups,
				       uint32_t gidset,
				       struct FuncInst *funcinst)
{
	uint32_t emi;
	int32_t ret;
	gid_t *sys_gidset = NULL;
	long rret;
	size_t range;
	wasmjit_signal_block_ctx set;

	_wasmjit_block_signals(&set);

	if (__builtin_mul_overflow(ngroups,
				   sizeof(em_gid_t),
				   &range)) {
		errno = EFAULT;
		goto err;
	}

	if (!_wasmjit_emscripten_check_range(funcinst, gidset, range)) {
		errno = EFAULT;
		goto err;
	}

	sys_gidset = calloc(ngroups, sizeof(gid_t));
	if (!sys_gidset) {
		goto err;
	}

	for (emi = 0; emi < ngroups; ++emi) {
		em_gid_t emgid;
		_wasmjit_memcpy_from_user(funcinst, &emgid,
					  gidset + emi * sizeof(em_gid_t), sizeof(emgid));
		sys_gidset[emi] = uint32_t_swap_bytes(emgid);
	}

	rret = sys_setgroups(ngroups, sys_gidset);
	if (rret < 0) {
		errno = -rret;
		goto err;
	}

	ret = 0;

	if (0) {
 err:
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
		ret = -1;
	}

	free(sys_gidset);

	_wasmjit_unblock_signals(&set);

	return ret;
}

#define EM_ITIMER_REAL      0
#define EM_ITIMER_VIRTUAL   1
#define EM_ITIMER_PROF      2

uint32_t wasmjit_emscripten__setitimer(uint32_t which,
				       uint32_t new_value,
				       uint32_t old_value,
				       struct FuncInst *funcinst)
{
	char *base;
	int sys_which;
	long rret;
	struct itimerval sys_new_value_v, sys_old_value_v;
	void *sys_new_value, *sys_old_value;
	wasmjit_signal_block_ctx set;
	int32_t ret;

	_wasmjit_block_signals(&set);

	if (!_wasmjit_emscripten_check_range(funcinst, new_value, sizeof(struct em_itimerval)) ||
	    (old_value &&
	     !_wasmjit_emscripten_check_range(funcinst, old_value, sizeof(struct em_itimerval)))) {
		errno = EFAULT;
		goto err;
	}

	if (EM_ITIMER_REAL == ITIMER_REAL &&
	    EM_ITIMER_VIRTUAL == ITIMER_VIRTUAL &&
	    EM_ITIMER_PROF == ITIMER_PROF) {
#if !IS_LINUX
		if (which != EM_ITIMER_REAL &&
		    which != EM_ITIMER_VIRTUAL &&
		    which != EM_ITIMER_PROF) {
			errno = EINVAL;
			goto err;
		}
#endif
		sys_which = which;
	} else {
		switch (which) {
		case EM_ITIMER_REAL: sys_which = ITIMER_REAL; break;
		case EM_ITIMER_VIRTUAL: sys_which = ITIMER_VIRTUAL; break;
		case EM_ITIMER_PROF: sys_which = ITIMER_PROF; break;
		default: {
			errno = EINVAL;
			goto err;
		}
		}
	}

	base = wasmjit_emscripten_get_base_address(funcinst);

#define PASSTHROUGH (							\
		     __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ &&	\
		     sizeof(struct itimerval) == sizeof(struct em_itimerval) && \
		     sizeof(struct timeval) == sizeof(struct em_timeval) && \
		     offsetof(struct timeval, tv_sec) == offsetof(struct em_timeval, tv_sec) && \
		     offsetof(struct timeval, tv_usec) == offsetof(struct em_timeval, tv_usec) && \
		     1							\
									)
	if (PASSTHROUGH) {
		sys_new_value = base + new_value;
		sys_old_value = old_value ? base + old_value : NULL;
	} else {
		if (!read_timeval(funcinst, &sys_new_value_v.it_interval,
				  new_value + offsetof(struct em_itimerval, it_interval)) ||
		    !read_timeval(funcinst, &sys_new_value_v.it_value,
				  new_value + offsetof(struct em_itimerval, it_value))) {
			errno = EINVAL;
			goto err;
		}
		sys_new_value = &sys_new_value_v;
		sys_old_value = old_value ? &sys_old_value_v : NULL;
	}

	rret = sys_setitimer(sys_which, sys_new_value, sys_old_value);
	if (rret < 0) {
		errno = -rret;
		goto err;
	}

	if (sys_old_value && !PASSTHROUGH) {
		if (!write_timeval(base + old_value + offsetof(struct em_itimerval, it_interval),
				   &sys_old_value_v.it_interval) ||
		    !write_timeval(base + old_value + offsetof(struct em_itimerval, it_value),
				   &sys_old_value_v.it_value)) {
			wasmjit_emscripten_internal_abort("setitimer ovalue overflow!");
		}
	}

	ret = 0;

	if (0) {
	err:
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
		ret = (int32_t) -1;
	}

	_wasmjit_unblock_signals(&set);

	return ret;

#undef PASSTHROUGH
}

#ifdef __KERNEL__

uint32_t wasmjit_emscripten__sigaction(uint32_t signum,
				       uint32_t act,
				       uint32_t oldact,
				       struct FuncInst *funcinst)
{
	(void) signum;
	(void) act;
	(void) oldact;
	(void) funcinst;
	/* TODO: implement */
	wasmjit_emscripten____setErrNo(EM_ENOSYS, funcinst);
	return -1;
}

#else

#define _EM_NSIG (sizeof(em_sigset_t) * CHAR_BIT + 1)

static int em_sigaddset(em_sigset_t *set, em_int sig)
{
	unsigned s = sig-1;
	assert(!(s >= _EM_NSIG-1 || sig-32U < 3));
	set->__bits[s/8/sizeof *set->__bits] |= 1UL<<(s&8*sizeof *set->__bits-1);
	return 0;
}

static int em_sigemptyset(em_sigset_t *set)
{
        set->__bits[0] = 0;
        if (sizeof(long)==4 || _EM_NSIG > 65) set->__bits[1] = 0;
        if (sizeof(long)==4 && _EM_NSIG > 65) {
                set->__bits[2] = 0;
                set->__bits[3] = 0;
        }
        return 0;
}

static int em_sigismember(const em_sigset_t *set, em_int sig)
{
	unsigned s = sig-1;
	assert(!(s >= _EM_NSIG-1));
	return !!(set->__bits[s/8/sizeof *set->__bits] & 1UL<<(s&8*sizeof *set->__bits-1));
}

static int convert_signal(em_int signum)
{
	if (
#define SIG(NAME, NUM) NUM == SIG ## NAME &&
#include <wasmjit/emscripten_runtime_sys_sig_def.h>
#undef SIG
	    1) {
#if !IS_LINUX
		if (
#define SIG(NAME, NUM) signum != NUM &&
#include <wasmjit/emscripten_runtime_sys_sig_def.h>
#undef SIG
		    1) {
			return -1;
		}
#endif
		return signum;
	} else {
		switch (signum) {
#define SIG(NAME, NUM) case NUM: return SIG ## NAME; break;
#include <wasmjit/emscripten_runtime_sys_sig_def.h>
#undef SIG
		default: return -1;
		}
	}
}

static em_int back_convert_signal(int signo)
{
	switch (signo) {
#define SIG(NAME, NUM) case SIG ## NAME: return NUM; break;
#include <wasmjit/emscripten_runtime_sys_sig_def.h>
#undef SIG
	default: return -1;
	}
}

#define EM_SA_NOCLDSTOP  1
#define EM_SA_NOCLDWAIT  2
#define EM_SA_SIGINFO    4
#define EM_SA_ONSTACK    0x08000000
#define EM_SA_RESTART    0x10000000
#define EM_SA_NODEFER    0x40000000
#define EM_SA_RESETHAND  0x80000000
#define EM_SA_RESTORER   0x04000000

static int check_sa_flags(uint32_t sa_flags)
{
	uint32_t all =
		EM_SA_NOCLDSTOP |
		EM_SA_NOCLDWAIT |
		EM_SA_SIGINFO |
		EM_SA_ONSTACK |
		EM_SA_RESTART |
		EM_SA_NODEFER |
		EM_SA_RESETHAND |
		0;

	return !(sa_flags & ~all);
}

static int convert_sa_flags(em_int sa_flags)
{
	int n_sa_flags = 0;

#define p(n)					\
	if (sa_flags & EM_SA_ ## n)		\
		n_sa_flags |= SA_ ## n

	p(NOCLDSTOP);
	p(NOCLDWAIT);
	p(SIGINFO);
	p(ONSTACK);
	p(RESTART);
	p(NODEFER);
	p(RESETHAND);

#undef p

	return n_sa_flags;
}

static em_int back_convert_sa_flags(int sa_flags)
{
	em_int n_sa_flags = 0;

#define p(n)					\
	if (sa_flags & SA_ ## n)		\
		n_sa_flags |= EM_SA_ ## n

	p(NOCLDSTOP);
	p(NOCLDWAIT);
	p(SIGINFO);
	p(ONSTACK);
	p(RESTART);
	p(NODEFER);
	p(RESETHAND);

#undef p

	return n_sa_flags;
}

#define EM_SIG_DFL  ((em_funcptr) 0)
#define EM_SIG_IGN  ((em_funcptr) 1)

static void _wasmjit_emscripten_sigaction_handler(int signum,
						  siginfo_t *sinfo,
						  void *uap)
{
	struct EmscriptenContext *ctx;
	em_funcptr fptr;
	struct FuncInst *funcinst;
	union ValueUnion args[4];
	struct FuncInst *handler_dyncall, *sigaction_dyncall;
	int saved;
	int32_t em_signum;

	saved = errno;

	em_signum = back_convert_signal(signum);

	/* NB: this shouldn't happen */
	assert(em_signum >= 0);

	if (_g_handler_setting) return;
	compiler_barrier();
	ctx = _g_handler_ctx;
	if (!ctx) return;

	handler_dyncall = wasmjit_get_export(ctx->asm_, "dynCall_vi",
					     IMPORT_DESC_TYPE_FUNC).func;

	sigaction_dyncall = wasmjit_get_export(ctx->asm_, "dynCall_viii",
					       IMPORT_DESC_TYPE_FUNC).func;

	if (ctx->sig_handlers[em_signum].is_sigaction) {
		fptr = ctx->sig_handlers[em_signum].handler.em_sa_sigaction;
		funcinst = sigaction_dyncall;
	} else {
		fptr = ctx->sig_handlers[em_signum].handler.em_sa_handler;
		funcinst = handler_dyncall;
	}

	if (!funcinst) return;

	assert(fptr != EM_SIG_IGN);
	assert(fptr != EM_SIG_DFL);

	/* TODO: CONVERT */
	(void) sinfo;
	/* TODO: convert this to some wasm usable representation of cpu state */
	(void) uap;

	args[0].i32 = fptr;
	args[1].i32 = em_signum;
	args[2].i32 = 0;
	args[3].i32 = 0;

	/* if process aborts during signal handler, it will longjmp to
	   to main() invoker */
	(void) wasmjit_invoke_function(funcinst, args, NULL);

	errno = saved;
}

uint32_t wasmjit_emscripten__sigaction(uint32_t signum,
				       uint32_t act,
				       uint32_t oldact,
				       struct FuncInst *funcinst)
{
	long rret;
	int sys_sig;
	struct sigaction sys_act_v, sys_oldact_v,
		*sys_act, *sys_oldact;
	struct em_sigaction act_v;
	size_t i;
	int32_t ret;

	if ((act &&
	     _wasmjit_emscripten_copy_from_user(funcinst, &act_v,
						act, sizeof(struct em_sigaction))) ||
	    (oldact &&
	     !_wasmjit_emscripten_check_range(funcinst, oldact, sizeof(struct em_sigaction)))) {
		errno = EFAULT;
		goto err;
	}

	if (act) {
		/* NB: we can't generally support restorer,
		   and emscripten doesn't use it */
		if (act_v.sa_restorer) {
			errno = EINVAL;
			goto err;
		}

		for (i = 0; i < ARRAY_LEN(act_v.sa_mask.__bits); ++i) {
			act_v.sa_mask.__bits[i] = uint32_t_swap_bytes(act_v.sa_mask.__bits[i]);
		}

		act_v.sa_flags = uint32_t_swap_bytes(act_v.sa_flags);
		if (act_v.sa_flags & EM_SA_SIGINFO) {
			act_v.__sa_handler.em_sa_sigaction =
				uint32_t_swap_bytes(act_v.__sa_handler.em_sa_sigaction);
		} else {
			act_v.__sa_handler.em_sa_handler =
				uint32_t_swap_bytes(act_v.__sa_handler.em_sa_handler);
		}
	}

	sys_sig = convert_signal(signum);
	if (sys_sig < 0) {
		errno = EINVAL;
		goto err;
	}

	if (act) {
		em_int signo;

		/* convert sa_flags */
		if (!check_sa_flags(act_v.sa_flags)) {
			errno = EINVAL;
			goto err;
		}

		sys_act_v.sa_flags = convert_sa_flags(act_v.sa_flags & ~(uint32_t) EM_SA_SIGINFO);

		/* convert sighandler / sigaction:
		     we need to save which function idx to call for which signal in
		     emscripten context */
		if (((act_v.sa_flags & EM_SA_SIGINFO) &&
		     act_v.__sa_handler.em_sa_sigaction == EM_SIG_DFL) ||
		    act_v.__sa_handler.em_sa_handler == EM_SIG_DFL) {
			sys_act_v.sa_handler = SIG_DFL;
		} else if (((act_v.sa_flags & EM_SA_SIGINFO) &&
			    act_v.__sa_handler.em_sa_sigaction == EM_SIG_IGN) ||
			   act_v.__sa_handler.em_sa_handler == EM_SIG_IGN) {
			sys_act_v.sa_handler = SIG_IGN;
		} else {
			sys_act_v.sa_sigaction = _wasmjit_emscripten_sigaction_handler;
			sys_act_v.sa_flags |= SA_SIGINFO;
		}

		/* convert sa_mask */
		sigemptyset(&sys_act_v.sa_mask);
		for (signo = 1; (size_t) signo < sizeof(act_v.sa_mask) * CHAR_BIT; ++signo) {
			if (em_sigismember(&act_v.sa_mask, signo)) {
				int sys_signo = convert_signal(signo);
				/* Nb: if the signal doesn't exist on the host system
				   we don't have to mask it */
				if (sys_signo >= 0) {
					sigaddset(&sys_act_v.sa_mask, sys_signo);
				}
			}
		}

		sys_act = &sys_act_v;
	} else {
		sys_act = NULL;
	}

	sys_oldact = oldact ? &sys_oldact_v : NULL;

	rret = sys_sigaction(sys_sig, sys_act, sys_oldact);
	if (rret < 0) {
		errno = -rret;
		goto err;
	}

	{
		struct em_sigaction oldact_v;
		struct EmscriptenContext *ctx =
			_wasmjit_emscripten_get_context(funcinst);

		if (sys_oldact) {
			em_sigemptyset(&oldact_v.sa_mask);
			for (i = 1; i < sizeof(sys_oldact_v.sa_mask) * CHAR_BIT; ++i) {
				if (sigismember(&sys_oldact_v.sa_mask, i)) {
					int32_t signo = back_convert_signal(i);
					if (signo >= 0) {
						em_sigaddset(&oldact_v.sa_mask, signo);
					}
				}
			}

			oldact_v.sa_flags = back_convert_sa_flags(sys_oldact_v.sa_flags);

			oldact_v.__sa_handler = ctx->sig_handlers[signum].handler;
			if (ctx->sig_handlers[signum].is_sigaction) {
				oldact_v.sa_flags |= EM_SA_SIGINFO;
			}

			oldact_v.sa_restorer = 0;
		}

		/* write out oldact_v to memory */
		if (sys_oldact) {
			char *base;
			base = wasmjit_emscripten_get_base_address(funcinst);

			if (oldact_v.sa_flags & EM_SA_SIGINFO) {
				oldact_v.__sa_handler.em_sa_sigaction =
					uint32_t_swap_bytes(oldact_v.__sa_handler.em_sa_sigaction);
			} else {
				oldact_v.__sa_handler.em_sa_sigaction =
					uint32_t_swap_bytes(oldact_v.__sa_handler.em_sa_sigaction);
			}

			oldact_v.sa_flags = uint32_t_swap_bytes(oldact_v.sa_flags);

			for (i = 0; i < ARRAY_LEN(oldact_v.sa_mask.__bits); ++i) {
				oldact_v.sa_mask.__bits[i] = uint32_t_swap_bytes(oldact_v.sa_mask.__bits[i]);
			}

			memcpy(base + oldact, &oldact_v, sizeof(oldact_v));
		}

		if (sys_act) {
			ctx->sig_handlers[signum].is_sigaction =
				!!(act_v.sa_flags & EM_SA_SIGINFO);
			ctx->sig_handlers[signum].handler =
				act_v.__sa_handler;
		}
	}

	ret = 0;

	if (0) {
	err:
		wasmjit_emscripten____setErrNo(convert_errno(errno), funcinst);
		ret = (int32_t) -1;
	}

	return ret;
}

#endif

uint32_t wasmjit_emscripten____syscall29(uint32_t which,
					 uint32_t varargs,
					 struct FuncInst *funcinst)
{
	(void) which;
	(void) varargs;
	(void) funcinst;
	return check_ret(sys_pause());
}

void wasmjit_emscripten_cleanup(struct ModuleInst *moduleinst) {
	(void)moduleinst;
	/* TODO: implement */
	remove_signal_context();
}

struct EmscriptenContext *wasmjit_emscripten_get_context(struct ModuleInst *module_inst)
{
	return module_inst->private_data;
}

#define alignMemory(size, factor) \
	(((size) % (factor)) ? ((size) - ((size) % (factor)) + (factor)) : (size))

void wasmjit_emscripten_start_func(struct FuncInst *funcinst)
{
	struct MemInst *meminst;
	struct GlobalInst *DYNAMICTOP_PTR, *STACK_MAX;
	uint32_t DYNAMIC_BASE;
	uint32_t copy_user_res;

	/* fill DYNAMIC_BASE */

	if (funcinst->module_inst->mems.n_elts < 1)
		wasmjit_emscripten_internal_abort("no memory");

	meminst = funcinst->module_inst->mems.elts[0];
	assert(meminst);

	DYNAMICTOP_PTR =
		wasmjit_get_export(funcinst->module_inst, "DYNAMICTOP_PTR",
				   IMPORT_DESC_TYPE_GLOBAL).global;
	assert(DYNAMICTOP_PTR && DYNAMICTOP_PTR->value.type == VALTYPE_I32);

	STACK_MAX =
		wasmjit_get_export(funcinst->module_inst, "STACK_MAX",
				   IMPORT_DESC_TYPE_GLOBAL).global;
	assert(STACK_MAX && STACK_MAX->value.type == VALTYPE_I32);

	DYNAMIC_BASE = alignMemory(STACK_MAX->value.data.i32,  16);

	DYNAMIC_BASE = uint32_t_swap_bytes(DYNAMIC_BASE);
	copy_user_res = wasmjit_emscripten_copy_to_user(meminst,
							DYNAMICTOP_PTR->value.data.i32,
							&DYNAMIC_BASE,
							sizeof(DYNAMIC_BASE));
	(void)copy_user_res;
	assert(!copy_user_res);
}

void wasmjit_emscripten_derive_memory_globals(uint32_t static_bump,
					      struct WasmJITEmscriptenMemoryGlobals *out)
{
#define TOTAL_STACK 5242880
#define STACK_ALIGN_V 16
#define GLOBAL_BASE 1024
#define STATIC_BASE GLOBAL_BASE

	uint32_t STATICTOP = STATIC_BASE + static_bump;
	uint32_t STACK_BASE;

#define staticAlloc(_out, s)						\
	do {								\
		*(_out) = STATICTOP;					\
		STATICTOP = (STATICTOP + (s) + 15) & ((uint32_t) -16);	\
	} while (0)

	out->memoryBase = STATIC_BASE;
	out->__memory_base = STATIC_BASE;
	out->__table_base = 0;

	out->tempDoublePtr = STATICTOP;
	STATICTOP += 16;

	staticAlloc(&out->DYNAMICTOP_PTR, 4);

	STACK_BASE = out->STACKTOP = alignMemory(STATICTOP, STACK_ALIGN_V);
	out->STACK_MAX = STACK_BASE + TOTAL_STACK;
}

#ifdef __KERNEL__

#include <wasmjit/ktls.h>

__attribute__((noreturn))
void wasmjit_emscripten_internal_abort(const char *msg)
{
	printk(KERN_NOTICE "kwasmjit abort PID %d: %s", current->pid, msg);
	wasmjit_trap(WASMJIT_TRAP_ABORT);
}

struct MemInst *wasmjit_emscripten_get_mem_inst(struct FuncInst *funcinst)
{
	return wasmjit_get_ktls()->mem_inst;
}

#else

struct MemInst *wasmjit_emscripten_get_mem_inst(struct FuncInst *funcinst) {
	return funcinst->module_inst->mems.elts[0];
}

__attribute__((noreturn))
void wasmjit_emscripten_internal_abort(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	wasmjit_trap(WASMJIT_TRAP_ABORT);
}

#endif
