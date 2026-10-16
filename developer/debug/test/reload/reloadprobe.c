/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1. */
#include <exec/memory.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <proto/exec.h>
#include "reloadprobe_base.h"
#include "reloadjob.h"
#include LC_LIBDEFS_FILE

#ifndef RELOAD_GENERATION
#error RELOAD_GENERATION must identify the built variant
#endif

static int ProbeInit(LIBBASETYPEPTR base)
{
    base->witness = NULL;
    base->allocation = AllocVec(4096, MEMF_PUBLIC | MEMF_CLEAR);
    return base->allocation != NULL;
}

static int ProbeExpunge(LIBBASETYPEPTR base)
{
    FreeVec(base->allocation);
    base->allocation = NULL;
    if (base->witness) {
        base->witness->released++;
        base->witness = NULL;
    }
    return TRUE;
}
ADD2INITLIB(ProbeInit, 0);
ADD2EXPUNGELIB(ProbeExpunge, 0);

AROS_LH0(ULONG, ReloadIdentity,
    struct ReloadProbeBase *, ReloadProbeBase, 5, Reloadprobe)
{
    AROS_LIBFUNC_INIT
    return RELOAD_GENERATION;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(BOOL, ReloadWatch, AROS_LHA(APTR, witness, A0),
    struct ReloadProbeBase *, ReloadProbeBase, 6, Reloadprobe)
{
    AROS_LIBFUNC_INIT
    struct ReloadWitness *w = witness;
    if (!w || w->magic != RELOAD_WITNESS_MAGIC || ReloadProbeBase->witness)
        return FALSE;
    ReloadProbeBase->witness = w;
    w->attached++;
    w->generation = RELOAD_GENERATION;
    return TRUE;
    AROS_LIBFUNC_EXIT
}

/* Executed by a separate process which owns an open library reference.
 * Completion is sent by its external trampoline AFTER this method returns
 * and CloseLibrary finishes: no callback into unloaded code is necessary. */
AROS_LH1(BOOL, ReloadWork, AROS_LHA(APTR, job, A0),
    struct ReloadProbeBase *, ReloadProbeBase, 7, Reloadprobe)
{
    AROS_LIBFUNC_INIT
    struct ReloadJob *j = job;
    struct Message *message;
    if (!j || j->magic != RELOAD_JOB_MAGIC || !j->parent)
        return FALSE;
    j->control = CreateMsgPort();
    if (!j->control)
        return FALSE;
    j->entered++;
    PutMsg(j->parent, &j->ready);
    for (;;) {
        WaitPort(j->control);
        message = GetMsg(j->control);
        if (message != &j->command)
            continue;
        if (j->operation == RELOAD_DRAIN_STOP)
            break;
        j->result = RELOAD_BUSY;
        ReplyMsg(message);
    }
    DeleteMsgPort(j->control);
    j->control = NULL;
    j->left++;
    return TRUE;
    AROS_LIBFUNC_EXIT
}
