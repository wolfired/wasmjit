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

/* For prlimit */
#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <wasmjit/posix_sys.h>

#include <wasmjit/ast.h>
#include <wasmjit/runtime.h>
#include <wasmjit/util.h>

#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#if !(defined(__linux__) || defined(__KERNEL__))

int prlimit(int pid, int resource,
	    const struct rlimit *new_limit,
	    struct rlimit *old_limit)
{
	(void)pid;
	(void)resource;
	(void)new_limit;
	(void)old_limit;
	errno = ENOSYS;
	return -1;
}

#endif

#if defined(__linux__)

#include <unistd.h>
#include <sys/syscall.h>

int getdents64(unsigned int fd, struct linux_dirent64 *buf, unsigned int nbytes)
{
	return syscall(SYS_getdents64, fd, buf, nbytes);
}

#endif

#ifdef NEED_POSIX_FADVISE

int posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
	(void) fd;
	(void) offset;
	(void) len;
	(void) advice;
	/* doing nothing is a valid implementation of posix_fadvise */
	return 0;
}

#endif

#if defined(__APPLE__)

ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt,
		off_t offset)
{
	ssize_t toret = 0;
	int i;
	for (i = 0; i < iovcnt; ++i) {
		size_t written = 0;
		while (written < iov[i].iov_len) {
			ssize_t ltoret;
			ltoret = pwrite(fd,
					iov[i].iov_base + written, iov[i].iov_len - written,
					offset);
			if (ltoret < 0) {
				if (toret)
					return toret;
				return ltoret;
			}
			written += ltoret;
			offset += ltoret;
			toret += ltoret;
		}
	}
	return toret;
}

#endif

#define __KDECL(to,n,t) t _##n
#define __KA(to,n,t) _##n

#define KWSCx(x, err, pre, name, ...)					\
	long pre ## sys_ ## name(__KMAP(x, __KDECL, __VA_ARGS__))	\
	{							\
		long ret;					\
		ret = name(__KMAP(x, __KA, __VA_ARGS__));	\
		if (!(err)) {					\
			return ret;				\
		}						\
		if (ret == -1) {				\
			ret = -errno;				\
		}						\
		return ret;					\
	}

#define KWSC0(f, ...) KWSCx(0, f, __VA_ARGS__)
#define KWSC1(f, ...) KWSCx(1, f, __VA_ARGS__)
#define KWSC2(f, ...) KWSCx(2, f, __VA_ARGS__)
#define KWSC3(f, ...) KWSCx(3, f, __VA_ARGS__)
#define KWSC4(f, ...) KWSCx(4, f, __VA_ARGS__)
#define KWSC5(f, ...) KWSCx(5, f, __VA_ARGS__)
#define KWSC6(f, ...) KWSCx(6, f, __VA_ARGS__)

#include <wasmjit/posix_sys_posix_def.h>
