/*
    Copyright (C) 2012-2021, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER

    Internal initialisation code for posixc.library
*/

#include <aros/debug.h>

#include <proto/exec.h>
#include <aros/symbolsets.h>

#include <libraries/posixc.h>

#include "__posixc_intbase.h"
#include "__reclock.h"

/* We handle StdCBase */
const ULONG __aros_rellib_base_StdCBase = 0;
SETRELLIBOFFSET(StdCBase, struct PosixCBase, StdCBase)

/* We handle StdCIOBase */
const ULONG __aros_rellib_base_StdCIOBase = 0;
SETRELLIBOFFSET(StdCIOBase, struct PosixCBase, StdCIOBase)

static int __posixc_open(struct PosixCIntBase *PosixCBase)
{
    D(bug("[posixc] %s(0x%p)\n", __func__, PosixCBase));

    /* Per-runtime scratch memory; descriptor storage owns its Exec heap. */
    PosixCBase->internalpool = CreatePool(MEMF_PUBLIC|MEMF_CLEAR|MEMF_SEM_PROTECTED, 256, 256);

    PosixCBase->upath_task = FindTask(NULL);
    NEWLIST((struct List *)&PosixCBase->upath_others);
    InitSemaphore(&PosixCBase->upath_sem);

    return PosixCBase->internalpool != NULL;
}

static void __posixc_close(struct PosixCIntBase *PosixCBase)
{
    D(bug("[posixc] %s(0x%p)\n", __func__, PosixCBase));
    if (PosixCBase->internalpool)
    {
        __upath_free_others(PosixCBase);
        DeletePool(PosixCBase->internalpool);
        PosixCBase->internalpool = NULL;
    }
}

ADD2OPENLIB(__posixc_open, -50);
ADD2CLOSELIB(__posixc_close, -50);
ADD2ABORTOPENLIB(__posixc_close, -50);
