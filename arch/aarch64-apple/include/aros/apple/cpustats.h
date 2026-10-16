/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_APPLE_CPUSTATS_H
#define AROS_APPLE_CPUSTATS_H
#include <stdint.h>
#define AROS_APPLE_KATTR_CPU_STATS UINT32_C(0x83f10014)
#define AROS_APPLE_CPU_STATS_VERSION 2U
#define AROS_APPLE_CPU_STATS_MAX 64U
enum AROSCPUState { AROS_CPU_OFFLINE, AROS_CPU_BUSY, AROS_CPU_IDLE };
/* Each CPU owns its record; transition callers mask local interrupts.
 * Snapshot readers use bounded retries and never block the widget. */
struct AROSCPUAccounting {
    uint32_t sequence, state;
    uint64_t since, busy, idle, transitions;
};
struct AROSCPUSample { uint64_t busy, idle, transitions; uint32_t state; };

/* Immutable inventory, published after boot-core accounting starts. Each
 * non-null pointer names that CPU's independently sequenced accounting record;
 * null or OFFLINE means no admitted CPU. A boot-core sample is never an SMP
 * aggregate. Readers must not write this storage. Units are timer ticks. */
struct AROSAppleCPUStats {
    uint32_t version, size, boot_index, cpu_count;
    uint64_t frequency;
    const struct AROSCPUAccounting *accounting[AROS_APPLE_CPU_STATS_MAX];
};
#define APPLE_CPU_READ(p) __atomic_load_n((p),__ATOMIC_RELAXED)
static inline int AROSAppleCPURead(const struct AROSCPUAccounting *c, uint64_t now, struct AROSCPUSample *s)
{
    for (unsigned attempt=0;attempt<3;attempt++) {
        uint32_t seq=__atomic_load_n(&c->sequence,__ATOMIC_ACQUIRE);
        if (seq&1) continue;
        s->state=APPLE_CPU_READ(&c->state);s->busy=APPLE_CPU_READ(&c->busy);s->idle=APPLE_CPU_READ(&c->idle);
        s->transitions=APPLE_CPU_READ(&c->transitions);
        uint64_t since=APPLE_CPU_READ(&c->since);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (seq!=APPLE_CPU_READ(&c->sequence)) continue;
        /* A writer on another CPU may have a newer timestamp than this read. */
        if (now<since) return 0;
        if (s->state==AROS_CPU_BUSY) s->busy+=now-since;
        if (s->state==AROS_CPU_IDLE) s->idle+=now-since;
        return 1;
    }
    return 0;
}
static inline int AROSAppleCPULoad(const struct AROSCPUSample *before, const struct AROSCPUSample *after)
{
    if (!before->state || !after->state || after->busy<before->busy || after->idle<before->idle) return -1;
    uint64_t busy=after->busy-before->busy,idle=after->idle-before->idle;
    /* Scale before multiplying, preserving bounded arithmetic for long uptime. */
    while (busy>UINT64_MAX/2000 || idle>UINT64_MAX/2000) { busy>>=1;idle>>=1; }
    return busy+idle ? (int)(busy*1000/(busy+idle)) : -1;
}
/* Validate inventory before indexing. A missing/older kernel is unavailable. */
static inline int AROSAppleCPUStatsValid(const struct AROSAppleCPUStats *stats)
{
    return stats && stats->version == AROS_APPLE_CPU_STATS_VERSION &&
        stats->size == sizeof(*stats) && stats->cpu_count &&
        stats->cpu_count <= AROS_APPLE_CPU_STATS_MAX &&
        stats->boot_index < stats->cpu_count && stats->frequency &&
        stats->accounting[stats->boot_index];
}
/* Admission count at observation time, not an atomic cross-CPU snapshot. */
static inline unsigned AROSAppleCPUStatsOnline(const struct AROSAppleCPUStats *stats)
{
    unsigned online = 0;
    if (!AROSAppleCPUStatsValid(stats)) return 0;
    for (unsigned i = 0; i < stats->cpu_count; ++i) {
        const struct AROSCPUAccounting *c = stats->accounting[i];
        uint32_t state = c ? APPLE_CPU_READ(&c->state) : AROS_CPU_OFFLINE;
        if (state > AROS_CPU_IDLE) return 0;
        online += state != AROS_CPU_OFFLINE;
    }
    return online;
}
static inline int AROSAppleCPUStatsSample(const struct AROSAppleCPUStats *stats,
    uint64_t now, struct AROSCPUSample *sample)
{
    return sample && AROSAppleCPUStatsValid(stats) &&
        AROSAppleCPURead(stats->accounting[stats->boot_index], now, sample) &&
        (sample->state == AROS_CPU_BUSY || sample->state == AROS_CPU_IDLE);
}
#undef APPLE_CPU_READ
#endif
