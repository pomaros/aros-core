/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The service contract: single-producer single-consumer rings and
          one wake operation, whatever runs the service.

    A service (audio first) talks to the rest of the system only through
    rings in memory it owns and through SvcWake(). The vehicle that runs it
    decides what a wake is: Signal() to a task or Cause() of a software
    interrupt when the service shares a core with the classic domain, an
    IPI or SEV on a dedicated core, a condition variable on a hosted
    thread. The service code is the same in every case.

    Rings count units (frames, requests) with free-running 32-bit indices
    and C11 acquire/release ordering, so they behave the same on one core
    and on eight. Each ring has one producer and one consumer.

    A wake is sent only when none is pending, and the consumer takes it
    before draining: sent == taken exactly, so a wake the vehicle lost shows
    as a wake still pending when the consumer's own timeout expires twice
    (lost).
*/

#ifndef AROS_SVC_H
#define AROS_SVC_H

#include <stdint.h>
#include <stdatomic.h>

struct SvcRing
{
    _Atomic uint32_t head;              /* units produced, written by the producer */
    uint32_t pad0[15];                  /* the indices on separate cache lines */
    _Atomic uint32_t tail;              /* units consumed, written by the consumer */
    uint32_t pad1[15];
    uint32_t capacity;                  /* units, a power of two */
    uint32_t unit;                      /* bytes per unit */
    uint8_t *data;
};

/* 0, or -1 if capacity is not a power of two or an argument is missing. */
int SvcRingInit(struct SvcRing *ring, void *data, uint32_t capacity, uint32_t unit);
/* Producer side. */
uint32_t SvcRingSpace(struct SvcRing *ring);
uint32_t SvcRingWrite(struct SvcRing *ring, const void *src, uint32_t units);
/* Consumer side. */
uint32_t SvcRingFill(struct SvcRing *ring);
uint32_t SvcRingRead(struct SvcRing *ring, void *dst, uint32_t units);
/* Consumer: drop everything queued (after a stop). */
void SvcRingFlush(struct SvcRing *ring);

/* The vehicles. */
enum SvcVehicle
{
    SVC_VEHICLE_SIGNAL = 1,             /* shared core: Signal() a task */
    SVC_VEHICLE_SOFTINT,                /* shared core: Cause() an interrupt (audio) */
    SVC_VEHICLE_DEDICATED,              /* dedicated core: IPI or SEV */
    SVC_VEHICLE_HOSTED                  /* host thread: the host's primitive */
};

struct SvcWaker
{
    uint32_t vehicle;
    _Atomic uint32_t pending;           /* a wake is on its way */
    _Atomic uint32_t sent;              /* wakes handed to the vehicle */
    _Atomic uint32_t taken;             /* wakes the consumer took */
    uint32_t lost;                      /* wakes the consumer found pending across two timeouts */
    uint32_t check_sent, check_armed;   /* the pending wake seen at the last timeout */
    void (*deliver)(struct SvcWaker *waker);  /* the vehicle's operation */
    void *target;                       /* Task, Interrupt, or host object */
    uint32_t mask;                      /* signal mask for SVC_VEHICLE_SIGNAL */
    uint32_t drop_every;                /* test control: lose every n-th wake; 0 never */
};

/* Producer: wake the consumer unless a wake is already pending. */
void SvcWake(struct SvcWaker *waker);
/* Consumer, on being woken: take the wake before draining. 1 if one was pending. */
int SvcTake(struct SvcWaker *waker);
/* Consumer, when its timeout expired without a wake. A wake pending at two
 * consecutive timeouts was dropped by the vehicle: it is taken, counted as
 * lost, and 1 returned. One seen only once may still be on its way. */
int SvcCheckLost(struct SvcWaker *waker);

/* Exec vehicles (shared core). */
struct Task;
struct Interrupt;
void SvcWakerSignal(struct SvcWaker *waker, struct Task *task, uint32_t mask);
void SvcWakerSoftInt(struct SvcWaker *waker, struct Interrupt *interrupt);
/* Any other vehicle: its own deliver function and target. */
void SvcWakerCustom(struct SvcWaker *waker, uint32_t vehicle,
    void (*deliver)(struct SvcWaker *waker), void *target);

#endif /* AROS_SVC_H */
