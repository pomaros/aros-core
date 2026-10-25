/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: The SMP form of FreezeTasks(), ThawTasks(), FreezeTask() and
          ThawTask() (freeze_smp.c).
*/
#ifndef EXEC_FREEZE_SMP_H
#define EXEC_FREEZE_SMP_H

#if defined(__AROSEXEC_SMP__)
ULONG exec_FreezeAllSMP(void);
ULONG exec_ThawAllSMP(void);
BOOL exec_FreezeOneSMP(struct Task *task);
BOOL exec_ThawOneSMP(struct Task *task);
#endif

#endif
