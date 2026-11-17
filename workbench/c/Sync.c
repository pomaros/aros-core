/*
    Copyright (C) 2026 John KNIPPER.
    Distributed under the AROS Public License, version 1.1.
*/

/******************************************************************************
    NAME
        Sync

    SYNOPSIS
        PATH/A

    LOCATION
        C:

    FUNCTION
        Ask the filesystem containing PATH to commit its buffered writes.
        Returns failure if the filesystem cannot acknowledge ACTION_FLUSH.

    INPUTS
        PATH -- An existing file, directory or volume root.

    EXAMPLE
        Sync Work:
******************************************************************************/

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/dos.h>

const TEXT version[] = "$VER: Sync 1.0 (17.11.2026)\n";
int __nocommandline;

int main(void)
{
    SIPTR args[1] = {0};
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)"PATH/A", args, NULL);
    BPTR lock;
    LONG error, ok;

    if (!rda)
    {
        PrintFault(IoErr(), (CONST_STRPTR)"Sync");
        return RETURN_FAIL;
    }

    lock = Lock((STRPTR)args[0], SHARED_LOCK);
    if (!lock)
    {
        error = IoErr();
        FreeArgs(rda);
        PrintFault(error, (CONST_STRPTR)"Sync");
        return RETURN_FAIL;
    }

    /* Keep the lock alive until the handler has acknowledged the barrier. */
    ok = DoPkt(((struct FileLock *)BADDR(lock))->fl_Task,
               ACTION_FLUSH, 0, 0, 0, 0, 0);
    error = ok ? 0 : IoErr();
    UnLock(lock);
    FreeArgs(rda);

    if (!ok)
    {
        if (error)
            PrintFault(error, (CONST_STRPTR)"Sync");
        else
            PutStr((CONST_STRPTR)"Sync: filesystem did not acknowledge flush\n");
        return RETURN_FAIL;
    }

    PutStr((CONST_STRPTR)"Sync: filesystem flush acknowledged\n");
    return RETURN_OK;
}
