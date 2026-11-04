/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    errno for stdcio.library code, force-included into every stdcio
    source file (see mmakefile.src).

    stdcio reaches stdc through its own rellib StdCBase (opened by
    genmodule), and stdc.library is per-opener, so the plain errno
    macro would set the errno of that private base - never seen by the
    calling program. stdcio_startup records the program's own StdCBase
    in StdCIOBase->StdCBase; set errno there when it is known.
*/
#ifndef __STDCIO_ERRNO_H
#define __STDCIO_ERRNO_H

#include <errno.h>
#include <libraries/stdc.h>
#include <libraries/stdcio.h>

static inline int *__stdcio_errnoptr(void)
{
    /* The shared core keeps errno in a task-local slot owned by
       task.resource, shared by every stdc opener of the same task, so the
       program's errno is reached through any base, including stdcio's
       private rellib base. No per-base errno field exists. */
    return __stdc_geterrnoptr();
}

#undef errno
#define errno (*__stdcio_errnoptr())

#endif /* __STDCIO_ERRNO_H */
