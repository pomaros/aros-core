/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_AARCH64_PROTECTION_H
#define AROS_AARCH64_PROTECTION_H

#include <stdint.h>
#include <stddef.h>

enum AArch64Protection {
    AARCH64_PROTECT_READ = 1,
    AARCH64_PROTECT_WRITE = 2,
    AARCH64_PROTECT_EXEC = 4,
    AARCH64_PROTECT_USER = 8,
    /* Shared OS code may execute at both EL0 and EL1, but remains read-only. */
    AARCH64_PROTECT_EXEC_BOTH = 16,
    /* READ | WRITE | EXEC | USER only: the descriptor the platform remaps
     * into its JIT permission (AP[0] set, AP[1] clear, PXN set, UXN clear).
     * Without remapping it is EL0 read-write-execute; the platform decides. */
    AARCH64_PROTECT_JIT = 32
};

/* Platform contract, deliberately independent of an allocator or privilege
 * transition. Caller holds the address-space mutation lock, excludes hardware
 * AF/dirty updates, and quiesces users of the changed mapping. In particular,
 * code/stack/table aliases used during break-before-make must remain mapped
 * while a containing block is broken. A local Forbid is NOT an SMP lock.
 *
 * resolve returns a permanent, writable alias for a physical table page.
 * allocate returns an aligned physical table address (zero means exhausted).
 * clean publishes a table byte range to the walker; invalidate performs
 * DSB ISHST; TLBI VMALLE1IS; DSB ISH; ISB for the address space. synchronize
 * makes modified code coherent before granting execute permission.
 * No callback may sleep with the platform's lock held.
 */
struct AArch64ProtectionOps {
    void *context;
    uint64_t *(*resolve)(void *, uint64_t);
    uint64_t (*allocate)(void *, unsigned);
    void (*clean)(void *, const void *, size_t);
    void (*invalidate)(void *);
    void (*synchronize)(void *, uint64_t, size_t);
    /* Optional platform restriction before a live block is split. */
    int (*can_split)(void *, uint64_t descriptor);
};

/* One aligned page in a TTBR0, <=48-bit, 4KB or 16KB stage-1 map. TCR HA/HD,
 * DS and tagged addresses are unsupported. Rejects W+X and execute-only.
 * Returns 1 on success, 0 on invalid/unsupported/unmapped input or allocation
 * failure. Failure may leave equivalent block-to-table splits behind, but
 * never changes leaf permissions. Does not create mappings for holes.
 *
 * No-access preserves the output address in an invalid descriptor using SW
 * bit 58; the table owner must reserve that bit. This lets the same function
 * restore a guard without guessing its physical address. Never free its
 * backing memory until the owner removes that descriptor.
 */
int AArch64ProtectPage(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t address, unsigned permissions);

#endif
