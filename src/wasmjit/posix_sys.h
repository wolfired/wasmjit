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

#ifndef __WASMJIT__EMSCRIPTEN_RUNTIME_SYS_H__
#define __WASMJIT__EMSCRIPTEN_RUNTIME_SYS_H__

#ifdef __KERNEL__

#include <linux/uio.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/limits.h>
#include <linux/socket.h>
#include <linux/poll.h>
#include <linux/stat.h>

typedef int socklen_t;
typedef struct user_msghdr user_msghdr_t;
typedef unsigned int nfds_t;

#define SYS_CMSG_NXTHDR(msg, cmsg) __CMSG_NXTHDR((msg)->msg_control, (msg)->msg_controllen, (cmsg))

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long)))))

#define st_get_nsec(m, st) ((st)->st_ ## m ## time_nsec)

#else

#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <net/if.h>
#include <sys/resource.h>
#include <sys/stat.h>

#if defined(_POSIX_VERSION) && _POSIX_VERSION >= 200809
#define st_get_nsec(m, st) ((st)->st_ ## m ## tim.tv_nsec)
#else
#define st_get_nsec(m, st) 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct msghdr user_msghdr_t;

#define SYS_CMSG_NXTHDR(msg, cmsg) CMSG_NXTHDR((msg), (cmsg))

#endif

#include <wasmjit/util.h>

/* declare all sys calls */

#define __KDECL(to,n,t) t _##n

#define KWSC1(pre, name, ...) KWSCx(1, pre, name, __VA_ARGS__)
#define KWSC2(pre, name, ...) KWSCx(2, pre, name, __VA_ARGS__)
#define KWSC3(pre, name, ...) KWSCx(3, pre, name, __VA_ARGS__)
#define KWSC4(pre, name, ...) KWSCx(4, pre, name, __VA_ARGS__)
#define KWSC5(pre, name, ...) KWSCx(5, pre, name, __VA_ARGS__)
#define KWSC6(pre, name, ...) KWSCx(6, pre, name, __VA_ARGS__)

#ifdef __KERNEL__

#define KWSCx(_n, _pre, _name, ...) extern long (*_pre ## sys_ ## _name)(__KMAP(_n, __KDECL, __VA_ARGS__));

#include <wasmjit/posix_sys_linux_kernel_def.h>

#define sys_preadv(fd, vec, vlen, pos)					\
	_sys_preadv((fd), (vec), (vlen), (pos) & 0xffffffff, (pos) >> 32)

#define sys_pwritev(fd, vec, vlen, pos)					\
	_sys_pwritev((fd), (vec), (vlen), (pos) & 0xffffffff, (pos) >> 32)

long sys_prlimit(pid_t pid, unsigned int resource,
		 const struct rlimit *new_limit,
		 struct rlimit *old_limit);

#define sys_stat sys_newstat
#define sys_lstat sys_newlstat

#else

#define KWSCx(_n, _pre, _name, ...) long _pre ## sys_ ## _name(__KMAP(_n, __KDECL, __VA_ARGS__));

#include <wasmjit/posix_sys_posix_def.h>

#endif

#undef KWSC1
#undef KWSC2
#undef KWSC3
#undef KWSC4
#undef KWSC5
#undef KWSC6
#undef KWSCx
#undef __KDECL

#endif
