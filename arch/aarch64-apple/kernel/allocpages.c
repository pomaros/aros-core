/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#include <aros/libcall.h>
#include <exec/memory.h>

#include "kernel_base.h"
#include "code_window.h"
#include "page_allocator.h"

#include <proto/kernel.h>

/*
 * Hunks (LoadSeg asks with MEMF_PUBLIC) and JIT regions (MEMF_EXECUTABLE)
 * come from the code window: virtually contiguous, physically anywhere, as
 * large as memory allows. Requests without either flag are drivers' DMA
 * buffers and probes, which need physically contiguous identity memory:
 * they come from the code arena, as do address-limited requests.
 * MEMF_RESERVE with either flag reserves window pages with no memory behind
 * them; KrnSetProtection commits what it makes accessible.
 */
static int for_window(void *address, uintptr_t length, uint32_t flags)
{
    if ((flags & (MEMF_PUBLIC | MEMF_EXECUTABLE)) == 0 ||
        (flags & (MEMF_31BIT | MEMF_24BITDMA)) != 0)
        return 0;
    return address == NULL || AROSAppleCodeWindowContains(address, length);
}

AROS_LH3(void *, KrnAllocPages,
    AROS_LHA(void *, address, A0),
    AROS_LHA(uintptr_t, length, D0),
    AROS_LHA(uint32_t, flags, D1),
    struct KernelBase *, KernelBase, 27, Kernel)
{
    AROS_LIBFUNC_INIT

    (void)KernelBase;
    if (for_window(address, length, flags))
        return (flags & MEMF_RESERVE) ?
            AROSAppleCodeWindowReserve(&AROSAppleCodeWindowKernelOps,
                address, length) :
            AROSAppleCodeWindowAllocate(&AROSAppleCodeWindowKernelOps,
                address, length);
    if (flags & MEMF_RESERVE)
        return NULL;
    return AROSApplePageAllocate(address, length, flags);

    AROS_LIBFUNC_EXIT
}
