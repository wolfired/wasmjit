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
#include <linux/dirent.h>
#include <linux/statfs.h>
#include <linux/fadvise.h>

typedef int socklen_t;
typedef struct user_msghdr user_msghdr_t;
typedef unsigned int nfds_t;

#define SYS_CMSG_NXTHDR(msg, cmsg) __CMSG_NXTHDR((msg)->msg_control, (msg)->msg_controllen, (cmsg))

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long)))))

#define st_get_nsec(m, st) ((st)->st_ ## m ## time_nsec)

struct not_real;
typedef struct not_real DIR;

typedef struct statfs64 user_statvfs;

#define statvfs_get_type(pstvfs) ((pstvfs)->f_type)
#define statvfs_get_low_fsid(pstvfs) ((pstvfs)->f_fsid.val[0])
#define statvfs_get_high_fsid(pstvfs) ((pstvfs)->f_fsid.val[1])
#define statvfs_get_flags(pstvfs) ((pstvfs)->f_flags)
#define statvfs_get_namemax(pstvfs) ((pstvfs)->f_namelen)

#ifndef ST_WRITE
#define ST_WRITE 0x0080
#endif

#ifndef ST_APPEND
#define ST_APPEND 0x100
#endif

#ifndef ST_IMMUTABLE
#define ST_IMMUTABLE 0x100
#endif

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
#include <fcntl.h>
#include <dirent.h>
#include <sys/statvfs.h>

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

#if defined(__linux__)

struct linux_dirent64 {
	uint64_t d_ino;
	int64_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[1];
};

#endif

typedef struct statvfs user_statvfs;

#define statvfs_get_type(pstvfs) 0
#define statvfs_get_low_fsid(pstvfs) ((pstvfs)->f_fsid & 0xffffffff)
#define statvfs_get_high_fsid(pstvfs) ((pstvfs)->f_fsid >> 32)
#define statvfs_get_flags(pstvfs) ((pstvfs)->f_flag)
#define statvfs_get_namemax(pstvfs) ((pstvfs)->f_namemax)

#endif

#include <wasmjit/util.h>

/* declare all sys calls */

#define __KDECL(to,n,t) t _##n

#define VOID0 void
#define VOID1
#define VOID2
#define VOID3
#define VOID4
#define VOID5
#define VOID6

#define KWSC0(f, ...) KWSCx(0, f, __VA_ARGS__)
#define KWSC1(f, ...) KWSCx(1, f, __VA_ARGS__)
#define KWSC2(f, ...) KWSCx(2, f, __VA_ARGS__)
#define KWSC3(f, ...) KWSCx(3, f, __VA_ARGS__)
#define KWSC4(f, ...) KWSCx(4, f, __VA_ARGS__)
#define KWSC5(f, ...) KWSCx(5, f, __VA_ARGS__)
#define KWSC6(f, ...) KWSCx(6, f, __VA_ARGS__)

#ifdef __KERNEL__

#define KWSCx(_n, err, _pre, _name, ...) extern long (*_pre ## sys_ ## _name)(__KMAP(_n, __KDECL, __VA_ARGS__) VOID##_n);

#include <wasmjit/posix_sys_linux_kernel_def.h>

#define sys_pread sys_pread64
#define sys_pwrite sys_pwrite64

long sys_prlimit(pid_t pid, unsigned int resource,
		 const struct rlimit *new_limit,
		 struct rlimit *old_limit);

#define sys_statvfs(path, buf) sys_statfs64((path), sizeof(*(buf)), (buf))

#define sys_stat sys_newstat
#define sys_lstat sys_newlstat
#define sys_fstat sys_newfstat
#define sys_posix_fadvise sys_fadvise64_64

#else

#define KWSCx(_n, err, _pre, _name, ...) long _pre ## sys_ ## _name(__KMAP(_n, __KDECL, __VA_ARGS__));

#include <wasmjit/posix_sys_posix_def.h>

#endif

#undef KWSC0
#undef KWSC1
#undef KWSC2
#undef KWSC3
#undef KWSC4
#undef KWSC5
#undef KWSC6
#undef KWSCx
#undef __KDECL
#undef VOID0
#undef VOID1
#undef VOID2
#undef VOID3
#undef VOID4
#undef VOID5
#undef VOID6


#endif
