/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/reloadprobe.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <exec/tasks.h>
#include <aros/debug.h>
#include <stdio.h>
#include "reloadprobe_witness.h"
#include "reloadjob.h"

static struct timerequest *deadline_request;
static struct ReloadJob *active_job;
static const char *waiting_for;

static ULONG arm_deadline(void)
{
    ULONG signal = 1UL << deadline_request->tr_node.io_Message.mn_ReplyPort->mp_SigBit;
    SetSignal(0, signal);
    deadline_request->tr_node.io_Command = TR_ADDREQUEST;
    deadline_request->tr_time.tv_secs = 2;
    deadline_request->tr_time.tv_micro = 0;
    SendIO(&deadline_request->tr_node);
    return signal;
}

static void cancel_deadline(void)
{
    AbortIO(&deadline_request->tr_node);
    WaitIO(&deadline_request->tr_node);
}

/* Unknown worker state must NEVER release this process's stack/ports/segments.
 * The diagnostic reports failure and parks for independent inspection. This is
 * not a successful shutdown, nor a force-unload or an unbounded host retry. */
static void retain(const char *reason)
{
    printf("[reload-async] FAIL reason=%s retained=1\n", reason);
    fflush(stdout);
    bug("[reload-async] FAIL reason=%s retained=1\n", reason);
    if (active_job)
        bug("[reload-async] state job=%p exec=%p phase=%s entered=%u left=%u closed=%u result=%u\n",
            active_job, SysBase, waiting_for, (unsigned)active_job->entered,
            (unsigned)active_job->left, (unsigned)active_job->closed, (unsigned)active_job->result);
    for (;;) Wait(0);
}

static void receive(struct MsgPort *port, struct Message *expected)
{
    ULONG timer_signal = arm_deadline();
    for (;;) {
        struct Message *msg = GetMsg(port);
        if (msg) {
            cancel_deadline();
            if (msg != expected) retain("unexpected-reply");
            return;
        }
        if (CheckIO(&deadline_request->tr_node)) {
            WaitIO(&deadline_request->tr_node);
            retain("reply-timeout");
        }
        Wait((1UL << port->mp_SigBit) | timer_signal);
    }
}

int main(int argc, char **argv)
{
    const ULONG generations[] = {1, 2, 1};
    struct Library *ReloadprobeBase;
    struct ReloadWitness witness;
    struct ReloadJob job;
    BPTR segment;
    struct MsgPort *timer_port;
    struct timerequest *timer_request;
    unsigned i;
    if (argc != 4) {
        bug("[reload-async] FAIL reason=arguments argc=%d\n", argc);
        puts("usage: ReloadAsync A/reloadprobe.library B/reloadprobe.library ReloadWorker");
        return RETURN_ERROR;
    }
    segment = LoadSeg(argv[3]);
    if (!segment) {
        bug("[reload-async] FAIL reason=worker-load ioerr=%ld\n", (long)IoErr());
        return RETURN_FAIL;
    }
    timer_port = CreateMsgPort();
    timer_request = timer_port ? (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_request)) : NULL;
    if (!timer_request || OpenDevice(TIMERNAME, UNIT_VBLANK, &timer_request->tr_node, 0))
        retain("clock-open");
    deadline_request = timer_request;
    for (i = 0; i < 3; i++) {
        struct Process *child;
        ULONG child_id;
        witness = (struct ReloadWitness){RELOAD_WITNESS_MAGIC, 0, 0, 0};
        job = (struct ReloadJob){0};
        job.magic = RELOAD_JOB_MAGIC;
        active_job = &job;
        job.path = argv[i == 1 ? 2 : 1];
        job.parent = CreateMsgPort();
        if (!job.parent) retain("parent-port");
        job.command.mn_ReplyPort = job.parent;
        ReloadprobeBase = OpenLibrary(job.path, 1);
        if (!ReloadprobeBase || ReloadIdentity() != generations[i] || !ReloadWatch(&witness))
            retain("identity-or-watch");
        SetSignal(0, SIGF_CHILD);
        child = CreateNewProcTags(NP_Seglist, segment, NP_FreeSeglist, FALSE,
                              NP_Cli, TRUE, NP_Arguments, (IPTR)"",
                              NP_UserData, (IPTR)&job, NP_NotifyOnDeath, TRUE,
                              NP_Name, (IPTR)"Reload worker", TAG_DONE);
        if (!child)
            retain("create-worker");
        waiting_for = "ready";
        receive(job.parent, &job.ready);
        /* An early failing child may already have exited when CreateNewProc
         * returns. Read its identity from the acknowledged job, not by
         * dereferencing a potentially dead Process pointer. */
        child_id = job.child_id;
        if (job.entered != 1 || job.generation != generations[i])
            retain("worker-entry");
        /* Now close the controller's reference while the worker is INSIDE
         * module code, with only the worker's own reference keeping it alive. */
        RemLibrary(ReloadprobeBase);
        CloseLibrary(ReloadprobeBase);
        ReloadprobeBase = NULL;
        if (witness.released) retain("released-in-flight");
        job.operation = RELOAD_TRY_STOP;
        PutMsg(job.control, &job.command);
        waiting_for = "busy";
        receive(job.parent, &job.command);
        if (job.result != RELOAD_BUSY || witness.released || job.left)
            retain("busy-stop-contract");
        job.operation = RELOAD_DRAIN_STOP;
        PutMsg(job.control, &job.command);
        waiting_for = "done";
        receive(job.parent, &job.done);
        if (job.result || job.left != 1 || job.closed != 1 ||
            witness.attached != 1 || witness.released != 1)
            retain("drain-release-contract");
        /* The worker executable is separate from the replaceable module.
         * DOS notifies after it has returned, so its entry code is no longer
         * on a CPU before we reuse job storage or unload the worker segment. */
        {
            ULONG timer_signal = arm_deadline();
            while (ChildStatus(child_id) == CHILD_ACTIVE) {
                if (CheckIO(&deadline_request->tr_node)) {
                    WaitIO(&deadline_request->tr_node);
                    retain("worker-exit-timeout");
                }
                Wait(SIGF_CHILD | timer_signal);
            }
            cancel_deadline();
        }
        {
            IPTR exited = ChildWait(child_id);
            if (exited <= CHILD_NOTFOUND ||
                ((struct ETask *)exited)->et_UniqueID != child_id)
                retain("child-reap");
            ChildFree(child_id);
        }
        DeleteMsgPort(job.parent);
        printf("[reload-async] cycle=%u generation=%u busy=1 drained=1 released=1 exited=1\n",
               i, (unsigned)generations[i]);
        bug("[reload-async] cycle=%u generation=%u busy=1 drained=1 released=1 exited=1\n",
            i, (unsigned)generations[i]);
    }
    UnLoadSeg(segment);
    CloseDevice(&timer_request->tr_node);
    DeleteIORequest(&timer_request->tr_node);
    DeleteMsgPort(timer_port);
    puts("[reload-async] PASS_SCOPED worker-library cycles=3 rollback=1");
    bug("[reload-async] PASS_SCOPED worker-library cycles=3 rollback=1\n");
    return RETURN_OK;
}
