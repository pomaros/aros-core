/*
    Copyright (C) 2012-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.

    Linked clients and shared modules select the same native task error cell.
*/
#ifndef STDC_STATIC
#include <libraries/stdc.h>
#include <proto/task.h>
#endif

int *__stdc_geterrnoptr(void)
{
#ifdef STDC_STATIC
    /* Freestanding bootstrap code has no shared runtime admission. */
    static int bootstrap_errno;
    return &bootstrap_errno;
#else
    struct StdCBase *base = __aros_getbase_StdCBase();
    struct Library *TaskResBase = base->_errno_task_resource;
    return (int *)GetTaskStorageSlot(base->_errno_slot);
#endif
}
