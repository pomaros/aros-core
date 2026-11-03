/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: what Venus needs of <sys/syscall.h> on AROS: the calling
          thread's identity, which is its task.
*/

#ifndef VENUS_AROS_SYS_SYSCALL_H
#define VENUS_AROS_SYS_SYSCALL_H

#define SYS_gettid 1

long vn_aros_gettid(void);

#define syscall(number) ((number) == SYS_gettid ? vn_aros_gettid() : -1L)

#endif
