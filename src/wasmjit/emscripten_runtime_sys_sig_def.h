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

SIG(HUP, 1)
SIG(INT, 2)
SIG(QUIT, 3)
SIG(ILL, 4)
SIG(TRAP, 5)
SIG(ABRT, 6)
SIG(BUS, 7)
SIG(FPE, 8)
SIG(KILL, 9)
SIG(USR1, 10)
SIG(SEGV, 11)
SIG(USR2, 12)
SIG(PIPE, 13)
SIG(ALRM, 14)
SIG(TERM, 15)
#ifdef SIGSTKFLT
SIG(STKFLT, 16)
#endif
SIG(CHLD, 17)
SIG(CONT, 18)
SIG(STOP, 19)
SIG(TSTP, 20)
SIG(TTIN, 21)
SIG(TTOU, 22)
SIG(URG, 23)
SIG(XCPU, 24)
SIG(XFSZ, 25)
SIG(VTALRM, 26)
SIG(PROF, 27)
SIG(WINCH, 28)
SIG(IO, 29)
#ifdef SIGPOLL
SIG(POLL, 29)
#endif
#ifdef SIGPWR
SIG(PWR, 30)
#endif
SIG(SYS, 31)
