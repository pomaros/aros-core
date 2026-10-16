/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The service contract's shared-core vehicles: Signal() and Cause().
*/

#include <exec/interrupts.h>
#include <exec/tasks.h>
#include <proto/exec.h>

#include <aros/svc.h>

static void deliver_signal(struct SvcWaker *waker)
{
    Signal((struct Task *)waker->target, waker->mask);
}

static void deliver_softint(struct SvcWaker *waker)
{
    Cause((struct Interrupt *)waker->target);
}

void SvcWakerSignal(struct SvcWaker *waker, struct Task *task, uint32_t mask)
{
    SvcWakerCustom(waker, SVC_VEHICLE_SIGNAL, deliver_signal, task);
    waker->mask = mask;
}

void SvcWakerSoftInt(struct SvcWaker *waker, struct Interrupt *interrupt)
{
    SvcWakerCustom(waker, SVC_VEHICLE_SOFTINT, deliver_softint, interrupt);
}
