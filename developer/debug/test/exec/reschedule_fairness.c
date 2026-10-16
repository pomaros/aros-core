/* SPDX-License-Identifier: APL-1.1
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * SMP0: explicit-yield fairness on the uniprocessor scheduler.
 */
#include <exec/tasks.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include <stdio.h>

#if !defined(__AROSEXEC_SMP__)
static volatile UQUAD counters[2];

static void worker_a(void)
{
    for (;;) {
        ++counters[0];
        Reschedule();
    }
}

static void worker_b(void)
{
    for (;;) {
        ++counters[1];
        Reschedule();
    }
}

/* The controller outranks both workers and blocks for one timer request.
 * When it wakes, neither worker can run while results are copied/deleted.
 * No worker uses a timer, signal handshake or shared counter to force fairness.
 */
static int measure(struct timerequest *timer, BYTE priority_a, UQUAD *a, UQUAD *b)
{
    struct Task *tasks[2];
    LONG error;

    counters[0] = counters[1] = 0;
    tasks[0] = CreateTask("SMP0.a", priority_a, worker_a, 16384);
    tasks[1] = CreateTask("SMP0.b", 0, worker_b, 16384);
    if (!tasks[0] || !tasks[1]) {
        if (tasks[0]) DeleteTask(tasks[0]);
        if (tasks[1]) DeleteTask(tasks[1]);
        return 0;
    }

    timer->tr_node.io_Command = TR_ADDREQUEST;
    timer->tr_time.tv_secs = 10;
    timer->tr_time.tv_micro = 0;
    error = DoIO((struct IORequest *)timer);
    *a = counters[0];
    *b = counters[1];
    DeleteTask(tasks[0]);
    DeleteTask(tasks[1]);
    return error == 0;
}
#endif

int main(void)
{
#if defined(__AROSEXEC_SMP__)
    puts("[SMP0] FAIL: use a uniprocessor build; this test does not pin SMP tasks");
    return RETURN_FAIL;
#else
    struct MsgPort *port;
    struct timerequest *timer;
    UQUAD a = 0, b = 0, hi = 0, lo = 0;
    BYTE old_priority;
    int measured, fair, priority;

    port = CreateMsgPort();
    timer = port ? (struct timerequest *)CreateIORequest(port, sizeof(*timer)) : NULL;
    if (!timer || OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)timer, 0)) {
        if (timer) DeleteIORequest((struct IORequest *)timer);
        if (port) DeleteMsgPort(port);
        puts("[SMP0] FAIL: cannot open timer.device");
        return RETURN_FAIL;
    }

    old_priority = SetTaskPri(FindTask(NULL), 20);
    measured = measure(timer, 0, &a, &b);
    if (measured)
        measured = measure(timer, 1, &hi, &lo);
    SetTaskPri(FindTask(NULL), old_priority);
    CloseDevice((struct IORequest *)timer);
    DeleteIORequest((struct IORequest *)timer);
    DeleteMsgPort(port);
    if (!measured) {
        puts("[SMP0] FAIL: task allocation or timer request failed");
        return RETURN_FAIL;
    }

    /* Counters cannot approach UQUAD overflow in two ten-second windows. */
    fair = a && b && 10 * a >= 9 * b && 10 * a <= 11 * b;
    priority = hi && lo == 0;
    printf("[SMP0] %s fair: a=%llu b=%llu ratio=%llu.%03llu\n",
           fair ? "PASS" : "FAIL", (unsigned long long)a, (unsigned long long)b,
           (unsigned long long)(b ? a / b : 0),
           (unsigned long long)(b ? (a % b) * 1000 / b : 0));
    printf("[SMP0] (control) %s: hi=%llu lo=%llu\n",
           priority ? "priority respected" : "FAIL priority inversion",
           (unsigned long long)hi, (unsigned long long)lo);
    return fair && priority ? RETURN_OK : RETURN_FAIL;
#endif
}
