#ifndef ___UTIMENS_H
#define ___UTIMENS_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: shared conversion for utimes(), utimensat() and futimens()
*/

#include <dos/dos.h>
#include <time.h>

/* Converts the modification time of a POSIX times[2] array (NULL: now) into
   the DOS date that stat() reads back as the same second, with the
   sub-second part rounded down to a tick. Returns 1 when a date was
   produced, 0 when the modification time is UTIME_OMIT, and -1 with errno
   set for an invalid or unrepresentable time. The access time is checked
   but not stored: DOS keeps one date per object. */
int __utimens_date(const struct timespec times[2], struct DateStamp *ds);

/* Sets the date of the object named by an AROS path. With follow non-zero
   a soft link is resolved first, so the target's date changes. Returns 0
   or -1 with errno set. */
int __utimens_set(const char *apath, const struct DateStamp *ds, int follow);

#endif /* ___UTIMENS_H */
