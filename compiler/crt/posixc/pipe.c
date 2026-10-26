/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
*/

#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/exec.h>

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include "__fdesc.h"
#include "__pipe_name.h"

/*****************************************************************************

    NAME */
#include <unistd.h>

        int pipe(

/*  SYNOPSIS */
        int *pipedes)

/*  FUNCTION

    INPUTS

    RESULT

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

******************************************************************************/
{
    BPTR reader, writer;
    fcb *rfcb = NULL, *wfcb = NULL;
    fdesc *rdesc = NULL, *wdesc = NULL;
    char pipe_name[POSIXC_PIPE_NAME_SIZE];

    if (!pipedes)
    {
        errno = EFAULT;

        return -1;
    }

    if (
        (rfcb = AllocVec(sizeof(fcb), MEMF_ANY | MEMF_CLEAR)) == NULL ||
        (rdesc = __alloc_fdesc()) == NULL ||
        (wfcb = AllocVec(sizeof(fcb), MEMF_ANY | MEMF_CLEAR)) == NULL ||
        (wdesc = __alloc_fdesc()) == NULL
    )
    {
    FreeVec(rfcb);
        if(rdesc)
            __free_fdesc(rdesc);
    FreeVec(wfcb);
        if(wdesc)
            __free_fdesc(wdesc);
        errno = ENOMEM;
        return -1;
    }

    __posixc_pipe_name(pipe_name, (unsigned long)getpid());

    writer = Open(pipe_name, MODE_NEWFILE);
    if (writer)
    {
        reader = Open(pipe_name, MODE_OLDFILE);
        if (!reader)
        {
            DeleteFile(pipe_name);
            Close(writer);
            writer = BNULL;
        }
    }

    if (!writer)
    {
        errno = __stdc_ioerr2errno(IoErr());
        __free_fdesc(rdesc);
        __free_fdesc(wdesc);
        return -1;
    }

    InitSemaphore(&rfcb->io_lock);
    InitSemaphore(&wfcb->io_lock);

    /* Each number is picked and reserved in one step: a number picked first
       and claimed later can meanwhile be taken by another thread, and
       claiming an occupied slot closes it. */
    pipedes[0] = __getfreefdslot(0);
    pipedes[1] = pipedes[0] < 0 ? -1 : __getfreefdslot(0);
    if (pipedes[1] < 0)
    {
        if (pipedes[0] >= 0)
            __setfdesc(pipedes[0], NULL);
        Close(reader);
        Close(writer);
        FreeVec(rfcb);
        FreeVec(wfcb);
        __free_fdesc(rdesc);
        __free_fdesc(wdesc);
        return -1;
    }
    rdesc->fdflags = 0;
    rdesc->fcb = rfcb;
    rdesc->fcb->handle    = reader;
    rdesc->fcb->flags     = O_RDONLY;
    rdesc->fcb->opencount = 1;
    __setfdesc(pipedes[0], rdesc);

    wdesc->fdflags = 0;
    wdesc->fcb = wfcb;
    wdesc->fcb->handle    = writer;
    wdesc->fcb->flags     = O_WRONLY;
    wdesc->fcb->opencount = 1;
    __setfdesc(pipedes[1], wdesc);

    return 0;
}

