/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL), Version 1.1.
 *
 * Concurrent read-only NVMe requests with independent buffers and handles.
 * Raw unit zero; no write commands. Parent context and code survive until every child is reaped.
 */
#include <exec/tasks.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <proto/timer.h>
#include <aros/debug.h>
#if defined(__aarch64__)
#include <aros/aarch64/cpucontext.h>
#endif
#include <string.h>

APTR KernelBase;
struct Device *TimerBase;

#if defined(__AROSEXEC_SMP__)
#define WORKERS 4
#define ROUNDS 64
#define BLOCK 4096UL
#define REFERENCE_SIZE (32UL * BLOCK)
static UBYTE *reference;
static ULONG active_reads, peak_reads;
#define STACKSIZE (256UL * 1024UL)
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while (0)

struct Worker
{
    struct Task *parent, *task;
    ULONG parent_signal, go_signal, tid, cpu, first_cpu, last_cpu;
    ULONG reads, checks, done, stage;
    LONG result, error;
    struct EClockVal begin, end;
};

/* Diagnostic observer uses its own pre-opened timer and the kernel console;
 * it never calls DOS while the workload is running. The parent cannot exit
 * or unload this image until the observer has also been reaped. */
struct Observer
{
    struct Task *parent;
    struct Worker *workers;
    ULONG signal, tid, ready, stop, stage;
};

static LONG Observe(void)
{
    struct Task *self = FindTask(NULL);
    struct Observer *o = self->tc_UserData;
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *timer = port ?
        (struct timerequest *)CreateIORequest(port, sizeof(*timer)) : NULL;
    BOOL opened = timer && !OpenDevice("timer.device", UNIT_MICROHZ, &timer->tr_node, 0);
    o->ready = opened ? 1 : 2;
    __atomic_store_n(&o->tid, self->tc_UnionETask.tc_ETask->et_UniqueID, __ATOMIC_RELEASE);
    Signal(o->parent, o->signal);
    if (opened)
    {
        for (ULONG sample = 0; sample < 5 &&
             !__atomic_load_n(&o->stop, __ATOMIC_ACQUIRE); sample++)
        {
            timer->tr_node.io_Command = TR_ADDREQUEST;
            timer->tr_time.tv_secs = 2;
            timer->tr_time.tv_micro = 0;
            DoIO(&timer->tr_node);
            if (__atomic_load_n(&o->stop, __ATOMIC_ACQUIRE))
                break;
#if defined(__aarch64__)
            struct ExceptionContext *ctx = o->parent->tc_UnionETask.tc_ETask->et_RegFrame;
            bug("[SMP-NVMeParallel] observer stage=%lu parent=%p state=%lu pc=%p lr=%p sp=%p fp=%p wait=%08lx recvd=%08lx\n",
                (unsigned long)__atomic_load_n(&o->stage, __ATOMIC_ACQUIRE),
                o->parent, (unsigned long)o->parent->tc_State,
                ctx ? (APTR)(IPTR)ctx->pc : NULL, ctx ? (APTR)(IPTR)ctx->lr : NULL,
                ctx ? (APTR)(IPTR)ctx->sp : NULL, ctx ? (APTR)(IPTR)ctx->fp : NULL,
                (unsigned long)o->parent->tc_SigWait, (unsigned long)o->parent->tc_SigRecvd);
#else
            bug("[SMP-NVMeParallel] observer stage=%lu parent=%p state=%lu\n",
                (unsigned long)__atomic_load_n(&o->stage, __ATOMIC_ACQUIRE),
                o->parent, (unsigned long)o->parent->tc_State);
#endif
            for (ULONG i = 0; i < WORKERS; i++)
                bug("[SMP-NVMeParallel] observer worker=%lu stage=%lu\n",
                    (unsigned long)i,
                    (unsigned long)__atomic_load_n(&o->workers[i].stage, __ATOMIC_ACQUIRE));
        }
        CloseDevice(&timer->tr_node);
    }
    if (timer)
        DeleteIORequest(&timer->tr_node);
    if (port)
        DeleteMsgPort(port);
    return opened ? RETURN_OK : RETURN_FAIL;
}

