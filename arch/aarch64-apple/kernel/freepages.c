/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#include <aros/kernel.h>
#include <aros/libcall.h>

#include "kernel_base.h"
#include "code_window.h"
#include "page_allocator.h"

#include <proto/kernel.h>

AROS_LH2(void, KrnFreePages,
    AROS_LHA(void *, address, A0),
    AROS_LHA(uintptr_t, length, D0),
    struct KernelBase *, KernelBase, 28, Kernel)
{
    AROS_LIBFUNC_INIT

    (void)KernelBase;
    /* Window pages are unmapped and go back to the arena, whatever their
     * protection: KrnAllocPages routed the request there. */
    if (AROSAppleCodeWindowContains(address, length)) {
        (void)AROSAppleCodeWindowFree(&AROSAppleCodeWindowKernelOps,
            address, length);
        return;
    }
    if (address != NULL && length != 0 && length <= UINT32_MAX) {
        /* Preserve ownership if any page failed to become writable. The
         * void public protection API cannot communicate that failure. */
        if (AROSApplePageProtect(address, (uint32_t)length,
                MAP_Readable | MAP_Writable))
            (void)AROSApplePageFreeRaw(address, length);
    }

    AROS_LIBFUNC_EXIT
}
