/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Change the position in a stream.
*/

#include <dos/dos.h>
#include <proto/dos.h>

#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#include "__stdio.h"
#include "__fdesc.h"
#include "__dos64.h"

#include <stdio.h>

/*
 * fseeko engine. Positions are handled in 64 bits throughout; the
 * __dos64 helpers use dos64.library when it is available and fall
 * back to the 32-bit dos.library calls (failing with
 * ERROR_OBJECT_TOO_LARGE for unrepresentable positions) when not.
 */
int __fseeko64 (
        FILE * stream,
        off64_t  offset,
        int    whence)
{
    LONG mode;
    fdesc *fdesc = __getfdesc(stream->fd);

    if (!fdesc)
    {
        errno = EBADF;
        return -1;
    }
    if (fdesc->fcb->privflags & _FCB_ISDIR)
    {
        errno = EISDIR;
        return -1;
    }
    switch (whence)
    {
        case SEEK_SET: mode = OFFSET_BEGINNING; break;
        case SEEK_CUR: mode = OFFSET_CURRENT; break;
        case SEEK_END: mode = OFFSET_END; break;
        default:
            errno = EINVAL;
            return -1;
    }

    /* Flush and seek share the handle with descriptor I/O. The filesystem
       owns logical positions and zero-filling on writes; seeking itself
       must never change file contents or size. Pass relative offsets to
       the handler rather than adding signed positions in the CRT. */
    __fcb_lock(fdesc->fcb);
    if (!Flush(fdesc->fcb->handle) ||
        __dos64_seek(fdesc->fcb, offset, mode) == -1)
    {
        LONG ioerr = IoErr();
        __fcb_unlock(fdesc->fcb);
        errno = (ioerr == ERROR_OBJECT_TOO_LARGE)
            ? EOVERFLOW : __stdc_ioerr2errno(ioerr);
        return -1;
    }
    stream->flags &= ~(__POSIXC_STDIO_EOF);
    __fcb_unlock(fdesc->fcb);
    return 0;
} /* __fseeko64 */

#if (__WORDSIZE < 64)
/*
 * 32-bit off_t variant: same engine, the offset simply widens.
 */
int __fseeko (
        FILE * stream,
        off_t  offset,
        int    whence)
{
    return __fseeko64 (stream, (off64_t)offset, whence);
} /* __fseeko */
#else
/*
 * on 64bit fseeko is an alias of fseeko64
 */
AROS_MAKE_ALIAS(__fseeko64,__fseeko);
#endif
