/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: JIT code memory on Apple AArch64: the write-protection toggle.
 *
 * A JIT asks for pages that are readable, writable and executable at EL0
 * (KrnSetProtection with MAP_Readable | MAP_Writable | MAP_Executable, no
 * MAP_Supervisor). What it gets depends on the kernel's JIT mode:
 *
 *   AROS_APPLE_JIT_NONE   the request is refused; run interpreters only.
 *   AROS_APPLE_JIT_SPRR   the pages are executable; the task switches every
 *                         JIT page it owns between r-x and rw- with
 *                         AROSAppleJitWriteProtect(). The switch is per task
 *                         and per core, through Apple's SPRR permission
 *                         remapping, and survives task switches.
 *   AROS_APPLE_JIT_RWX    virtual-machine guests without SPRR: the pages are
 *                         really writable and executable at once, so the
 *                         same JIT code can be exercised off the hardware.
 *                         AROSAppleJitWriteProtect() succeeds and does
 *                         nothing.
 *
 * The mode is KrnGetSystemAttr(AROS_APPLE_KATTR_JIT_MODE). The boot
 * property /chosen aros,jit selects it: absent or 0 gives the target's
 * default (NONE on Apple silicon, RWX in a virtual-machine guest), 1 turns
 * the SPRR switch on, 2 forces NONE everywhere.
 */

#ifndef AROS_APPLE_JIT_H
#define AROS_APPLE_JIT_H

#include <stdint.h>

#define AROS_APPLE_KATTR_JIT_MODE UINT32_C(0x83f10020)

#define AROS_APPLE_JIT_NONE 0
#define AROS_APPLE_JIT_SPRR 1
#define AROS_APPLE_JIT_RWX 2

/* Runtime SVC: x0 = 1 makes the calling task's JIT pages writable (and not
 * executable), x0 = 0 makes them executable (and not writable). Returns the
 * JIT mode, or AROS_APPLE_RUNTIME_SVC_ERROR when the mode is NONE. */
#define AROS_APPLE_RUNTIME_SVC_JIT_WRITE UINT32_C(0xa50a)

static inline uint64_t AROSAppleJitWriteProtect(int writable)
{
    register uint64_t value __asm__("x0") = writable ? 1 : 0;

    __asm__ volatile("svc #0xa50a" : "+r"(value) :: "memory", "cc");
    return value;
}

#endif
