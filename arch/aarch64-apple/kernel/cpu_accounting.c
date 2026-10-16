/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "cpu_accounting.h"
#define READ(p) __atomic_load_n((p),__ATOMIC_RELAXED)
#define WRITE(p,v) __atomic_store_n((p),(v),__ATOMIC_RELAXED)
void AROSCPUAccount(struct AROSCPUAccounting *c, uint32_t state, uint64_t now)
{
    uint32_t old=READ(&c->state),seq=READ(&c->sequence);
    /* Repeated exception entry is not a state transition. */
    if (old==state) return;
    WRITE(&c->sequence,seq+1);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    uint64_t elapsed=now-READ(&c->since);
    if (old==AROS_CPU_BUSY) WRITE(&c->busy,READ(&c->busy)+elapsed);
    if (old==AROS_CPU_IDLE) WRITE(&c->idle,READ(&c->idle)+elapsed);
    WRITE(&c->since,now);WRITE(&c->state,state);
    WRITE(&c->transitions,READ(&c->transitions)+1);
    __atomic_store_n(&c->sequence,seq+2,__ATOMIC_RELEASE);
}
int AROSCPUSnapshot(const struct AROSCPUAccounting *c, uint64_t now, struct AROSCPUSample *s)
{
    return AROSAppleCPURead(c, now, s);
}
int AROSCPULoad(const struct AROSCPUSample *before, const struct AROSCPUSample *after)
{
    return AROSAppleCPULoad(before, after);
}
