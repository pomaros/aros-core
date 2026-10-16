/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Kernel-owned SPI diagnostics. One poller publishes; readers never wait for
 * it. Atomic payload accesses avoid C data races as well as torn snapshots.
 */
#ifndef AROS_APPLE_SPIHIDSTATS_H
#define AROS_APPLE_SPIHIDSTATS_H
#include <stdint.h>

#define AROS_APPLE_KATTR_SPI_STATS UINT32_C(0x83f10010)
#define AROS_APPLE_SPI_STATS_VERSION 3U

enum AROSAppleSPIMetric {
    APPLE_SPI_SAMPLE_COUNTER, APPLE_SPI_COUNTER_FREQUENCY,
    APPLE_SPI_POLLS, APPLE_SPI_KEY_REPORTS, APPLE_SPI_PAD_REPORTS,
    APPLE_SPI_KEY_EVENTS, APPLE_SPI_PAD_EVENTS, APPLE_SPI_RIGHT_EVENTS,
    APPLE_SPI_ERRORS, APPLE_SPI_IRQS, APPLE_SPI_DEVICE_IRQS,
    APPLE_SPI_DEVICE_WAKES, APPLE_SPI_TIMER_WAKES, APPLE_SPI_XFER_WAKES,
    APPLE_SPI_EARLY, APPLE_SPI_NETS, APPLE_SPI_LINE_SKIPS,
    APPLE_SPI_POLL_TICKS, APPLE_SPI_POLL_MAX_TICKS, APPLE_SPI_TRANSPORT,
    APPLE_SPI_DMA_WAIT_RX, APPLE_SPI_DMA_WAIT_TX,
    APPLE_SPI_DMA_WAIT_ACTIVE, APPLE_SPI_DMA_WAIT_OTHER,
    APPLE_SPI_DMA_WAKE_SIO, APPLE_SPI_DMA_WAKE_SPI, APPLE_SPI_DMA_WAKE_DEADLINE,
    APPLE_SPI_IRQ_EVENTS, APPLE_SPI_IRQ_EVENT_TICKS,
    APPLE_SPI_IRQ_EVENT_MAX_TICKS, APPLE_SPI_IRQ_NO_EVENT,
    APPLE_SPI_METRIC_COUNT
};
/* Transport 1 is PIO (with FIFO interrupt assistance when armed), 2 is DMA.
 * DMA_WAIT_* partition returns from Wait during transfer mode by the SIO
 * phase before sleeping. DMA_WAKE_* count the returned signal bits, which
 * can overlap. Neither proves a task context switch: Wait may return an
 * already pending signal. Boot and cleanup waits are excluded here, while
 * XFER_WAKES includes cleanup waits following a failed transfer.
 * Counter-valued metrics inherited
 * from the driver wrap at 32 bits. POLL_TICKS is a 64-bit cumulative duration
 * of PollReport calls, including waits and errors, not CPU or input latency.
 * IRQ_EVENTS counts drain batches where a device attention IRQ was paired
 * with an actual HW_Input_PushEvent return. IRQ_EVENT_TICKS sums the elapsed
 * CNTVCT ticks from handler entry to that first return; MAX is since boot.
 * It includes scheduling, transfer, decoding and any enabled driver logging,
 * but excludes device sampling before the IRQ and downstream display latency.
 * At most one event is paired per IRQ; IRQ_NO_EVENT counts batches discarded
 * without a delivered event (idle, invalid, unchanged or failed reports).
 * Timer-only reads are excluded. These four counters are 64-bit.
 * SAMPLE_COUNTER uses CNTVCT_EL0. Frequency zero means not yet published.
 * IRQ counters are individually sampled during publication, not latched by
 * the hardware at one instant. No reset operation is exposed to readers. */
struct AROSAppleSPIStats {
    uint32_t version;
    uint32_t size;
    uint32_t sequence;
    uint64_t values[APPLE_SPI_METRIC_COUNT];
};

/* The kernel initializes version/size before exposing the storage. Exactly
 * one writer may publish, and the source must not alias the shared values. */
static inline void AROSAppleSPIStatsPublish(struct AROSAppleSPIStats *state,
    const uint64_t values[APPLE_SPI_METRIC_COUNT])
{
    unsigned i;
    uint32_t sequence = __atomic_load_n(&state->sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&state->sequence, sequence + 1U, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    for (i = 0; i < APPLE_SPI_METRIC_COUNT; ++i)
        __atomic_store_n(&state->values[i], values[i], __ATOMIC_RELAXED);
    __atomic_store_n(&state->sequence, sequence + 2U, __ATOMIC_RELEASE);
}

/* Failure leaves an unspecified output; callers must not print it. */
static inline int AROSAppleSPIStatsRead(const struct AROSAppleSPIStats *state,
    uint64_t values[APPLE_SPI_METRIC_COUNT], uint32_t *sequence)
{
    unsigned attempt, i;
    if (state->version != AROS_APPLE_SPI_STATS_VERSION ||
        state->size != sizeof(*state))
        return 0;
    for (attempt = 0; attempt < 16U; ++attempt) {
        uint32_t before = __atomic_load_n(&state->sequence, __ATOMIC_ACQUIRE);
        if (before & 1U)
            continue;
        for (i = 0; i < APPLE_SPI_METRIC_COUNT; ++i)
            values[i] = __atomic_load_n(&state->values[i], __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (before == __atomic_load_n(&state->sequence, __ATOMIC_RELAXED)) {
            *sequence = before;
            return 1;
        }
    }
    return 0;
}
#endif