static BOOL read_bytes(struct IOStdReq *io, UBYTE *buffer, ULONG offset, ULONG length)
{
    io->io_Command = NSCMD_TD_READ64;
    io->io_Data = buffer;
    io->io_Length = length;
    io->io_Offset = offset;
    io->io_Actual = 0;
    return DoIO((struct IORequest *)io) == 0 && io->io_Actual == length;
}

static BOOL protected_device(struct IOStdReq *io)
{
    io->io_Command = TD_PROTSTATUS;
    return DoIO((struct IORequest *)io) == 0 && io->io_Actual == 1;
}

static BOOL seed_reference(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOExtTD *request = port ? (struct IOExtTD *)CreateIORequest(port, sizeof(*request)) : NULL;
    BOOL opened = request && !OpenDevice("applenvme.device", 0, (struct IORequest *)request, 0);
    UBYTE *check = AllocMem(REFERENCE_SIZE, MEMF_PUBLIC);
    BOOL ok = FALSE;
    if (opened && check && protected_device(&request->iotd_Req))
        ok = read_bytes(&request->iotd_Req, reference, 0, REFERENCE_SIZE) &&
             read_bytes(&request->iotd_Req, check, 0, REFERENCE_SIZE) &&
             !memcmp(reference, check, REFERENCE_SIZE) &&
             !memcmp(reference + BLOCK, "EFI PART", 8);
    if (check) FreeMem(check, REFERENCE_SIZE);
    if (opened) CloseDevice((struct IORequest *)request);
    if (request) DeleteIORequest((struct IORequest *)request);
    if (port) DeleteMsgPort(port);
    return ok;
}

