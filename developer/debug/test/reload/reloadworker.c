/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/reloadprobe.h>
#include "reloadjob.h"

int main(void)
{
    struct ReloadJob *j = FindTask(NULL)->tc_UserData;
    struct Library *ReloadprobeBase;
    if (!j || j->magic != RELOAD_JOB_MAGIC)
        return 20;
    j->child_id = FindTask(NULL)->tc_UnionETask.tc_ETask->et_UniqueID;
    ReloadprobeBase = OpenLibrary(j->path, 1);
    j->result = 20;
    if (ReloadprobeBase) {
        j->generation = ReloadIdentity();
        j->result = ReloadWork(j) ? 0 : 20;
        CloseLibrary(ReloadprobeBase);
        j->closed++;
    }
    /* No job access after publication; the parent also waits for DOS's
     * death notification before unloading THIS executable segment. */
    PutMsg(j->parent, &j->done);
    return 0;
}
