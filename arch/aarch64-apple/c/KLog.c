/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: KLog [TAIL n] [FIND text] [SINCE n] - print the kernel log ring,
          the kernel's debug output kept in memory (aros/apple/klog.h).

    TAIL prints the last n lines (default all that the ring holds), FIND
    only the lines that contain text, SINCE only what was written after the
    character count n; the last line printed is "[klog] head=<count>" so a
    later run can continue with SINCE.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <aros/apple/klog.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>

#include <string.h>

APTR KernelBase;

int main(void)
{
    IPTR args[3] = { 0, 0, 0 };
    struct RDArgs *rda = ReadArgs("TAIL/K/N,FIND/K,SINCE/K/N", args, NULL);
    const struct AROSAppleKernelLog *log;
    IPTR attr;
    uint64_t head, start, n, i;
    char *copy;
    LONG tail = -1, lines = 0;
    const char *find;

    if (!rda) {
        PrintFault(IoErr(), "KLog");
        return RETURN_ERROR;
    }
    KernelBase = OpenResource("kernel.resource");
    attr = KernelBase ? KrnGetSystemAttr(AROS_APPLE_KATTR_KLOG) : (IPTR)-1;
    log = (attr != (IPTR)-1 && attr != 0) ? (const struct AROSAppleKernelLog *)attr : NULL;
    if (!log || log->version != AROS_APPLE_KLOG_VERSION || log->size < sizeof(*log)) {
        PutStr("KLog: this kernel keeps no log ring\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    if (args[0])
        tail = *(LONG *)args[0];
    find = (const char *)args[1];

    head = *log->head;
    start = head > log->capacity ? head - log->capacity : 0;
    if (args[2] && *(LONG *)args[2] > 0 && (uint64_t)*(LONG *)args[2] > start)
        start = (uint64_t)*(LONG *)args[2];
    n = head - start;
    copy = AllocVec(n + 1, MEMF_ANY);
    if (!copy) {
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    for (i = 0; i < n; ++i) {
        char c = log->ring[(start + i) % log->capacity];
        copy[i] = (c == '\r' || c == 0) ? ' ' : c;
    }
    copy[n] = 0;

    /* Count the lines that qualify, then skip to the last TAIL of them. */
    {
        char *p = copy, *e;
        LONG total = 0, skip;

        while (*p) {
            e = strchr(p, '\n');
            if (e)
                *e = 0;
            if (!find || strstr(p, find))
                ++total;
            if (!e)
                break;
            *e = '\n';
            p = e + 1;
        }
        skip = (tail >= 0 && total > tail) ? total - tail : 0;
        p = copy;
        while (*p) {
            e = strchr(p, '\n');
            if (e)
                *e = 0;
            if (!find || strstr(p, find)) {
                if (skip > 0)
                    --skip;
                else {
                    PutStr(p);
                    PutStr("\n");
                    ++lines;
                }
            }
            if (!e)
                break;
            p = e + 1;
        }
    }
    Printf("[klog] head=%lu lines=%ld\n", (unsigned long)head, lines);
    FreeVec(copy);
    FreeArgs(rda);
    return RETURN_OK;
}
