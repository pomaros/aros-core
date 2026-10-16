/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Actual task.resource accounting, UP only. No private accounting fields are
    read: even the currently running task is found through the public list.
*/
#include <exec/tasks.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <resources/task.h>
#include <proto/exec.h>
#include <proto/task.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>
#include <aros/debug.h>
#include <stdio.h>
#include <string.h>

/* This CLI/headless command never receives a Workbench startup message.
 * Match the existing pipe/settings probes instead of waiting before main. */
int __nowbsupport = 1;
int __nostdiowin = 1;
int __nocommandline = 1;
struct WBStartup *WBenchMsg;

APTR TaskResBase;
struct Device *TimerBase;
static volatile ULONG running;
static volatile UQUAD iterations;
struct sample { UQUAD cpu; ULONG usage; };

static UQUAD clock_us(void)
{
    struct EClockVal clock;
    ULONG frequency = ReadEClock(&clock);
    UQUAD ticks = ((UQUAD)clock.ev_hi << 32) | clock.ev_lo;
    return ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency;
}

/* Find live entries while holding the resource's documented list lock. Never
 * query an address retained after exit, including during churn/address reuse. */
static int sample_task(struct Task *wanted, const char *name, struct sample *out)
{
    struct TaskList *list = LockTaskList(LTF_ALL);
    struct Task *task;
    int found = 0;
    if (!list) return -1;
    while ((task = NextTaskEntry(list, LTF_ALL))) {
        if ((wanted && task == wanted) ||
            (!wanted && task->tc_Node.ln_Name && !strcmp(task->tc_Node.ln_Name, name))) {
            struct timeval cpu = {0};
            ULONG usage = 0;
            struct TagItem tags[] = {
                {TaskTag_CPUTime, (IPTR)&cpu},
                {TaskTag_CPUUsage, (IPTR)&usage}, {TAG_DONE, 0}
            };
            QueryTaskTagList(task, tags);
            if (cpu.tv_micro >= 1000000) { found = -1; break; }
            out->cpu = (UQUAD)cpu.tv_secs * 1000000 + cpu.tv_micro;
            out->usage = usage;
            ++found;
        }
    }
    UnLockTaskList(list, LTF_ALL);
    return found;
}

static int delay_us(struct timerequest *timer, ULONG micros)
{
    timer->tr_node.io_Command = TR_ADDREQUEST;
    timer->tr_time.tv_secs = micros / 1000000;
    timer->tr_time.tv_micro = micros % 1000000;
    return DoIO((struct IORequest *)timer) == 0;
}

static void busy_worker(void)
{
    while (running) ++iterations;
    for (;;) Wait(SIGBREAKF_CTRL_C);
}

static void exit_worker(void)
{
    Wait(SIGBREAKF_CTRL_C);
    /* NewCreateTask's default final PC removes this task on entry return. */
}

