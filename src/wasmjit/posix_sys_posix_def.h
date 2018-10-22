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

KWSC3(, lseek, int, off_t, int)
KWSC3(, writev, int, const struct iovec *, int)
KWSC3(, write, int, const void *, size_t)
KWSC1(, close, int)
KWSC6(, sendto, int, const void *, size_t, int, const struct sockaddr *, socklen_t)
KWSC6(, recvfrom, int, void *, size_t, int, struct sockaddr *, socklen_t *)
KWSC3(, sendmsg, int, const user_msghdr_t *, int)
KWSC3(, recvmsg, int, user_msghdr_t *, int)
KWSC3(, read, int, void *, size_t)
KWSC3(, readv, int, const struct iovec *, int)
KWSC2(, chmod, const char *, mode_t)
KWSC4(, preadv, int, const struct iovec *, int, off_t)
KWSC4(, pwritev, int, const struct iovec *, int, off_t)
KWSC2(, getrlimit, int, struct rlimit *)
KWSC4(, prlimit, int, int, const struct rlimit *, struct rlimit *)
KWSC2(, ftruncate, int, off_t);
KWSC2(, stat, const char *, struct stat *);
KWSC2(, lstat, const char *, struct stat *);
