/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: MemProbe [MB n] [CHUNK n] [FILE path] [WAIT ticks] [WRITE path] - which memory a stop comes
          with when it is filled.

    Allocates MB (4096) megabytes in CHUNK (64) MB blocks, one at a time;
    it first appends "region <name> <lower> <upper> <physical lower> free <MB>"
    for each exec memory header, then before filling each block it appends "<index> <start> <end> <physical
    start>" into FILE (RAM:memprobe.txt), closed at once so that the host can
    fetch it while the command runs, waits WAIT (100) ticks, then fills the block with a pattern and
    reads it back. A machine that stops while a block is filled leaves that
    block's line in the file. The blocks are freed at the end; the last line
    is "MemProbe: <n> blocks filled, <errors> errors".

    With WRITE, it instead writes MB megabytes into that file in 4 MB
    writes and appends "<MB written> <ms for the last 256 MB>" every 256 MB,
    then "MemProbe: <MB> MB written in <ms> ms".
*/

#include <aros/kernel.h>
#include <dos/dos.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/kernel.h>

#include <stdio.h>
#include <string.h>

APTR KernelBase;

static void note(const char *path, const char *line)
{
    BPTR f = Open((CONST_STRPTR)path, MODE_READWRITE);

    if (f != BNULL) {
        Seek(f, 0, OFFSET_END);
        Write(f, (APTR)line, strlen(line));
        Close(f);
    }
}

int main(void)
{
    IPTR args[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rda = ReadArgs("MB/N,CHUNK/N,FILE/K,WAIT/N,WRITE/K", args, NULL);
    LONG mb = rda && args[0] ? *(LONG *)args[0] : 4096;
    LONG chunk_mb = rda && args[1] ? *(LONG *)args[1] : 64;
    const char *path = rda && args[2] ? (const char *)args[2] : "RAM:memprobe.txt";
    LONG wait = rda && args[3] ? *(LONG *)args[3] : 100;
    ULONG count, i, errors = 0, filled = 0;
    UQUAD **blocks;
    size_t chunk;
    char line[160];

    if (rda == NULL) {
        PrintFault(IoErr(), "MemProbe");
        return RETURN_ERROR;
    }
    if (chunk_mb < 1)
        chunk_mb = 64;
    KernelBase = OpenResource("kernel.resource");
    if (args[4]) {
        struct DateStamp t0, t1, tl;
        UBYTE *buf = AllocVec(4 << 20, MEMF_ANY | MEMF_CLEAR);
        BPTR out = Open((CONST_STRPTR)args[4], MODE_NEWFILE);
        LONG written = 0;

        if (buf == NULL || out == BNULL) {
            Printf("MemProbe: cannot write %s\n", args[4]);
            if (out != BNULL)
                Close(out);
            FreeVec(buf);
            FreeArgs(rda);
            return RETURN_FAIL;
        }
        DateStamp(&t0);
        tl = t0;
        while (written < mb && Write(out, buf, 4 << 20) == (4 << 20)) {
            written += 4;
            if (written % 256 == 0) {
                DateStamp(&t1);
                snprintf(line, sizeof(line), "%ld %ld\n", (long)written,
                    (long)(((t1.ds_Minute - tl.ds_Minute) * 3000 + t1.ds_Tick - tl.ds_Tick) * 20));
                note(path, line);
                tl = t1;
            }
        }
        Close(out);
        DateStamp(&t1);
        snprintf(line, sizeof(line), "MemProbe: %ld MB written in %ld ms\n", (long)written,
            (long)(((t1.ds_Minute - t0.ds_Minute) * 3000 + t1.ds_Tick - t0.ds_Tick) * 20));
        note(path, line);
        Printf("%s", (IPTR)line);
        FreeVec(buf);
        FreeArgs(rda);
        return RETURN_OK;
    }
    chunk = (size_t)chunk_mb << 20;
    count = (ULONG)(mb / chunk_mb);
    {
        struct MemHeader *mh;
        char all[1024];
        size_t used = 0;

        Forbid();
        ForeachNode(&SysBase->MemList, mh) {
            if (used < sizeof(all) - 120)
                used += snprintf(all + used, sizeof(all) - used, "region %s %p %p %p free %lu\n",
                    mh->mh_Node.ln_Name ? mh->mh_Node.ln_Name : "?", mh->mh_Lower, mh->mh_Upper,
                    KrnVirtualToPhysical(mh->mh_Lower), (unsigned long)(mh->mh_Free >> 20));
        }
        Permit();
        note(path, all);
        Printf("%s", (IPTR)all);
    }
    blocks = AllocVec(count * sizeof(*blocks), MEMF_ANY | MEMF_CLEAR);
    if (blocks == NULL || KernelBase == NULL) {
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    for (i = 0; i < count; ++i) {
        UQUAD *p = AllocVec(chunk, MEMF_ANY);
        size_t n, words = chunk / sizeof(UQUAD);

        if (p == NULL) {
            snprintf(line, sizeof(line), "%lu no memory\n", (unsigned long)i);
            note(path, line);
            break;
        }
        blocks[i] = p;
        snprintf(line, sizeof(line), "%lu %p %p %p\n", (unsigned long)i, (void *)p,
            (void *)((UBYTE *)p + chunk), KrnVirtualToPhysical(p));
        note(path, line);
        Printf("%s", (IPTR)line);
        Delay(wait);
        for (n = 0; n < words; ++n)
            p[n] = (UQUAD)(IPTR)&p[n] ^ UINT64_C(0x5a5a5a5aa5a5a5a5);
        for (n = 0; n < words; ++n)
            if (p[n] != ((UQUAD)(IPTR)&p[n] ^ UINT64_C(0x5a5a5a5aa5a5a5a5)))
                ++errors;
        ++filled;
    }
    for (i = 0; i < count; ++i)
        FreeVec(blocks[i]);
    FreeVec(blocks);
    snprintf(line, sizeof(line), "MemProbe: %lu blocks filled, %lu errors\n", (unsigned long)filled,
        (unsigned long)errors);
    note(path, line);
    Printf("%s", (IPTR)line);
    FreeArgs(rda);
    return errors == 0 ? RETURN_OK : RETURN_FAIL;
}