static LONG ReadChild(void)
{
    struct Task *self = FindTask(NULL);
    struct Worker *w = self->tc_UserData;
    BYTE go_bit = AllocSignal(-1);
    struct MsgPort *port = CreateMsgPort();
    struct IOExtTD *request = port ? (struct IOExtTD *)CreateIORequest(port, sizeof(*request)) : NULL;
    BOOL opened = request && !OpenDevice("applenvme.device", 0, (struct IORequest *)request, 0);
    UBYTE *buffer = AllocMem(8 * BLOCK + 2, MEMF_PUBLIC);
    BOOL ready = opened && buffer && protected_device(&request->iotd_Req);

    __atomic_store_n(&w->stage, 1, __ATOMIC_RELEASE);
    w->task = self;
    w->first_cpu = KrnGetCPUNumber();
    w->result = RETURN_FAIL;
    if (go_bit >= 0)
    {
        w->go_signal = 1UL << go_bit;
        SetSignal(0, w->go_signal);
    }
    __atomic_store_n(&w->tid, self->tc_UnionETask.tc_ETask->et_UniqueID,
                     __ATOMIC_RELEASE);
    Signal(w->parent, w->parent_signal);
    if (go_bit >= 0)
    {
        /* The parent may safely Signal task exactly once: the child cannot
         * leave this wait before that signal, even if it arrived early. */
        __atomic_store_n(&w->stage, 2, __ATOMIC_RELEASE);
        Wait(w->go_signal);
        __atomic_store_n(&w->stage, 3, __ATOMIC_RELEASE);
        ReadEClock(&w->begin);
        for (ULONG i = 0; ready && i < ROUNDS; i++)
        {
            __atomic_store_n(&w->stage, 100 + i * 2, __ATOMIC_RELEASE);
            ULONG offset = ((i * 7 + w->cpu) % 20) * BLOCK + (i % 2 ? 13 : 0);
            ULONG length = i % 3 == 0 ? 17 : i % 3 == 1 ? BLOCK : 8 * BLOCK;
            ULONG active, peak;
            memset(buffer, 0xda, 8 * BLOCK + 2);
            active = __atomic_add_fetch(&active_reads, 1, __ATOMIC_ACQ_REL);
            peak = __atomic_load_n(&peak_reads, __ATOMIC_RELAXED);
            while (active > peak && !__atomic_compare_exchange_n(&peak_reads,
                       &peak, active, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
            BOOL read_ok = read_bytes(&request->iotd_Req, buffer + 1, offset, length);
            __atomic_sub_fetch(&active_reads, 1, __ATOMIC_ACQ_REL);
            w->error = request->iotd_Req.io_Error;
            w->result = RETURN_FAIL;
            if (!read_ok) break;
            w->reads++;
            __atomic_store_n(&w->stage, 101 + i * 2, __ATOMIC_RELEASE);
            if (buffer[0] != 0xda || buffer[length + 1] != 0xda ||
                memcmp(buffer + 1, reference + offset, length)) break;
            w->result = RETURN_OK;
            w->checks++;
        }
        ReadEClock(&w->end);
        FreeSignal(go_bit);
    }
    if (buffer) FreeMem(buffer, 8 * BLOCK + 2);
    if (opened) CloseDevice((struct IORequest *)request);
    if (request) DeleteIORequest((struct IORequest *)request);
    if (port) DeleteMsgPort(port);
    __atomic_store_n(&w->stage, 300, __ATOMIC_RELEASE);
    w->last_cpu = KrnGetCPUNumber();
    __atomic_store_n(&w->done, 1, __ATOMIC_RELEASE);
    return w->result;
}

static UQUAD ticks(const struct EClockVal *value)
{
    return ((UQUAD)value->ev_hi << 32) | value->ev_lo;
}

int main(void)
{
    static const ULONG cpus[WORKERS] = {1, 3, 4, 7};
    struct Worker workers[WORKERS] = {0};
    struct Observer observer = {0};
    struct MsgPort *port;
    struct timerequest *timer;
    struct EClockVal now;
    BYTE signal;
    ULONG created = 0, frequency;
    UQUAD begin = 0, end = ~(UQUAD)0;
    LONG result = RETURN_FAIL;
    BOOL ok = TRUE;

    KernelBase = OpenResource("kernel.resource");
    if (!KernelBase || KrnGetCPUCount() != 8 ||
        !(FindTask(NULL)->tc_Flags & TF_ETASK))
    {
        PutStr("SMP-NVMeParallel requires eight online CPUs and an ETask parent\n");
        return RETURN_FAIL;
    }
    reference = AllocMem(REFERENCE_SIZE, MEMF_PUBLIC);
    if (!reference) return RETURN_FAIL;
    if (!seed_reference())
    {
        FreeMem(reference, REFERENCE_SIZE);
        PutStr("SMP-NVMeParallel: read-only reference failed\n");
        return RETURN_FAIL;
    }
    port = CreateMsgPort();
    if (!port)
    {
        FreeMem(reference, REFERENCE_SIZE);
        return RETURN_FAIL;
    }
    timer = (struct timerequest *)CreateIORequest(port, sizeof(*timer));
    if (!timer)
        goto free_port;
    if (OpenDevice("timer.device", UNIT_MICROHZ, &timer->tr_node, 0))
        goto free_request;
    TimerBase = timer->tr_node.io_Device;
    frequency = ReadEClock(&now);
    signal = AllocSignal(-1);
    if (signal < 0)
        goto close_timer;

    observer.parent = FindTask(NULL);
    observer.workers = workers;
    observer.signal = 1UL << signal;
    if (!CreateNewProcTags(NP_Entry, (IPTR)Observe, NP_Name, (IPTR)"SMP-NVMeObserver",
            NP_UserData, (IPTR)&observer, NP_StackSize, STACKSIZE,
            NP_NotifyOnDeath, TRUE, TAG_DONE))
    {
        FreeSignal(signal);
        goto close_timer;
    }
    while (!__atomic_load_n(&observer.tid, __ATOMIC_ACQUIRE))
        Wait(observer.signal);
    if (observer.ready != 1)
        goto reap_observer;

    REPORT("[SMP-NVMeParallel] start workers=4 rounds=64 reference=131072 protected=1 frequency=%lu\n",
           (unsigned long)frequency);
    Flush(Output());
    for (ULONG i = 0; i < WORKERS; i++)
    {
        struct Worker *w = &workers[i];
        APTR affinity;
        w->cpu = cpus[i];
        w->parent = FindTask(NULL);
        w->parent_signal = 1UL << signal;
        affinity = KrnAllocCPUMask();
        if (!affinity)
            break;
        KrnClearCPUMask(affinity);
        KrnGetCPUMask(w->cpu, affinity);
        if (!CreateNewProcTags(NP_Entry, (IPTR)ReadChild,
            NP_Name, (IPTR)"SMP-NVMeReader", NP_StackSize, STACKSIZE,
            NP_UserData, (IPTR)w, NP_Affinity, (IPTR)affinity,
            NP_Cli, TRUE, NP_NotifyOnDeath, TRUE, TAG_DONE))
        {
            KrnFreeCPUMask(affinity);
            break;
        }
        created++;
        __atomic_store_n(&observer.stage, 12 + i * 3, __ATOMIC_RELEASE);
    }
    /* Failure to create a later child does not strand earlier children or
     * unload their live code. Release and reap every successful creation. */
    __atomic_store_n(&observer.stage, 30, __ATOMIC_RELEASE);
    for (ULONG i = 0; i < created; i++)
        while (!__atomic_load_n(&workers[i].tid, __ATOMIC_ACQUIRE))
            Wait(1UL << signal);
    __atomic_store_n(&observer.stage, 31, __ATOMIC_RELEASE);
    for (ULONG i = 0; i < created; i++)
        if (workers[i].go_signal)
            Signal(workers[i].task, workers[i].go_signal);
    __atomic_store_n(&observer.stage, 32, __ATOMIC_RELEASE);
    for (ULONG i = 0; i < created; i++)
    {
        struct Worker *w = &workers[i];
        struct ETask *exited = (struct ETask *)ChildWait(w->tid);
        if ((IPTR)exited <= CHILD_ACTIVE)
        {
            REPORT("[SMP-NVMeParallel] FAIL child wait; retaining live context\n");
            for (;;)
                Wait(0);
        }
        ChildFree(exited->et_UniqueID);
        if (!__atomic_load_n(&w->done, __ATOMIC_ACQUIRE) ||
            w->result || w->reads != ROUNDS || w->checks != ROUNDS ||
            w->first_cpu != w->cpu || w->last_cpu != w->cpu)
            ok = FALSE;
        if (ticks(&w->begin) > begin)
            begin = ticks(&w->begin);
        if (ticks(&w->end) < end)
            end = ticks(&w->end);
        REPORT("[SMP-NVMeParallel] worker cpu=%lu first=%lu last=%lu reads=%lu checks=%lu rc=%ld ioerr=%ld begin=%08lx%08lx end=%08lx%08lx reaped=1\n",
            (unsigned long)w->cpu, (unsigned long)w->first_cpu,
            (unsigned long)w->last_cpu, (unsigned long)w->reads,
            (unsigned long)w->checks, (long)w->result, (long)w->error,
            (unsigned long)w->begin.ev_hi, (unsigned long)w->begin.ev_lo,
            (unsigned long)w->end.ev_hi, (unsigned long)w->end.ev_lo);
    }
    if (created == WORKERS && ok && frequency && end > begin && peak_reads > 1)
        result = RETURN_OK;
    REPORT("[SMP-NVMeParallel] result=%s created=%lu peak=%lu overlap=%08lx%08lx\n",
        result == RETURN_OK ? "PASS" : "FAIL", (unsigned long)created, (unsigned long)peak_reads,
        (unsigned long)((end > begin ? end - begin : 0) >> 32),
        (unsigned long)((end > begin ? end - begin : 0) & 0xffffffffUL));
    Flush(Output());
reap_observer:
    __atomic_store_n(&observer.stop, 1, __ATOMIC_RELEASE);
    {
        struct ETask *exited = (struct ETask *)ChildWait(observer.tid);
        if ((IPTR)exited <= CHILD_ACTIVE)
            for (;;)
                Wait(0);
        ChildFree(exited->et_UniqueID);
    }
    FreeSignal(signal);
close_timer:
    CloseDevice(&timer->tr_node);
free_request:
    DeleteIORequest(&timer->tr_node);
free_port:
    DeleteMsgPort(port);
    FreeMem(reference, REFERENCE_SIZE);
    return result;
}
#else
int main(void)
{
    return RETURN_FAIL;
}
#endif
