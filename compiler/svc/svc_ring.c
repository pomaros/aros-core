/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The service contract's rings and wake accounting; no OS calls.
*/

#include <string.h>

#include <aros/svc.h>

int SvcRingInit(struct SvcRing *ring, void *data, uint32_t capacity, uint32_t unit)
{
    if (!ring || !data || !unit || !capacity || (capacity & (capacity - 1)))
        return -1;
    atomic_store_explicit(&ring->head, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0, memory_order_relaxed);
    ring->capacity = capacity;
    ring->unit = unit;
    ring->data = data;
    return 0;
}

uint32_t SvcRingSpace(struct SvcRing *ring)
{
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);

    return ring->capacity - (head - tail);
}

uint32_t SvcRingFill(struct SvcRing *ring)
{
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);

    return head - tail;
}

/* Copy n units between the ring at index `at` and a flat buffer, wrapping. */
static void copy(struct SvcRing *ring, uint32_t at, void *flat, uint32_t n, int to_ring)
{
    uint32_t mask = ring->capacity - 1, first = ring->capacity - (at & mask);
    uint8_t *slot = ring->data + (size_t)(at & mask) * ring->unit;

    if (first > n)
        first = n;
    if (to_ring) {
        memcpy(slot, flat, (size_t)first * ring->unit);
        memcpy(ring->data, (uint8_t *)flat + (size_t)first * ring->unit, (size_t)(n - first) * ring->unit);
    } else {
        memcpy(flat, slot, (size_t)first * ring->unit);
        memcpy((uint8_t *)flat + (size_t)first * ring->unit, ring->data, (size_t)(n - first) * ring->unit);
    }
}

uint32_t SvcRingWrite(struct SvcRing *ring, const void *src, uint32_t units)
{
    uint32_t space = SvcRingSpace(ring), head;

    if (units > space)
        units = space;
    if (!units)
        return 0;
    head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    copy(ring, head, (void *)src, units, 1);
    /* the data before the index that publishes it */
    atomic_store_explicit(&ring->head, head + units, memory_order_release);
    return units;
}

uint32_t SvcRingRead(struct SvcRing *ring, void *dst, uint32_t units)
{
    uint32_t fill = SvcRingFill(ring), tail;

    if (units > fill)
        units = fill;
    if (!units)
        return 0;
    tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    copy(ring, tail, dst, units, 0);
    /* the copy out before the index that frees the space */
    atomic_store_explicit(&ring->tail, tail + units, memory_order_release);
    return units;
}

void SvcRingFlush(struct SvcRing *ring)
{
    atomic_store_explicit(&ring->tail, atomic_load_explicit(&ring->head, memory_order_acquire),
        memory_order_release);
}

void SvcWake(struct SvcWaker *waker)
{
    uint32_t n;

    if (atomic_exchange_explicit(&waker->pending, 1, memory_order_acq_rel))
        return;                         /* the consumer has not taken the last one */
    n = atomic_fetch_add_explicit(&waker->sent, 1, memory_order_relaxed) + 1;
    if (waker->drop_every && n % waker->drop_every == 0)
        return;                         /* test control: the vehicle loses this one */
    if (waker->deliver)
        waker->deliver(waker);
}

int SvcTake(struct SvcWaker *waker)
{
    if (!atomic_exchange_explicit(&waker->pending, 0, memory_order_acq_rel))
        return 0;
    atomic_fetch_add_explicit(&waker->taken, 1, memory_order_relaxed);
    return 1;
}

int SvcCheckLost(struct SvcWaker *waker)
{
    uint32_t sent = atomic_load_explicit(&waker->sent, memory_order_relaxed);

    if (!atomic_load_explicit(&waker->pending, memory_order_acquire)) {
        waker->check_armed = 0;
        return 0;
    }
    if (!waker->check_armed || waker->check_sent != sent) {
        waker->check_armed = 1;        /* maybe still on its way: look again next time */
        waker->check_sent = sent;
        return 0;
    }
    waker->check_armed = 0;
    if (!SvcTake(waker))
        return 0;
    waker->lost++;
    return 1;
}

void SvcWakerCustom(struct SvcWaker *waker, uint32_t vehicle,
    void (*deliver)(struct SvcWaker *waker), void *target)
{
    waker->vehicle = vehicle;
    atomic_store_explicit(&waker->pending, 0, memory_order_relaxed);
    atomic_store_explicit(&waker->sent, 0, memory_order_relaxed);
    atomic_store_explicit(&waker->taken, 0, memory_order_relaxed);
    waker->lost = 0;
    waker->check_sent = waker->check_armed = 0;
    waker->deliver = deliver;
    waker->target = target;
    waker->mask = 0;
    waker->drop_every = 0;
}
