/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Narrow recovery predicate for an explicitly registered stack-guard probe.
 * This is not a policy for recovering arbitrary application faults.
 */
#ifndef AROS_AARCH64_GUARD_FAULT_H
#define AROS_AARCH64_GUARD_FAULT_H
#include <stdint.h>

static inline int AArch64GuardProbeMatches(uintptr_t owner, uintptr_t current,
    uintptr_t lower, uintptr_t upper, uintptr_t expected_pc,
    uint64_t vector, uint64_t spsr, uint64_t esr, uint64_t far,
    uint64_t pc, uint64_t sp, uint64_t saved_sp)
{
    return owner && owner == current && lower >= 16 && upper > lower &&
        vector == 8 && (spsr & 31) == 0 &&
        ((esr >> 26) & 63) == 0x24 && (esr & 63) == 7 &&
        (esr & (UINT64_C(1) << 6)) &&
        !(esr & ((UINT64_C(1) << 10) | (UINT64_C(1) << 7))) &&
        far == lower - 16 && pc == expected_pc && sp == lower &&
        saved_sp >= lower && saved_sp <= upper && !(saved_sp & 15);
}
#endif
