/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_APPLE_MEMORY_RENDEZVOUS_H
#define AROS_APPLE_MEMORY_RENDEZVOUS_H
#include <stdint.h>

/* Control state used by the SMP IPI/page-table rendezvous adapter.
 * Zero initialized. One owner, one non-reentrant participant per CPU.
 * The owner keeps local migration/preemption disabled. Participants acknowledge
 * only after stopping accesses and leave only after local architectural sync.
 * Every atomic request contains both generation and target mask: a delayed
 * participant cannot combine an old request with a new set of targets.
 * Cancellation is permitted only BEFORE mutation. It need not await late IPIs.
 * Successful mutation retains ownership until all participants departed.
 * No request payload is dereferenced by a delayed participant. */
struct AROSAppleMemoryRendezvous {
    uint32_t owned, generation;
    uint64_t request;
    uint64_t arrived[32], departed[32];
};
static inline uint64_t AROSAppleMemoryRendezvousBegin(
    struct AROSAppleMemoryRendezvous *r, uint32_t targets)
{
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&r->owned, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    /* Never reuse a generation: fail closed at exhaustion, no ABA. */
    if (r->generation == UINT32_MAX) {
        __atomic_store_n(&r->owned, 0, __ATOMIC_RELEASE);
        return 0;
    }
    uint64_t token = ((uint64_t)++r->generation << 32) | targets;
    __atomic_store_n(&r->request, token, __ATOMIC_RELEASE);
    return token;
}
static inline uint64_t AROSAppleMemoryRendezvousRequest(
    struct AROSAppleMemoryRendezvous *r, unsigned cpu)
{
    uint64_t token = __atomic_load_n(&r->request, __ATOMIC_ACQUIRE);
    return cpu < 32 && ((uint32_t)token & (UINT32_C(1) << cpu)) ? token : 0;
}
static inline int AROSAppleMemoryRendezvousEnter(
    struct AROSAppleMemoryRendezvous *r, unsigned cpu, uint64_t token)
{
    if (!token || AROSAppleMemoryRendezvousRequest(r, cpu) != token)
        return 0;
    __atomic_store_n(&r->arrived[cpu], token, __ATOMIC_RELEASE);
    return 1;
}
static inline int AROSAppleMemoryRendezvousHeld(
    struct AROSAppleMemoryRendezvous *r, uint64_t token)
{
    return __atomic_load_n(&r->request, __ATOMIC_ACQUIRE) == token;
}
static inline int AROSAppleMemoryRendezvousAll(
    uint64_t slots[32], uint64_t token)
{
    if (!token) return 0;
    for (unsigned cpu = 0; cpu < 32; ++cpu)
        if (((uint32_t)token & (UINT32_C(1) << cpu)) &&
            __atomic_load_n(&slots[cpu], __ATOMIC_ACQUIRE) != token)
            return 0;
    return 1;
}
/* Owner only. Release after completing mutation and publication barriers. */
static inline void AROSAppleMemoryRendezvousRelease(
    struct AROSAppleMemoryRendezvous *r)
{
    __atomic_store_n(&r->request, 0, __ATOMIC_RELEASE);
}
/* Participant only, following local architectural synchronization. */
static inline void AROSAppleMemoryRendezvousLeave(
    struct AROSAppleMemoryRendezvous *r, unsigned cpu, uint64_t token)
{
    if (cpu < 32 && token)
        __atomic_store_n(&r->departed[cpu], token, __ATOMIC_RELEASE);
}
/* Owner only: after all departures, or cancellation before any mutation. */
static inline void AROSAppleMemoryRendezvousEnd(
    struct AROSAppleMemoryRendezvous *r)
{
    AROSAppleMemoryRendezvousRelease(r);
    __atomic_store_n(&r->owned, 0, __ATOMIC_RELEASE);
}
#endif
