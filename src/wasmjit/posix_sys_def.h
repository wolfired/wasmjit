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


KWSC3(1, , socket, int, int, int)
KWSC3(1, , bind, int, const struct sockaddr *, socklen_t)
KWSC3(1, , connect, int, const struct sockaddr *, socklen_t)
KWSC2(1, , listen, int, int)
KWSC3(1, , accept, int, struct sockaddr *, socklen_t *)
KWSC1(1, , unlink, const char *)
KWSC1(1, , chdir, const char *)
KWSC3(1, , getsockname, int, struct sockaddr *, socklen_t *)
KWSC3(1, , getpeername, int, struct sockaddr *, socklen_t *)
KWSC5(1, , setsockopt, int, int, int, const void *, socklen_t)
KWSC5(1, , getsockopt, int, int, int, void *, socklen_t *)
KWSC1(1, , pipe, int *)
KWSC5(1, , select, int, fd_set *, fd_set *, fd_set *, struct timeval *)
KWSC3(1, , poll, struct pollfd *, nfds_t, int)
KWSC0(0, , getuid)
KWSC0(0, , getgid)
KWSC0(0, , getegid)
KWSC0(0, , getpid)
KWSC0(0, , geteuid)
KWSC3(1, , chown, const char *, uid_t, gid_t)
KWSC2(1, , rename, const char *, const char *)
KWSC0(0, , getppid)
KWSC0(1, , setsid)
KWSC2(1, , clock_gettime, clockid_t, struct timespec *)
KWSC2(1, , gettimeofday, struct timeval *, struct timezone *)
KWSC2(1, , kill, pid_t, int)
KWSC0(1, , sched_yield)
