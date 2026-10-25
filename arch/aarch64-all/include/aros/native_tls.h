/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef AROS_AARCH64_NATIVE_TLS_H
#define AROS_AARCH64_NATIVE_TLS_H

#include <stdint.h>

/* Native task TLS, not the kernel's TPIDRRO_EL0 CPU anchor. The value is
 * opaque to Exec. The TLS runtime owns allocation and task-exit cleanup. */
static inline uint64_t AROSAArch64ReadTaskPointer(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(value) :: "memory");
    return value;
}

static inline void AROSAArch64WriteTaskPointer(uint64_t value)
{
    __asm__ volatile("msr tpidr_el0, %0\n\tisb" :: "r"(value) : "memory");
}

#endif
