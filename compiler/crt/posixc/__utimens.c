/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: shared conversion for utimes(), utimensat() and futimens()
*/

#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <errno.h>
#include <sys/stat.h>

#include "__posixc_time.h"
#include "__utimens.h"

#define NSEC_PER_TICK (1000000000L / TICKS_PER_SECOND)

static int valid_nsec(const struct timespec *ts)
{
    return ts->tv_nsec == UTIME_NOW || ts->tv_nsec == UTIME_OMIT
        || (ts->tv_nsec >= 0 && ts->tv_nsec < 1000000000L);
}

int __utimens_date(const struct timespec times[2], struct DateStamp *ds)
{
    long long local;

    if (times == NULL)
    {
        DateStamp(ds);
        return 1;
    }
    if (!valid_nsec(&times[0]) || !valid_nsec(&times[1]))
    {
        errno = EINVAL;
        return -1;
    }
    if (times[1].tv_nsec == UTIME_OMIT)
        return 0;
    if (times[1].tv_nsec == UTIME_NOW)
    {
        DateStamp(ds);
        return 1;
    }

    /* The inverse of __stat(): DOS dates count local time from 1978. */
    local = (long long)times[1].tv_sec - OFFSET_FROM_1970
          - (long long)__stdc_gmtoffset() * 60;
    if (local < 0 || local / (60*60*24) > 0x7fffffffLL)
    {
        errno = EINVAL;
        return -1;
    }
    ds->ds_Days   = (LONG)(local / (60*60*24));
    ds->ds_Minute = (LONG)((local / 60) % (60*24));
    ds->ds_Tick   = (LONG)((local % 60) * TICKS_PER_SECOND
                  + times[1].tv_nsec / NSEC_PER_TICK);
    return 1;
}

int __utimens_set(const char *apath, const struct DateStamp *ds, int follow)
{
    const char *target = apath;
    STRPTR resolved = NULL;
    BPTR lock = BNULL;
    ULONG size = 256;
    int result = -1;

    if (follow)
    {
        /* Lock follows soft links; its name is the object whose date
           changes. An exclusively locked file is set by its own name. */
        lock = Lock((CONST_STRPTR)apath, SHARED_LOCK);
        if (lock == BNULL && IoErr() != ERROR_OBJECT_IN_USE)
        {
            errno = __stdc_ioerr2errno(IoErr());
            return -1;
        }
        while (lock != BNULL)
        {
            resolved = AllocVec(size, MEMF_ANY);
            if (resolved == NULL)
            {
                errno = ENOMEM;
                goto out;
            }
            if (NameFromLock(lock, resolved, size))
            {
                target = (const char *)resolved;
                break;
            }
            FreeVec(resolved);
            resolved = NULL;
            if (IoErr() != ERROR_LINE_TOO_LONG || size >= 65536)
            {
                errno = __stdc_ioerr2errno(IoErr());
                goto out;
            }
            size *= 2;
        }
        if (lock != BNULL)
        {
            UnLock(lock);
            lock = BNULL;
        }
    }

    if (SetFileDate((CONST_STRPTR)target, (struct DateStamp *)ds))
        result = 0;
    else
        errno = __stdc_ioerr2errno(IoErr());

out:
    if (lock != BNULL)
        UnLock(lock);
    if (resolved != NULL)
        FreeVec(resolved);
    return result;
}
