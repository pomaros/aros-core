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
    BPTR reader = BNULL, writer = BNULL;
    int readfd, writefd, error = 0;
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
            error = __stdc_ioerr2errno(IoErr());
            DeleteFile(pipe_name);
            Close(writer);
            writer = BNULL;
        }
    }

    if (!writer)
    {
        if (!error)
            error = __stdc_ioerr2errno(IoErr());
        goto fail;
    }

    InitSemaphore(&rfcb->io_lock);
    InitSemaphore(&wfcb->io_lock);

    /* Each number is picked and reserved in one step: a number picked first
       and claimed later can meanwhile be taken by another thread, and
       claiming an occupied slot closes it. */
    readfd = __getfreefdslot(0);
    writefd = readfd < 0 ? -1 : __getfreefdslot(0);
    if (writefd < 0)
    {
        error = errno;
        if (readfd >= 0)
            __setfdesc(readfd, NULL);
        goto fail;
    }
    rdesc->fdflags = 0;
    rdesc->fcb = rfcb;
    rdesc->fcb->handle    = reader;
    rdesc->fcb->flags     = O_RDONLY;
    rdesc->fcb->opencount = 1;
    __setfdesc(readfd, rdesc);

    wdesc->fdflags = 0;
    wdesc->fcb = wfcb;
    wdesc->fcb->handle    = writer;
    wdesc->fcb->flags     = O_WRONLY;
    wdesc->fcb->opencount = 1;
    __setfdesc(writefd, wdesc);

    pipedes[0] = readfd;
    pipedes[1] = writefd;
    return 0;

fail:
    if (reader) Close(reader);
    if (writer) Close(writer);
    FreeVec(rfcb);
    FreeVec(wfcb);
    __free_fdesc(rdesc);
    __free_fdesc(wdesc);
    errno = error;
    return -1;
}

