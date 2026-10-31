/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_AARCH64_MAPPING_H
#define AROS_AARCH64_MAPPING_H

#include "protection.h"

/* Same mutation/quiescence contract as protection.h. This interface creates
 * mappings in holes only: existing RAM blocks must never be replaced to make
 * a virtual reservation. Intermediate tables are retained after unmapping.
 * Tables successfully published into a parent become address-space owned,
 * including on a later allocation failure. The owner reclaims tables only
 * after quiesced detachment/address-space teardown. allocate must keep a
 * ledger of EVERY returned page: an invalid address or failed resolve can
 * leave a returned page unpublished and still caller-owned. No release
 * callback is supplied and allocation failure is not an implicit rollback.
 * normal_index is the platform's MAIR index for normal cacheable memory.
 * Allocation must return a permanent identity-accessible table alias through
 * resolve; a zeroed descriptor is the only available leaf state. The normal
 * MAIR index is a platform assertion, not validated from its numerical value.
 * Supports TTBR0 <=48-bit 4K/16K, IPS <=48-bit, with no EPD0/TBI0/HA/HD/DS.
 * Table roots are page-aligned. This API refuses JIT32 as well as W+X;
 * executable leaf support does not establish safety of other physical aliases.
 */
int AArch64MapPage(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t virtual_address, uint64_t physical_address,
    unsigned permissions, unsigned normal_index);

/* Removes only the exact physical page installed by the owner, including a
 * temporarily inaccessible guard. Returns 0 rather than freeing another
 * owner's backing. The caller releases physical memory only after success.
 */
int AArch64UnmapPage(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t virtual_address, uint64_t expected_physical);

/* Remove only a privileged, UXN leaf with the expected physical backing.
 * This does not authorize eviction: the caller must own the private root,
 * exclude exception-transition code/data/stacks and serialize the mutation.
 * Never frees backing, allocates tables or splits blocks. The trusted source
 * root of a clone must not be passed here to make room for user mappings. */
int AArch64UnmapPrivilegedPage(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t virtual_address, uint64_t expected_physical);

/* Change permissions only for the exact existing physical owner; supports a
 * retained invalid guard too. Never replaces backing or splits a block. Uses
 * the current protection engine under the same caller-held lock/quiescence
 * interval, but intentionally refuses that engine's special JIT32 mode. */
int AArch64RemapPage(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t virtual_address, uint64_t expected_physical,
    unsigned permissions);

/* Update an existing privileged, UXN alias of exactly the expected page.
 * Refuses EL0-accessible aliases, unsupported leaves and any user/JIT flags.
 * Never creates or splits mappings. Caller serializes/quiesces every root
 * and retains backing on any refusal, including partial multi-root updates. */
int AArch64RemapPrivilegedPage(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t virtual_address, uint64_t expected_physical,
    unsigned permissions);

/* Permanent page owner lookup, also for an inaccessible guard. Does not
 * walk/split blocks or allocate; this is not a resident-user-pointer probe.
 * Only resolve is required; output is unchanged on failure. Restricted parent
 * tables are refused by ownership/mutation operations; resident translation
 * can inspect their physical target without promising access permissions. */
int AArch64MappingPhysical(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t virtual_address, uint64_t *physical);

/* Read-only TTBR0 translation of valid resident page/block descriptors. Keeps
 * the byte offset; does not check access permissions, fault pages in, allocate
 * tables or accept invalid guard descriptors. Only resolve is required.
 * Caller serializes table mutation and keeps all table pages alive. */
int AArch64TranslateResident(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t virtual_address, uint64_t *physical);

/* Read-only admission of one aligned resident EL0 page in an inactive root.
 * Requires AF, explicit user permission and the platform-asserted
 * normal-memory MAIR index. Refuses blocks, guarded/contiguous descriptors,
 * unsupported geometry and ALL parent restrictions (conservative refusal).
 * Only resolve is required; no root switch, allocation or cache/TLB mutation.
 * Caller serializes mutations and independently verifies ownership, full page
 * size and expected backing. This proves access, not lifetime or ownership.
 * Output is unchanged on refusal. Access is READ, WRITE or EXEC; EXEC requires
 * a read-only leaf with UXN clear. */
int AArch64UserPagePhysical(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, uint64_t virtual_address, unsigned access,
    unsigned normal_index, uint64_t *physical);

/* Build an unpublished private table tree from an immutable trusted template.
 * Preserves physical mappings/cache attributes and EL1 read/write restrictions,
 * removes EL0 data access and forces UXN on all leaves. Implicit EL1 execute
 * denial on writable user pages becomes explicit PXN. Privileged RWX leaves,
 * guarded/contiguous descriptors and non-address table attributes are refused.
 * Uses the same 4K/16K <=48-bit geometry as the leaf helpers. Full root pages
 * are required; unused entries outside the configured VA range must be zero.
 *
 * Caller freezes source mutations and pins ALL source tables/backing. allocate
 * must return fresh, unique pages disjoint from source storage, and ledger each
 * allocation for cleanup, INCLUDING partial failure. table_budget bounds total
 * newly allocated pages; recursion is limited by the architecture's four levels.
 * Output remains unchanged on refusal. This never installs TTBR, invalidates a
 * live root, or establishes a domain's lifetime. Caller must publish with the
 * platform's barriers/TLB protocol and keep template mappings coherent; this
 * snapshot is not a live mirror of subsequent trusted-root changes.
 */
int AArch64ClonePrivilegedRoot(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, size_t table_budget, uint64_t *output_root);

/* Read-only preflight with precisely the clone descriptor validation. Only
 * resolve is required; no allocation, clean or invalidation callback is used.
 * Counts destination pages, including repeated source subtrees (clones do not
 * share tables). Source tables must be frozen and pinned as above. The count
 * is valid for that snapshot only: a caller that drops the source lock before
 * allocating must revalidate under the lock and handle growth before cloning.
 * table_budget bounds traversal; table_count remains unchanged on refusal.
 */
int AArch64CountPrivilegedRoot(const struct AArch64ProtectionOps *, uint64_t ttbr0,
    uint64_t tcr, size_t table_budget, size_t *table_count);

#endif
