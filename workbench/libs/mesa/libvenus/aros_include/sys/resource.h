/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: what Venus needs of <sys/resource.h> on AROS. Venus forwards a
          thread's scheduling priority to the renderer; AROS tasks have
          none in the POSIX sense, so every thread reports the default.
*/

#ifndef VENUS_AROS_SYS_RESOURCE_H
#define VENUS_AROS_SYS_RESOURCE_H

#define PRIO_PROCESS 0

static inline int getpriority(int which, int who)
{
    (void)which; (void)who;
    return 0;
}

#endif
