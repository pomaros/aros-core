/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_AARCH64_RELEASE_STORE_H
#define AROS_AARCH64_RELEASE_STORE_H
#include <stdint.h>

struct AArch64ReleaseStore {
    uint64_t address, value;
    unsigned bytes;
};

/* Decode the scalar STLUR family. No memory is accessed here. Conservatively
 * refuse unaligned accesses: the caller must preserve their architectural
 * fault, not turn them into a sequence of smaller writes. This helper does
 * not admit an address; ownership and permissions are the caller's job. */
static inline int AArch64DecodeReleaseStore(uint32_t instruction,
    const uint64_t x[31],uint64_t sp,struct AArch64ReleaseStore *store)
{
    if ((instruction & UINT32_C(0x3fe00c00)) != UINT32_C(0x19000000)) return 0;
    unsigned rn=(instruction>>5)&31,rt=instruction&31;
    unsigned bytes=1U<<(instruction>>30);
    int offset=(int)((instruction>>12)&511)-((instruction&(1U<<20)) ? 512 : 0);
    uint64_t base=rn==31 ? sp : x[rn];
    if ((rn==31 && (sp&15)) || (offset<0 && base<(unsigned)-offset) ||
        (offset>=0 && base>UINT64_MAX-(unsigned)offset)) return 0;
    uint64_t address=offset<0 ? base-(unsigned)-offset : base+(unsigned)offset;
    if ((address&(bytes-1)) || address>UINT64_MAX-(bytes-1)) return 0;
    store->address=address;store->value=rt==31 ? 0 : x[rt];store->bytes=bytes;
    return 1;
}

/* The caller supplies a validated, naturally aligned normal-memory alias.
 * Match the instruction's release ordering and single-copy atomic width.
 * In particular memcpy is not a substitute for this operation. */
static inline void AArch64PerformReleaseStore(const struct AArch64ReleaseStore *store,
    void *alias)
{
    switch (store->bytes) {
    case 1: __atomic_store_n((uint8_t *)alias,(uint8_t)store->value,__ATOMIC_RELEASE);break;
    case 2: __atomic_store_n((uint16_t *)alias,(uint16_t)store->value,__ATOMIC_RELEASE);break;
    case 4: __atomic_store_n((uint32_t *)alias,(uint32_t)store->value,__ATOMIC_RELEASE);break;
    case 8: __atomic_store_n((uint64_t *)alias,store->value,__ATOMIC_RELEASE);break;
    }
}
#endif
