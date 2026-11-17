/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
/* Diagnostic launcher: compare RunCommand with/without task-storage scope.
 * No GL dependency. Child logs and return values must both be inspected.
 */
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/task.h>
#include <stdio.h>
#include <string.h>

APTR TaskResBase;

int main(int argc, char **argv)
{
    const char args[] = "softpipe openonly\n";
    BPTR report = Output();
    BPTR outputs[10] = {0};
    LONG slot;
    int scoped, n, result = RETURN_OK;

    if (argc != 4 || (strcmp(argv[1], "scoped") && strcmp(argv[1], "plain")))
    {
        FPrintf(report, "Usage: virglrunprobe scoped|plain child log-prefix\n");
        return RETURN_FAIL;
    }
    scoped = !strcmp(argv[1], "scoped");
    TaskResBase = OpenResource("task.resource");
    if (!TaskResBase) return RETURN_FAIL;
    /* Make storage nonempty, so NULL from SaveTaskStorage means failure. */
    slot = AllocTaskStorageSlot();
    if (slot <= 0) return RETURN_FAIL;
    if (!SetTaskStorageSlot(slot, 123))
    {
        FreeTaskStorageSlot(slot);
        return RETURN_FAIL;
    }
    for (n = 1; n <= 10; ++n)
    {
        char path[512];
        BPTR seg, output, previous;
        APTR saved = NULL;
        LONG rc;
        if (snprintf(path, sizeof(path), "%s-%d.log", argv[3], n) >= (int)sizeof(path))
        { result = RETURN_FAIL; break; }
        seg = LoadSeg(argv[2]);
        if (!seg) { result = RETURN_FAIL; break; }
        output = Open(path, MODE_NEWFILE);
        if (!output) { UnLoadSeg(seg); result = RETURN_FAIL; break; }
        if (scoped)
        {
            saved = SaveTaskStorage();
            if (!saved)
            { Close(output); UnLoadSeg(seg); result = RETURN_FAIL; break; }
        }
        FPrintf(report, "child %ld scoped=%ld output=%p start\n", (LONG)n, (LONG)scoped, output);
        Flush(report);
        previous = SelectOutput(output);
        rc = RunCommand(seg, 262144, args, sizeof(args) - 1);
        if (scoped) RestoreTaskStorage(saved);
        SelectOutput(previous);
        Flush(output);
        outputs[n-1] = output; /* Keep handles distinct across invocations. */
        UnLoadSeg(seg);
        FPrintf(report, "child %ld rc=%ld slot=%ld\n", (LONG)n, rc, GetTaskStorageSlot(slot));
        Flush(report);
        if (rc != RETURN_OK || GetTaskStorageSlot(slot) != 123)
        { result = RETURN_FAIL; break; }
    }
    for (n = 0; n < 10; ++n)
    {
        char data[4096], *match;
        LONG len;
        int passes = 0;
        if (!outputs[n]) { result = RETURN_FAIL; continue; }
        Seek(outputs[n], 0, OFFSET_BEGINNING);
        len = Read(outputs[n], data, sizeof(data) - 1);
        if (len < 0) len = 0;
        data[len] = 0;
        match = data;
        while ((match = strstr(match, "openonly: PASS\n")) != NULL)
        { ++passes; ++match; }
        FPrintf(report, "log %ld passes=%ld\n", (LONG)n + 1, (LONG)passes);
        if (passes != 1) result = RETURN_FAIL;
        Close(outputs[n]);
    }
    FreeTaskStorageSlot(slot);
    FPrintf(report, "runner result=%ld\n", (LONG)result);
    return result;
}
