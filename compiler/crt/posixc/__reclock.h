#ifndef ___RECLOCK_H
#define ___RECLOCK_H

/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1

    POSIX advisory record locks kept by posixc (see __reclock.c).
*/

#include <fcntl.h>

#include "__fdesc.h"

int __posixc_reclock(int fd, fdesc *desc, int cmd, struct flock *fl);
/* close() of DESC releases the process's locks independently of active I/O. */
void __posixc_reclock_close(fdesc *desc);
/* Last fcb reference, after all descriptor and operation references leave. */
void __posixc_reclock_release_file(fcb *file);
void __posixc_reclock_release_owner(APTR base);

#endif /* ___RECLOCK_H */
