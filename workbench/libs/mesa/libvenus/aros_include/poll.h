/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: what Venus needs of <poll.h> on AROS. Venus polls only sync
          files, which AROS does not have: every poll fails.
*/

#ifndef VENUS_AROS_POLL_H
#define VENUS_AROS_POLL_H

#include <errno.h>

#define POLLIN  0x001
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020

typedef unsigned int nfds_t;

struct pollfd
{
    int   fd;
    short events;
    short revents;
};

static inline int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    (void)fds; (void)nfds; (void)timeout;
    errno = ENOSYS;
    return -1;
}

#endif
