#ifndef _POSIXC_PTY_H_
#define _POSIXC_PTY_H_

/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    BSD pseudo-terminal helpers, declared where GNU declares them.
    openpty() is built on posix_openpt() and its family in <stdlib.h>.
    Both fail with ENOSYS until AROS has a pseudo-terminal device; that work
    keeps these entry points.
*/

#include <aros/system.h>
#include <termios.h>
#include <sys/ioctl.h> /* struct winsize */

__BEGIN_DECLS

int openpty(int *, int *, char *, const struct termios *, const struct winsize *);
int login_tty(int);

__END_DECLS

#endif /* _POSIXC_PTY_H_ */
