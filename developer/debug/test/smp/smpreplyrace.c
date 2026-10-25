/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: SMP coverage for ReplyMsg() and WaitIO() meeting on one message.
          A server on one core takes each request off its port and replies
          at once; the client on another core calls WaitIO() after a short
          delay that sweeps across the server's reply. WaitIO() reads the
          node type outside the port's lock, so a type that turns
          NT_REPLYMSG before the message is on the reply port's list lets
          it Remove() a node that is not linked there: the reply is then
          left on the port, and the stale links corrupt the server's list.

          SMP-ReplyPort has timer.device reply about every 400 us, which
          rarely meets the window on a fast core. Here every round meets
          it at some offset.
*/

#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/io.h>
#include <utility/tagitem.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>

#include <stdio.h>
#include <string.h>

#if defined(__AROSEXEC_SMP__)
#define DEBUG 1
#include <aros/debug.h>

#define REPORT(...) do { bug(__VA_ARGS__); printf(__VA_ARGS__); fflush(stdout); } while (0)

APTR KernelBase;

#define STACKSIZE       16384
#define MAX_PAIRS       4
#define ROUNDS          200000
#define SWEEP           256         /* delay steps between PutMsg and WaitIO */
#define WAIT_TICKS      6000        /* 120 s bound */

struct Pair
{
    struct MsgPort  *p_ServerPort;      /* published by the server */
    volatile ULONG  p_ServerReady;
    volatile ULONG  p_Stop;
    volatile ULONG  p_ServerDone;
    volatile ULONG  p_ClientDone;
    volatile ULONG  p_Replied;
    volatile ULONG  p_Completed;
    volatile ULONG  p_Leftover;
    volatile ULONG  p_Stray;            /* foreign nodes seen by the server */
    volatile ULONG  p_Failed;
    int             p_ServerCPU;
    int             p_ClientCPU;
};

static int g_NumCPUs = 1;

static struct Pair *mypair(void)
{
    return (struct Pair *)FindTask(NULL)->tc_UserData;
}

static struct Task *spawn_pinned(const char *name, APTR pc, int cpu,
                                 struct Pair *p)
{
    cpumask_t *mask = KrnAllocCPUMask();

    if (!mask)
        return NULL;
    KrnClearCPUMask(mask);
    KrnGetCPUMask(cpu % g_NumCPUs, mask);
    return NewCreateTask(TASKTAG_NAME,      (IPTR)name,
                         TASKTAG_PRI,       0,
                         TASKTAG_PC,        (IPTR)pc,
                         TASKTAG_STACKSIZE, STACKSIZE,
                         TASKTAG_USERDATA,  (IPTR)p,
                         TASKTAG_AFFINITY,  (IPTR)mask,
                         TAG_DONE);
}

/* Replies to everything as soon as it arrives: no Wait(), the core is ours */
static void Server(void)
{
    struct Pair *p = mypair();
    struct MsgPort *port = CreateMsgPort();
    struct Message *msg;

    if (!port)
    {
        p->p_Failed++;
        p->p_ServerDone = 1;
        Wait(0);
    }
    p->p_ServerPort = port;
    p->p_ServerReady = 1;
    while (!p->p_Stop)
    {
        if ((msg = GetMsg(port)) != NULL)
        {
            if (msg->mn_Length != sizeof(struct IORequest))
                p->p_Stray++;
            ReplyMsg(msg);
            p->p_Replied++;
        }
    }
    while ((msg = GetMsg(port)) != NULL)
        p->p_Stray++;
    DeleteMsgPort(port);
    p->p_ServerDone = 1;
    Wait(0);
}

