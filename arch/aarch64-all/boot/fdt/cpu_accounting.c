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
    for (unsigned attempt=0;attempt<3;attempt++) {
        uint32_t seq=__atomic_load_n(&c->sequence,__ATOMIC_ACQUIRE);
        if (seq&1) continue;
        s->state=READ(&c->state);s->busy=READ(&c->busy);s->idle=READ(&c->idle);
        s->transitions=READ(&c->transitions);
        uint64_t since=READ(&c->since);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (seq!=READ(&c->sequence)) continue;
        /* A writer on another CPU may have a newer timestamp than this read. */
        if (now<since) return 0;
        if (s->state==AROS_CPU_BUSY) s->busy+=now-since;
        if (s->state==AROS_CPU_IDLE) s->idle+=now-since;
        return 1;
    }
    return 0;
}
int AROSCPULoad(const struct AROSCPUSample *before, const struct AROSCPUSample *after)
{
    if (!before->state || !after->state || after->busy<before->busy || after->idle<before->idle) return -1;
    uint64_t busy=after->busy-before->busy,idle=after->idle-before->idle;
    /* Scale before multiplying, preserving bounded arithmetic for long uptime. */
    while (busy>UINT64_MAX/2000 || idle>UINT64_MAX/2000) { busy>>=1;idle>>=1; }
    return busy+idle ? (int)(busy*1000/(busy+idle)) : -1;
}
