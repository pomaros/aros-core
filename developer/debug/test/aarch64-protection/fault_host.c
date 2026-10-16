/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "guard_fault.h"
#include <assert.h>

void test_guard_fault(void)
{
    const uint64_t esr = (UINT64_C(0x24) << 26) | 0x40 | 7;
    uint64_t args[] = {1, 1, 0x14000, 0x24000, 0x400000,
        8, 0, esr, 0x13ff0, 0x400000, 0x14000, 0x23000};
#define MATCH() AArch64GuardProbeMatches(args[0], args[1], args[2], args[3], \
    args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11])
    assert(MATCH());
    /* Wrong task, vector, privilege, FAR, PC, active SP or saved SP must
     * never take the continuation, even if another field still matches. */
    const unsigned indices[] = {0, 1, 5, 6, 8, 9, 10, 11};
    for (unsigned i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
        unsigned index = indices[i];
        args[index]++;
        assert(!MATCH());
        args[index]--;
    }
    const uint64_t wrong_esr[] = {esr ^ 0x40, esr | (1U << 10),
        esr | (1U << 7), esr ^ (1U << 26), (esr & ~63U) | 15};
    for (unsigned i = 0; i < sizeof(wrong_esr) / sizeof(wrong_esr[0]); ++i) {
        args[7] = wrong_esr[i];
        assert(!MATCH());
    }
}
