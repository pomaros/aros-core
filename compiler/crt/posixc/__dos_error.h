/*
    Copyright (C) 2026 John Knipper.
    Distributed under the AROS Public License, version 1.1.

    Translate an immediately captured native failure into the caller's POSIX
    error namespace. The caller still owns its per-task errno assignment.
*/
#ifndef __POSIXC_DOS_ERROR_H
#define __POSIXC_DOS_ERROR_H

#include <dos/dos.h>
#include <errno.h>

static int __posixc_dos_error(LONG error)
{
    switch (error)
    {
        case 0:
            return EIO; /* a failed operation must not report "No error" */
        case ERROR_ACTION_NOT_KNOWN:
        case ERROR_NOT_IMPLEMENTED:
            return ENOSYS;
        case ERROR_DISK_FULL:
            return ENOSPC;
        case ERROR_DISK_WRITE_PROTECTED:
            return EROFS;
        case ERROR_DELETE_PROTECTED:
        case ERROR_WRITE_PROTECTED:
        case ERROR_READ_PROTECTED:
            return EACCES;
        case ERROR_DIRECTORY_NOT_EMPTY:
            return ENOTEMPTY;
        case ERROR_TOO_MANY_LEVELS:
            return ELOOP;
        default:
            return __stdc_ioerr2errno(error);
    }
}

#endif
