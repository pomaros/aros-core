/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/reloadprobe.h>
#include <aros/debug.h>
#include "reloadprobe_witness.h"
#include <stdlib.h>
#include <stdio.h>

/* Two immutable paths contain the same resident name, with different code.
 * A -> B -> A proves replacement and rollback, rather than reopening A.
 * No general-purpose force-expunge operation is provided. */
int main(int argc, char **argv)
{
    struct Library *ReloadprobeBase, *second;
    struct ReloadWitness w;
    const ULONG generations[] = {1, 2, 1};
    unsigned i;
    if (argc != 3) {
        puts("usage: ReloadCheck path-A/reloadprobe.library path-B/reloadprobe.library");
        return RETURN_ERROR;
    }
    for (i = 0; i < 3; i++) {
        w = (struct ReloadWitness){RELOAD_WITNESS_MAGIC, 0, 0, 0};
        ReloadprobeBase = OpenLibrary(argv[(i == 1) ? 2 : 1], 1);
        if (!ReloadprobeBase) {
            printf("[reload] FAIL cycle=%u reason=open\n", i);
            return RETURN_FAIL;
        }
        if (ReloadIdentity() != generations[i] || !ReloadWatch(&w)) {
            RemLibrary(ReloadprobeBase);
            CloseLibrary(ReloadprobeBase);
            printf("[reload] FAIL cycle=%u reason=identity-or-watch\n", i);
            bug("[reload] FAIL cycle=%u reason=identity-or-watch\n", i);
            return RETURN_FAIL;
        }
        second = OpenLibrary("reloadprobe.library", 1);
        if (!second) {
            RemLibrary(ReloadprobeBase);
            CloseLibrary(ReloadprobeBase);
            puts("[reload] FAIL reason=second-open");
            return RETURN_FAIL;
        }
        RemLibrary(ReloadprobeBase); /* must defer while either client holds it */
        if (w.released || ReloadIdentity() != generations[i]) {
            /* A broken lifetime contract: do not touch possibly freed code. */
            puts("[reload] FAIL reason=expunged-while-open");
            return RETURN_FAIL;
        }
        CloseLibrary(second);
        if (w.released) {
            puts("[reload] FAIL reason=expunged-with-client");
            return RETURN_FAIL;
        }
        CloseLibrary(ReloadprobeBase); /* pending expunge releases the allocation */
        ReloadprobeBase = NULL;
        if (w.attached != 1 || w.released != 1 || w.generation != generations[i]) {
            printf("[reload] FAIL cycle=%u reason=release attached=%lu released=%lu\n",
                   i, (unsigned long)w.attached, (unsigned long)w.released);
            return RETURN_FAIL;
        }
        printf("[reload] cycle=%u generation=%lu release=1 busy_refusal=1\n",
               i, (unsigned long)w.generation);
        bug("[reload] cycle=%u generation=%u release=1 busy_refusal=1\n",
            i, (unsigned)w.generation);
    }
    puts("[reload] PASS_SCOPED synchronous-library cycles=3 rollback=1");
    bug("[reload] PASS_SCOPED synchronous-library cycles=3 rollback=1\n");
    return RETURN_OK;
}
