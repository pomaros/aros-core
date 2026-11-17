/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER
*/

#include <errno.h>
#include <stdio.h>

#include <proto/dos.h>

#include "__stdio.h"
#include "__fdesc.h"
#include "__dos64.h"

/*
 * ftello engine: full 64-bit position via the __dos64 helpers.
 */
off64_t __ftello64(FILE *stream)
{
    QUAD cnt;
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

    /* GetFilePosition64 includes buffered bytes without flushing or changing
       the stream. Flush would use a 32-bit seek for unread input. */
    cnt = __dos64_getpos (fdesc->fcb);

    if (cnt == -1)
        errno = __stdc_ioerr2errno (IoErr ());

    return cnt;
}

#if (__WORDSIZE < 64)
/*
 * 32-bit off_t variant: positions beyond off_t range report EOVERFLOW.
 */
off_t __ftello(FILE *stream)
{
    QUAD cnt = __ftello64(stream);

    if (cnt != (QUAD)(off_t)cnt)
    {
        errno = EOVERFLOW;
        return (off_t)-1;
    }
    return (off_t)cnt;
}
#else
/*
 * on 64bit ftello is an alias of ftello64
 */
AROS_MAKE_ALIAS(__ftello64,__ftello);
#endif
