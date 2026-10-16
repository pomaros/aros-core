/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL), Version 1.1.
 *
 * Concurrent real DOS commands, with independent loaded segments and handles.
 * RAM only. Parent context and code survive until every child is reaped.
 */
#include <exec/tasks.h>
#include <exec/io.h>
#include <devices/timer.h>
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
#define ROUNDS 16
#define FILESIZE (256UL * 1024UL)
#define STACKSIZE (256UL * 1024UL)
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while (0)

struct Worker
{
    BPTR segment;
    const char *args, *path;
    struct Task *parent, *task;
    ULONG parent_signal, go_signal, tid, cpu, first_cpu, last_cpu;
    ULONG copies, checks, done, stage;
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
            bug("[SMP-DOSParallel] observer stage=%lu parent=%p state=%lu pc=%p lr=%p sp=%p fp=%p wait=%08lx recvd=%08lx\n",
                (unsigned long)__atomic_load_n(&o->stage, __ATOMIC_ACQUIRE),
                o->parent, (unsigned long)o->parent->tc_State,
                ctx ? (APTR)(IPTR)ctx->pc : NULL, ctx ? (APTR)(IPTR)ctx->lr : NULL,
                ctx ? (APTR)(IPTR)ctx->sp : NULL, ctx ? (APTR)(IPTR)ctx->fp : NULL,
                (unsigned long)o->parent->tc_SigWait, (unsigned long)o->parent->tc_SigRecvd);
#else
            bug("[SMP-DOSParallel] observer stage=%lu parent=%p state=%lu\n",
                (unsigned long)__atomic_load_n(&o->stage, __ATOMIC_ACQUIRE),
                o->parent, (unsigned long)o->parent->tc_State);
#endif
            for (ULONG i = 0; i < WORKERS; i++)
                bug("[SMP-DOSParallel] observer worker=%lu stage=%lu\n",
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

static UBYTE pattern(ULONG offset)
{
    return (UBYTE)((offset * 73UL + 19UL) ^ (offset >> 8));
}

static BOOL seed_file(void)
{
    UBYTE buffer[4096];
    BPTR file = Open("RAM:SMP-Parallel/seed", MODE_NEWFILE);
    BOOL ok = file != BNULL;
    if (!file)
        return FALSE;
    for (ULONG offset = 0; ok && offset < FILESIZE; offset += sizeof(buffer))
    {
        for (ULONG i = 0; i < sizeof(buffer); i++)
            buffer[i] = pattern(offset + i);
        ok = Write(file, buffer, sizeof(buffer)) == sizeof(buffer);
    }
    if (!Close(file))
        ok = FALSE;
    return ok;
}

static BOOL check_file(const char *path)
{
    UBYTE buffer[4096];
    BPTR file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    BOOL ok = file != BNULL;
    if (!file)
        return FALSE;
    for (ULONG offset = 0; ok && offset < FILESIZE; offset += sizeof(buffer))
    {
        ok = Read(file, buffer, sizeof(buffer)) == sizeof(buffer);
        for (ULONG i = 0; ok && i < sizeof(buffer); i++)
            ok = buffer[i] == pattern(offset + i);
    }
    if (ok)
        ok = Read(file, buffer, 1) == 0;
    if (!Close(file))
        ok = FALSE;
    return ok;
}

static LONG CopyChild(void)
{
    struct Task *self = FindTask(NULL);
    struct Worker *w = self->tc_UserData;
    BYTE go_bit = AllocSignal(-1);

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
        for (ULONG i = 0; i < ROUNDS; i++)
        {
            __atomic_store_n(&w->stage, 100 + i * 2, __ATOMIC_RELEASE);
            w->result = RunCommand(w->segment, STACKSIZE,
                                  (STRPTR)w->args, strlen(w->args));
            w->error = IoErr();
            if (w->result != RETURN_OK)
                break;
            w->copies++;
            __atomic_store_n(&w->stage, 101 + i * 2, __ATOMIC_RELEASE);
            if (!check_file(w->path))
            {
                w->error = IoErr();
                w->result = RETURN_FAIL;
                break;
            }
            w->checks++;
        }
        ReadEClock(&w->end);
        FreeSignal(go_bit);
    }
    __atomic_store_n(&w->stage, 200, __ATOMIC_RELEASE);
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
    static const char *paths[WORKERS] = {
        "RAM:SMP-Parallel/out-1", "RAM:SMP-Parallel/out-3",
        "RAM:SMP-Parallel/out-4", "RAM:SMP-Parallel/out-7"
    };
    static const char *arguments[WORKERS] = {
        "RAM:SMP-Parallel/seed RAM:SMP-Parallel/out-1 QUIET\n",
        "RAM:SMP-Parallel/seed RAM:SMP-Parallel/out-3 QUIET\n",
        "RAM:SMP-Parallel/seed RAM:SMP-Parallel/out-4 QUIET\n",
        "RAM:SMP-Parallel/seed RAM:SMP-Parallel/out-7 QUIET\n"
    };
    struct Worker workers[WORKERS] = {0};
    struct Observer observer = {0};
    struct MsgPort *port;
    struct timerequest *timer;
    struct EClockVal now;
    BPTR directory;
    BYTE signal;
    ULONG created = 0, frequency;
    UQUAD begin = 0, end = ~(UQUAD)0;
    LONG result = RETURN_FAIL;
    BOOL ok = TRUE;

    KernelBase = OpenResource("kernel.resource");
    if (!KernelBase || KrnGetCPUCount() != 8 ||
        !(FindTask(NULL)->tc_Flags & TF_ETASK))
    {
        PutStr("SMP-DOSParallel requires eight online CPUs and an ETask parent\n");
        return RETURN_FAIL;
    }
    /* A pre-existing directory belongs to somebody else: never overwrite it. */
    directory = Lock("RAM:SMP-Parallel", SHARED_LOCK);
    if (directory)
    {
        UnLock(directory);
        PutStr("SMP-DOSParallel: RAM:SMP-Parallel already exists\n");
        return RETURN_FAIL;
    }
    directory = CreateDir("RAM:SMP-Parallel");
    if (!directory)
        return RETURN_FAIL;
    UnLock(directory);
    if (!seed_file())
        return RETURN_FAIL;
    port = CreateMsgPort();
    if (!port)
        return RETURN_FAIL;
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
    if (!CreateNewProcTags(NP_Entry, (IPTR)Observe, NP_Name, (IPTR)"SMP-DOSObserver",
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

    REPORT("[SMP-DOSParallel] start workers=4 rounds=16 bytes=262144 frequency=%lu\n",
           (unsigned long)frequency);
    Flush(Output());
    for (ULONG i = 0; i < WORKERS; i++)
    {
        struct Worker *w = &workers[i];
        APTR affinity;
        w->cpu = cpus[i];
        w->path = paths[i];
        w->args = arguments[i];
        w->parent = FindTask(NULL);
        w->parent_signal = 1UL << signal;
        /* Each ELF has its own writable data and startup/library state. */
        __atomic_store_n(&observer.stage, 10 + i * 3, __ATOMIC_RELEASE);
        w->segment = LoadSeg("C:Copy");
        __atomic_store_n(&observer.stage, 11 + i * 3, __ATOMIC_RELEASE);
        if (!w->segment)
            break;
        affinity = KrnAllocCPUMask();
        if (!affinity)
            break;
        KrnClearCPUMask(affinity);
        KrnGetCPUMask(w->cpu, affinity);
        if (!CreateNewProcTags(NP_Entry, (IPTR)CopyChild,
            NP_Name, (IPTR)paths[i], NP_StackSize, STACKSIZE,
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
            REPORT("[SMP-DOSParallel] FAIL child wait; retaining live context\n");
            for (;;)
                Wait(0);
        }
        ChildFree(exited->et_UniqueID);
        if (!__atomic_load_n(&w->done, __ATOMIC_ACQUIRE) ||
            w->result || w->copies != ROUNDS || w->checks != ROUNDS ||
            w->first_cpu != w->cpu || w->last_cpu != w->cpu)
            ok = FALSE;
        if (ticks(&w->begin) > begin)
            begin = ticks(&w->begin);
        if (ticks(&w->end) < end)
            end = ticks(&w->end);
        REPORT("[SMP-DOSParallel] worker cpu=%lu first=%lu last=%lu copies=%lu checks=%lu rc=%ld ioerr=%ld begin=%08lx%08lx end=%08lx%08lx reaped=1\n",
            (unsigned long)w->cpu, (unsigned long)w->first_cpu,
            (unsigned long)w->last_cpu, (unsigned long)w->copies,
            (unsigned long)w->checks, (long)w->result, (long)w->error,
            (unsigned long)w->begin.ev_hi, (unsigned long)w->begin.ev_lo,
            (unsigned long)w->end.ev_hi, (unsigned long)w->end.ev_lo);
    }
    if (created == WORKERS && ok && frequency && end > begin)
        result = RETURN_OK;
    REPORT("[SMP-DOSParallel] result=%s created=%lu overlap=%08lx%08lx\n",
        result == RETURN_OK ? "PASS" : "FAIL", (unsigned long)created,
        (unsigned long)((end > begin ? end - begin : 0) >> 32),
        (unsigned long)((end > begin ? end - begin : 0) & 0xffffffffUL));
    Flush(Output());
    for (ULONG i = 0; i < WORKERS; i++)
        if (workers[i].segment)
            UnLoadSeg(workers[i].segment);
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
    return result;
}
#else
int main(void)
{
    return RETURN_FAIL;
}
#endif
