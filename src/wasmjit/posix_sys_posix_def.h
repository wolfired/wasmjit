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


/* common definitions */
#include <wasmjit/posix_sys_def.h>

KWSC3(1, , lseek, int, off_t, int)
KWSC3(1, , writev, int, const struct iovec *, int)
KWSC3(1, , write, int, const void *, size_t)
KWSC1(1, , close, int)
KWSC6(1, , sendto, int, const void *, size_t, int, const struct sockaddr *, socklen_t)
KWSC6(1, , recvfrom, int, void *, size_t, int, struct sockaddr *, socklen_t *)
KWSC3(1, , sendmsg, int, const user_msghdr_t *, int)
KWSC3(1, , recvmsg, int, user_msghdr_t *, int)
KWSC3(1, , read, int, void *, size_t)
KWSC3(1, , readv, int, const struct iovec *, int)
KWSC2(1, , chmod, const char *, mode_t)
KWSC4(1, , pread, int, void *, size_t, off_t)
KWSC4(1, , pwrite, int, const void *, size_t, off_t)
KWSC2(1, , getrlimit, int, struct rlimit *)
KWSC4(1, , prlimit, int, int, const struct rlimit *, struct rlimit *)
KWSC2(1, , ftruncate, int, off_t);
KWSC2(1, , stat, const char *, struct stat *);
KWSC2(1, , lstat, const char *, struct stat *);
KWSC2(1, , fstat, int, struct stat *);
#ifdef __linux__
KWSC3(1, , getdents64, int, void *, size_t)
#endif
KWSC4(1, , openat, int, const char *, int, mode_t)
KWSC2(1, , statvfs, const char *, struct statvfs *)
KWSC4(1, , posix_fadvise, int, off_t, off_t, int)
KWSC4(1, , fstatat, int, const char *, struct stat *, int);
KWSC4(1, , pwritev, int, const struct iovec *, int, off_t)
KWSC2(1, , mkdir, const char *, mode_t)
KWSC1(1, , umask, mode_t)
KWSC2(1, , dup2, int, int)
KWSC2(1, , setrlimit, int, const struct rlimit *)
KWSC3(1, , readlink, const char *, char *, size_t)
KWSC3(1, , setpriority, int, id_t, int)
KWSC3(1, , sigaction, int, const struct sigaction *, struct sigaction *)
KWSC3(1, , sigprocmask, int, const sigset_t *, sigset_t *)
KWSC1(1, , sigsuspend, const sigset_t *)
KWSC3(1, , waitpid, pid_t, int *, int)
