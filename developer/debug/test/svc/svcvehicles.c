/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: SvcVehicles - [SMP13] on AROS: the synthetic service in the
          shared vehicle (engine and client as tasks on the bound core,
          Signal() as SvcWake), on a dedicated core when there is more than
          one CPU (the engine created with an affinity to the last CPU), and
          the shared vehicle's dropped-wake control. One line per run and a
          verdict line; the digest must equal the host gate's.
*/

#include <exec/tasks.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>

#include <stdio.h>
#include <string.h>

#include "svc_synth.h"

APTR KernelBase;

struct Side {
    struct Task *task;
    BYTE signal;
    struct MsgPort *port;
    struct timerequest *timer;
};

static struct Side engine_side, client_side;
static struct SvcSynth synth;
static struct Task *parent;
static BYTE done_signal;

static struct Side *side_of(struct SvcSynth *s, struct SvcWaker *w)
{
    return w == &s->to_engine ? &engine_side : &client_side;
}

static int side_open(struct Side *side)
{
    side->task = FindTask(NULL);
    side->signal = AllocSignal(-1);
    side->port = CreateMsgPort();
    side->timer = side->port ? (struct timerequest *)CreateIORequest(side->port, sizeof(*side->timer)) : NULL;
    if (side->signal == -1 || !side->timer || OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)side->timer, 0))
        return -1;
    return 0;
}

static void side_close(struct Side *side)
{
    if (side->timer) {
        if (side->timer->tr_node.io_Device)
            CloseDevice((struct IORequest *)side->timer);
        DeleteIORequest((struct IORequest *)side->timer);
    }
    if (side->port)
        DeleteMsgPort(side->port);
    if (side->signal != -1)
        FreeSignal(side->signal);
    memset(side, 0, sizeof(*side));
    side->signal = -1;
}

int svc_synth_wait(struct SvcSynth *s, struct SvcWaker *w, uint32_t timeout_ms)
{
    struct Side *side = side_of(s, w);
    ULONG wake = 1UL << side->signal, tick = 1UL << side->port->mp_SigBit, got;

    side->timer->tr_node.io_Command = TR_ADDREQUEST;
    side->timer->tr_time.tv_secs = 0;
    side->timer->tr_time.tv_micro = timeout_ms * 1000;
    SendIO((struct IORequest *)side->timer);
    got = Wait(wake | tick);
    if (!CheckIO((struct IORequest *)side->timer))
        AbortIO((struct IORequest *)side->timer);
    WaitIO((struct IORequest *)side->timer);
    SetSignal(0, tick);
    if (got & wake) {
        SvcTake(w);
        return 1;
    }
    return 0;
}

static void engine_entry(void)
{
    if (side_open(&engine_side) == 0) {
        /* the client may only wake us once we can be woken */
        SvcWakerSignal(&synth.to_engine, engine_side.task, 1UL << engine_side.signal);
        Signal(parent, 1UL << done_signal);
        svc_synth_engine(&synth);
    }
    side_close(&engine_side);
    Forbid();
    Signal(parent, 1UL << done_signal);
}

static int run(const char *name, uint32_t drop_every, int cpu, uint32_t *digest)
{
    struct Task *engine;
    struct TagItem tags[] = {
        { TASKTAG_PC, (IPTR)engine_entry },
        { TASKTAG_NAME, (IPTR)"SvcVehicles engine" },
        { TASKTAG_PRI, 5 },
        { TASKTAG_STACKSIZE, 32768 },
        { cpu >= 0 ? TASKTAG_AFFINITY : TAG_IGNORE, 0 },
        { TAG_DONE, 0 }
    };
    void *mask = NULL;
    ULONG lost;

    memset(&synth, 0, sizeof(synth));
    svc_synth_init(&synth);
    engine_side.signal = -1;
    if (side_open(&client_side)) {
        side_close(&client_side);
        return -1;
    }
    SvcWakerSignal(&synth.to_client, client_side.task, 1UL << client_side.signal);
    synth.to_client.drop_every = drop_every;
    if (cpu >= 0) {
        mask = KrnAllocCPUMask();
        KrnGetCPUMask(cpu, mask);
        tags[4].ti_Data = (IPTR)mask;
    }
    SetSignal(0, 1UL << done_signal);
    engine = NewCreateTaskA(tags);
    if (!engine) {
        side_close(&client_side);
        return -1;
    }
    Wait(1UL << done_signal);           /* engine side ready */
    synth.to_engine.drop_every = drop_every;
    *digest = svc_synth_client(&synth);
    Wait(1UL << done_signal);           /* engine gone */
    lost = synth.to_engine.lost + synth.to_client.lost;
    printf("[SMP13] vehicle=%s digest=%08lx lost-wakes=%lu ring-overruns=%lu idle-spin=%lu "
        "wakes=%lu/%lu taken=%lu/%lu timeouts=%lu/%lu\n", name, (unsigned long)*digest,
        (unsigned long)lost, (unsigned long)synth.overruns, (unsigned long)synth.spins,
        (unsigned long)synth.to_engine.sent, (unsigned long)synth.to_client.sent,
        (unsigned long)synth.to_engine.taken, (unsigned long)synth.to_client.taken,
        (unsigned long)synth.timeouts_engine, (unsigned long)synth.timeouts_client);
    side_close(&client_side);
    return (int)lost;
}

int main(void)
{
    uint32_t shared = 0, dedicated = 0, control = 0;
    int lost_shared, lost_dedicated = 0, lost_control;
    unsigned int cpus = 1;
    const char *configs = "shared";

    KernelBase = OpenResource("kernel.resource");
    parent = FindTask(NULL);
    done_signal = AllocSignal(-1);
    if (done_signal == -1)
        return RETURN_FAIL;
    if (KernelBase)
        cpus = KrnGetCPUCount();
    lost_shared = run("shared", 0, -1, &shared);
    if (cpus > 1) {
        lost_dedicated = run("dedicated", 0, (int)cpus - 1, &dedicated);
        configs = "dedicated,shared";
    } else {
        dedicated = shared;
    }
    lost_control = run("shared-control-drop-100", 100, -1, &control);
    FreeSignal(done_signal);
    if (lost_shared == 0 && lost_dedicated == 0 && lost_control > 0 && shared == dedicated && shared == control) {
        printf("[SMP13] PASS vehicles: configs=[%s] output-digest-equal=1 lost-wakes=0 ring-overruns=0 "
            "idle-spin=0 digest=%08lx cpus=%u\n", configs, (unsigned long)shared, cpus);
        printf("[SMP13] (control) dropped-wakes: lost-wakes=%d\n", lost_control);
        return RETURN_OK;
    }
    printf("[SMP13] FAIL vehicles: configs=[%s] digests=%08lx/%08lx/%08lx lost=%d/%d control-lost=%d cpus=%u\n",
        configs, (unsigned long)shared, (unsigned long)dedicated, (unsigned long)control,
        lost_shared, lost_dedicated, lost_control, cpus);
    return RETURN_ERROR;
}