static void Client(void)
{
    struct Pair *p = mypair();
    struct MsgPort *reply = CreateMsgPort();
    struct IORequest *io;
    ULONG round;
    volatile ULONG spin;

    if (!reply)
        goto fail;
    io = CreateIORequest(reply, sizeof(struct IORequest));
    if (!io)
    {
        DeleteMsgPort(reply);
        goto fail;
    }
    while (!p->p_ServerReady)
        ;

    for (round = 0; round < ROUNDS && !p->p_Failed; round++)
    {
        io->io_Flags = 0;
        io->io_Error = 0;
        PutMsg(p->p_ServerPort, &io->io_Message);
        for (spin = round % SWEEP; spin; spin--)
            ;
        WaitIO(io);
        p->p_Completed++;
        /* WaitIO() took the reply off: the port must be empty */
        if (GetMsg(reply) != NULL)
        {
            p->p_Leftover++;
            break;
        }
    }

    DeleteIORequest(io);
    DeleteMsgPort(reply);
    p->p_ClientDone = 1;
    Wait(0);

fail:
    p->p_Failed++;
    p->p_ClientDone = 1;
    Wait(0);
}

int main(void)
{
    struct Pair p[MAX_PAIRS];
    struct Task *server[MAX_PAIRS], *client[MAX_PAIRS];
    int n, k, tick, ok = 1, created = 0;

    KernelBase = OpenResource("kernel.resource");
    if (KernelBase)
        g_NumCPUs = KrnGetCPUCount();

    n = g_NumCPUs / 2;
    if (n > MAX_PAIRS)
        n = MAX_PAIRS;
    REPORT("[smpreplyrace] start: cpus=%d, %d client/server pairs x %d rounds\n",
           g_NumCPUs, n, ROUNDS);

    if (n < 1)
    {
        REPORT("[smpreplyrace] DONE: 0 PASS, 0 FAIL, 1 INVALID (needs two CPUs)\n");
        return RETURN_WARN;
    }

    memset(p, 0, sizeof(p));
    for (k = 0; k < n; k++)
    {
        p[k].p_ClientCPU = k;
        p[k].p_ServerCPU = k + n;
        server[k] = spawn_pinned("smpreplyrace.server", Server, k + n, &p[k]);
        client[k] = server[k] ? spawn_pinned("smpreplyrace.client", Client, k, &p[k]) : NULL;
        if (!client[k])
        {
            if (server[k])
            {
                p[k].p_Stop = 1;
                while (!p[k].p_ServerDone)
                    Delay(1);
                Forbid();
                RemTask(server[k]);
                Permit();
            }
            break;
        }
        created++;
    }

    for (tick = 0; tick < WAIT_TICKS; tick++)
    {
        int done = 0;

        for (k = 0; k < created; k++)
            if (p[k].p_ClientDone)
                done++;
        if (done == created)
            break;
        Delay(1);
    }
    for (k = 0; k < created; k++)
        p[k].p_Stop = 1;
    for (k = 0; k < created; k++)
    {
        int t;

        for (t = 0; t < 250 && !p[k].p_ServerDone; t++)
            Delay(1);
    }

    Forbid();
    for (k = 0; k < created; k++)
    {
        if (p[k].p_ClientDone)
            RemTask(client[k]);
        if (p[k].p_ServerDone)
            RemTask(server[k]);
    }
    Permit();

    if (created < n)
    {
        REPORT("[smpreplyrace] DONE: 0 PASS, 0 FAIL, 1 INVALID (task create failed)\n");
        return RETURN_WARN;
    }
    if (tick == WAIT_TICKS)
    {
        REPORT("[smpreplyrace] *** TIMEOUT *** (clients stuck)\n");
        ok = 0;
    }

    for (k = 0; k < n; k++)
    {
        REPORT("[smpreplyrace] pair %d (client cpu %d, server cpu %d): %lu completed, "
               "%lu replied, %lu left on the reply port, %lu stray, %lu failed\n",
               k, p[k].p_ClientCPU, p[k].p_ServerCPU,
               (unsigned long)p[k].p_Completed, (unsigned long)p[k].p_Replied,
               (unsigned long)p[k].p_Leftover, (unsigned long)p[k].p_Stray,
               (unsigned long)p[k].p_Failed);
        if (p[k].p_Leftover || p[k].p_Stray || p[k].p_Failed ||
            !p[k].p_ServerDone || p[k].p_Completed != ROUNDS)
            ok = 0;
    }

    REPORT("[smpreplyrace] DONE: %d PASS, %d FAIL, 0 INVALID\n", ok, !ok);
    return ok ? RETURN_OK : RETURN_FAIL;
}
#else
int main(void)
{
    return RETURN_FAIL;
}
#endif
