/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
*/

#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/exec.h>

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include "__fdesc.h"

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
    /* PIPE:cpipe-%08x-%d, where %x is the getpid(), %d is the nth pipe */
    char pipe_name[5 + 6 + 8 + 1 + 16 + 1];
    static int pipeno = 0;
    int number;

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

    /* Take the number in the same step as the increment: read again
       afterwards, two threads can name the same pipe. */
    number = __atomic_add_fetch(&pipeno, 1, __ATOMIC_RELAXED);

    snprintf(pipe_name, sizeof(pipe_name), "PIPE:cpipe-%08lx-%d",
               (unsigned long)getpid(), number);
    pipe_name[sizeof(pipe_name)-1] = 0;

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