int main(void)
{
#if defined(__AROSEXEC_SMP__)
    kprintf("[CPU21] FAIL unsupported SMP build\n");
    return RETURN_FAIL;
#else
    struct MsgPort *port = NULL;
    struct timerequest *timer = NULL;
    struct Task *worker = NULL;
    struct sample base = {0}, busy = {0}, idle = {0}, before = {0}, after = {0};
    UQUAD busy_start, busy_end, idle_end, slice_start, slice_end;
    BYTE priority = 0;
    BOOL opened = FALSE, changed_priority = FALSE;
    const char *error = "initialization";
    unsigned churn;
    kprintf("[CPU21] main entered\n");
    TaskResBase = OpenResource("task.resource");
    port = CreateMsgPort();
    if (port) timer = (struct timerequest *)CreateIORequest(port, sizeof(*timer));
    if (!TaskResBase || !timer || OpenDevice(TIMERNAME, UNIT_MICROHZ,
                                            (struct IORequest *)timer, 0)) goto done;
    opened = TRUE;
    TimerBase = timer->tr_node.io_Device;
    priority = SetTaskPri(FindTask(NULL), 20);
    changed_priority = TRUE;
    kprintf("[CPU21] begin usage_scale=4294967295 window_us=250000 cpus=1\n");
    running = 1;
    worker = CreateTask("CPU21.busy", 0, busy_worker, 32768);
    error = "busy task or baseline";
    if (!worker || sample_task(worker, NULL, &base) != 1 || base.usage != 0) goto done;
    busy_start = clock_us();
    error = "busy measurement";
    if (!delay_us(timer, 1000000) || sample_task(worker, NULL, &busy) != 1) goto done;
    busy_end = clock_us();
    running = 0;
    error = "idle measurement";
    if (!delay_us(timer, 1000000) || sample_task(worker, NULL, &idle) != 1) goto done;
    idle_end = clock_us();
    kprintf("[CPU21] windows base_cpu=%llu base_usage=%lu busy_cpu=%llu busy_usage=%lu idle_cpu=%llu idle_usage=%lu busy_us=%llu idle_us=%llu iterations=%llu\n",
        (unsigned long long)base.cpu, (unsigned long)base.usage,
        (unsigned long long)busy.cpu, (unsigned long)busy.usage,
        (unsigned long long)idle.cpu, (unsigned long)idle.usage,
        (unsigned long long)(busy_end-busy_start), (unsigned long long)(idle_end-busy_end),
        (unsigned long long)iterations);
    error = "busy/idle accounting contract";
    if (!iterations || busy.cpu < base.cpu + 250000 || idle.cpu < busy.cpu ||
        busy.usage < 0x40000000UL || idle.usage > 0x0cccccccUL || idle.usage >= busy.usage ||
        busy_end-busy_start < 1000000 || idle_end-busy_end < 1000000 ||
        idle.cpu-busy.cpu > 50000 || busy.cpu-base.cpu > busy_end-busy_start+10000) goto done;
    DeleteTask(worker); worker = NULL;

    /* Forbid excludes task switches, not interrupts. An advancing public
     * CPUTime here must include the current, not-yet-switched-out slice. */
    error = "current running slice";
    Forbid();
    int first = sample_task(FindTask(NULL), NULL, &before);
    slice_start = clock_us();
    while (clock_us()-slice_start < 50000) { __asm__ volatile("" ::: "memory"); }
    int second = sample_task(FindTask(NULL), NULL, &after);
    slice_end = clock_us();
    Permit();
    kprintf("[CPU21] current before_cpu=%llu after_cpu=%llu elapsed_us=%llu\n",
        (unsigned long long)before.cpu, (unsigned long long)after.cpu,
        (unsigned long long)(slice_end-slice_start));
    if (first != 1 || second != 1 || after.cpu < before.cpu + 40000 ||
        after.cpu-before.cpu > slice_end-slice_start+10000) goto done;

    error = "task exit churn";
    for (churn = 0; churn < 32; ++churn) {
        char name[32];
        struct sample observed;
        unsigned polls;
        snprintf(name, sizeof(name), "CPU21.churn.%u", churn);
        worker = CreateTask(name, 0, exit_worker, 32768);
        if (!worker || sample_task(worker, NULL, &observed) != 1) goto done;
        Signal(worker, SIGBREAKF_CTRL_C);
        /* From this point the raw address is never dereferenced or queried. */
        worker = NULL;
        for (polls = 0; polls < 100; ++polls) {
            int present;
            if (!delay_us(timer, 10000)) goto done;
            present = sample_task(NULL, name, &observed);
            if (present == 0) break;
            if (present != 1) goto done;
        }
        if (polls == 100) goto done;
    }
    kprintf("[CPU21] churn created=32 exited=32 live_queries=32\n");
    kprintf("[CPU21] sleep unqualified\n");
    error = NULL;
done:
    if (worker) DeleteTask(worker);
    if (changed_priority) SetTaskPri(FindTask(NULL), priority);
    if (opened) CloseDevice((struct IORequest *)timer);
    if (timer) DeleteIORequest((struct IORequest *)timer);
    if (port) DeleteMsgPort(port);
    if (error) kprintf("[CPU21] FAIL %s\n", error);
    else kprintf("[CPU21] PASS\n");
    return error ? RETURN_FAIL : RETURN_OK;
#endif
}
