/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1

    v8.library glue for AArch64 AROS, beside the engine sources.

    - The JIT hooks of the D-135 heap model (V8 queue 0033): code pages come
      from the kernel's executable arena and the write switch is SPRR's.
    - The timer hooks v8_init.c calls: the x86_64 hosted build provides them
      in a glibc shim, which only a V8 built for target_os=linux needs. This
      V8 is built against the AROS SDK and reads posixc's clock directly, so
      there is no timer.device to open.
*/

#include <stddef.h>
#include <stdint.h>

void v8_init_timer(void) {}
void v8_cleanup_timer(void) {}

/*
 * The JIT write switch and the kernel's JIT mode (<aros/apple/jit.h>).
 * V8's RwxMemoryWriteScope calls the switch around every write into code
 * pages; V8::Initialize reads the mode (__aros_v8_jit_mode) and runs
 * jitless when the kernel offers no executable code pages.
 */
#define DEBUG 1
#include <aros/debug.h>
#include <exec/memory.h>
#include <aros/apple/jit.h>
#include <aros/kernel.h>
#include <proto/exec.h>
#include <proto/kernel.h>

static int jit_mode = -1;

int __aros_v8_jit_mode(void)
{
    struct KernelBase *KernelBase;
    intptr_t mode;

    if (jit_mode >= 0)
        return jit_mode;
    KernelBase = OpenResource("kernel.resource");
    mode = KernelBase ? KrnGetSystemAttr(AROS_APPLE_KATTR_JIT_MODE) : -1;
    /* A kernel without the attribute (hosted, other platforms) answers -1. */
    jit_mode = (mode == AROS_APPLE_JIT_SPRR || mode == AROS_APPLE_JIT_RWX) ?
        (int)mode : AROS_APPLE_JIT_NONE;
    return jit_mode;
}

/* Only the SPRR kernel has the switch; elsewhere it is a no-op, never an SVC. */
uint64_t __aros_v8_jit_write(int writable)
{
    if (__aros_v8_jit_mode() != AROS_APPLE_JIT_SPRR)
        return (uint64_t)__aros_v8_jit_mode();
    return AROSAppleJitWriteProtect(writable);
}

#ifndef MEMF_RESERVE
#define MEMF_RESERVE (1L << 14)
#endif

/* JIT code pages from the kernel's executable arena (<aros/apple/jit.h>). */
void *__aros_v8_jit_alloc(size_t size)
{
    struct KernelBase *KernelBase = OpenResource("kernel.resource");

    void *pages;

    if (KernelBase == NULL || __aros_v8_jit_mode() == AROS_APPLE_JIT_NONE)
        return NULL;
    /* Reserved, not backed: V8 maps code space with a protection, which
     * commits the pages it makes accessible (MEMF_RESERVE). */
    pages = KrnAllocPages(NULL, size, MEMF_EXECUTABLE | MEMF_PUBLIC | MEMF_RESERVE);
    if (pages == NULL)
        bug("[V8JIT] executable arena refused %lu bytes\n", (unsigned long)size);
    return pages;
}

void __aros_v8_jit_free(void *address, size_t size)
{
    struct KernelBase *KernelBase = OpenResource("kernel.resource");

    if (KernelBase == NULL)
        return;
    /* Back to plain data pages before the arena reuses them. */
    KrnSetProtection(address, size, MAP_Readable | MAP_Writable);
    KrnFreePages(address, size);
}

int __aros_v8_jit_protect(void *address, size_t size, int read, int write,
    int execute)
{
    struct KernelBase *KernelBase = OpenResource("kernel.resource");
    ULONG flags = 0;

    if (KernelBase == NULL)
        return 0;
    if (read) flags |= MAP_Readable;
    if (write) flags |= MAP_Writable;
    if (execute) flags |= MAP_Executable;
    KrnSetProtection(address, size, flags);
    return 1;
}

/* Diagnostic trace from V8's memory shim, on the kernel debug channel. */
void __aros_v8_trace(const char *what, const void *address, size_t size, int flag)
{
    bug("[V8JIT] %s %p size=%lu flag=%d\n", what, address, (unsigned long)size, flag);
}
