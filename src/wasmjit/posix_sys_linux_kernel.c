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

#include <wasmjit/posix_sys.h>

#include <wasmjit/util.h>
#include <wasmjit/ktls.h>

#define __KT(to,n,t) t
#define __KA(to,n,t) _##n
#define __KDECL(to,n,t) t _##n
#define __KINIT(to,n,t) . t = (unsigned long) _##n
#define __KSET(to,n,t) vals-> t = (unsigned long) _##n

#define KWSC1(name, ...) KWSCx(1, name, __VA_ARGS__)
#define KWSC2(name, ...) KWSCx(2, name, __VA_ARGS__)
#define KWSC3(name, ...) KWSCx(3, name, __VA_ARGS__)
#define KWSC5(name, ...) KWSCx(5, name, __VA_ARGS__)
#define KWSC6(name, ...) KWSCx(6, name, __VA_ARGS__)

#define KWSCx(x, name, ...) long (*sys_ ## name)(__KMAP(x, __KT, __VA_ARGS__));

#include <wasmjit/posix_sys_def.h>

#undef KWSCx

#ifdef __x86_64__

#define KWSCx(x, name, ...) long (*name)(struct pt_regs *);

static struct {
#include <wasmjit/posix_sys_def.h>
} sctable_regs;

#undef KWSCx
#define KWSCx(x, name, ...)						\
	long sys_ ## name ## _regs(__KMAP(x, __KDECL, __VA_ARGS__))	\
	{								\
		struct pt_regs *vals = &wasmjit_get_ktls()->regs;	\
		__KMAP(x, __KSET, di, si, dx, cx, r8, r9);		\
		return sctable_regs. name (vals);			\
	}

#include <wasmjit/posix_sys_def.h>

#undef KWSCx

#define SCPREFIX "__x64_sys_"

#endif

int posix_linux_kernel_init(void)
{
#ifdef SCPREFIX

#define KWSCx(x, n, ...)						\
	do {								\
		sys_ ## n = (void *)kallsyms_lookup_name("sys_" #n);	\
		if (!sys_ ## n) {					\
			sctable_regs. n = (void *)kallsyms_lookup_name(SCPREFIX #n); \
			if (!sctable_regs. n)				\
				return 0;				\
			sys_ ## n = &sys_ ## n ## _regs;		\
		}							\
	}								\
	while (0);

#else

#define KWSCx(x, n, ...)					\
	do {							\
		sctable. n = (void *)kallsyms_lookup_name("sys_" #n);	\
		if (!sctable. n)				\
			return 0;				\
	}							\
	while (0);

#endif

#include <wasmjit/posix_sys_def.h>

	return 1;
}
