/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include "consumers.h"
#include <resources/task.h>
#include <proto/task.h>
#include <proto/exec.h>
extern APTR TaskResBase;
/* Copy only bounded scalar/name data while task lifetimes are protected.
 * No task pointers escape the resource lock; no messaging or formatting there. */
int IndicatorConsumersRead(struct IndicatorConsumers *result)
{
    struct IndicatorConsumers list={0};
    struct TaskList *locked;
    struct Task *task;
    unsigned count=0;
    if (!TaskResBase) return FALSE;
    locked=LockTaskList(LTF_ALL);
    if (locked) {
        while (count<128 && (task=NextTaskEntry(locked,LTF_ALL))) {
            ULONG usage=0;
            struct TagItem tags[]={{TaskTag_CPUUsage,(IPTR)&usage},{TAG_DONE,0}};
            QueryTaskTagList(task,tags);
            IndicatorConsumerAdd(&list,(const char *)task->tc_Node.ln_Name,usage);
            ++count;
        }
        if (count==128 && NextTaskEntry(locked,LTF_ALL)) list.truncated=1;
    }
    UnLockTaskList(locked,LTF_ALL);
    if (!locked) return 0;
    *result=list;
    return 1;
}
