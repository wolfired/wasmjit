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

#ifndef __KWASMJIT__KWASMJIT_H
#define __KWASMJIT__KWASMJIT_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif

#if defined(__linux__)
#include <linux/ioctl.h>
#elif defined(__APPLE__) || defined(__OpenBSD__)
#include <sys/ioccom.h>
#else
#error System not supported
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define KWASMJIT_MAGIC 0xCC

struct kwasmjit_instantiate_args {
    uint32_t version;
    const char *file_name;
    const char *module_name;
    uint32_t flags;
};

#define KWASMJIT_INSTANTIATE_EMSCRIPTEN_RUNTIME_FLAGS_NO_TABLE 1

struct kwasmjit_instantiate_emscripten_runtime_args {
    uint32_t version;
    uint32_t static_bump;
    size_t tablemin, tablemax;
    uint32_t flags;
};

struct kwasmjit_emscripten_invoke_main_args {
    uint32_t version;
    const char *module_name;
    int argc;
    char **argv;
    char **envp;
    uint32_t flags;
};

struct kwasmjit_error_message_args {
    uint32_t version;
    char *buffer;
    size_t size;
};

#define KWASMJIT_INSTANTIATE _IOW(KWASMJIT_MAGIC, 0, struct kwasmjit_instantiate_args)
#define KWASMJIT_INSTANTIATE_EMSCRIPTEN_RUNTIME _IOW(KWASMJIT_MAGIC, 1, struct kwasmjit_instantiate_emscripten_runtime_args)
#define KWASMJIT_EMSCRIPTEN_INVOKE_MAIN _IOW(KWASMJIT_MAGIC, 2, struct kwasmjit_emscripten_invoke_main_args)
#define KWASMJIT_ERROR_MESSAGE _IOW(KWASMJIT_MAGIC, 3, struct kwasmjit_error_message_args)

#ifdef __cplusplus
}
#endif

#endif
