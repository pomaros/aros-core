/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: FaultDiag KIND [DELAY n] - crash this process on purpose, one way per KIND,
          so the kernel's answer to a task fault can be qualified: the
          "[KI] task crashed and suspended:" report on the evidence channel
          and in the kernel log (C:KLog FIND "task crashed"), the entry in
          the crash log (C:CrashLog), and a desktop and USB service that
          keep running while this process stays suspended for good.

    KIND:
      READ    load from address 16: data read through NULL+0x10
      WRITE   store to address 16: data write through NULL+0x10
      JUMP    call address 16: instruction fetch at NULL+0x10
      UNDEF   an undefined instruction
      BRK     a breakpoint instruction
      STACK   recursion until the stack is gone: a data abort on the guard,
              SP outside the stack in the report
      FORBID  READ inside Forbid(): the CPU leaves the Forbid with the task
      DISABLE READ inside Disable(): the CPU leaves the Disable with the task
      FREETWICE  AllocMem() then FreeMem() twice: exec reports the second
              free ("[MM] Chunk allocator error" in the kernel log) and posts
              the AN_FreeTwice alert, which must reach the screen with the
              rest of the system running; after it, the command returns
              ("[faultdiag] FREETWICE: returned")

    DELAY waits that many seconds first, so a qualification can let the
    desktop pass its own checks before the crash lands on it.

    One line goes out before the fault; nothing is printed after it, the
    process never returns from the fault. Run it from a Shell of its own
    (Run >NIL: C:FaultDiag READ) to keep the Shell that started it.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdint.h>
#include <string.h>

#define TEMPLATE    "KIND/A,DELAY/N"
#define BAD_ADDRESS ((volatile uint64_t *)(uintptr_t)16)

static volatile uint64_t sink;

static uint64_t recurse(uint64_t depth)
{
    volatile uint64_t frame[64];

    frame[depth % 64] = depth;
    return recurse(depth + 1) + frame[(depth + 1) % 64];
}

int main(void)
{
    IPTR args[2] = { 0, 0 };
    struct RDArgs *rda;
    const char *kind;
    int known = 1;
    LONG delay = 0;

    rda = ReadArgs(TEMPLATE, args, NULL);
    if (rda == NULL || args[0] == 0) {
        PutStr("FaultDiag: KIND is READ, WRITE, JUMP, UNDEF, BRK, STACK, FORBID, DISABLE or FREETWICE\n");
        if (rda)
            FreeArgs(rda);
        return RETURN_ERROR;
    }
    kind = (const char *)args[0];
    if (args[1])
        delay = *(LONG *)args[1];
    if (delay > 0 && delay <= 600)
        Delay((ULONG)delay * TICKS_PER_SECOND);
    Printf("[faultdiag] %s: faulting now, task \"%s\"\n", kind,
        (IPTR)FindTask(NULL)->tc_Node.ln_Name);
    Flush(Output());

    if (!strcmp(kind, "READ"))
        sink = *BAD_ADDRESS;
    else if (!strcmp(kind, "WRITE"))
        *BAD_ADDRESS = 1;
    else if (!strcmp(kind, "JUMP"))
        ((void (*)(void))(uintptr_t)16)();
    else if (!strcmp(kind, "UNDEF"))
        __asm__ volatile("udf #0");
    else if (!strcmp(kind, "BRK"))
        __asm__ volatile("brk #0");
    else if (!strcmp(kind, "STACK"))
        sink = recurse(0);
    else if (!strcmp(kind, "FORBID")) {
        Forbid();
        sink = *BAD_ADDRESS;
        Permit();
    } else if (!strcmp(kind, "DISABLE")) {
        Disable();
        sink = *BAD_ADDRESS;
        Enable();
    } else if (!strcmp(kind, "FREETWICE")) {
        APTR block = AllocMem(4096, MEMF_ANY);

        if (block != NULL) {
            FreeMem(block, 4096);
            FreeMem(block, 4096);
        }
        FreeArgs(rda);
        Printf("[faultdiag] FREETWICE: returned (block %p)\n", (IPTR)block);
        return block != NULL ? RETURN_OK : RETURN_FAIL;
    }
    else
        known = 0;

    FreeArgs(rda);
    if (!known) {
        PutStr("FaultDiag: KIND is READ, WRITE, JUMP, UNDEF, BRK, STACK, FORBID, DISABLE or FREETWICE\n");
        return RETURN_ERROR;
    }
    /* Not reached when the kernel routes the fault: the kernel suspends
     * the task. Reached means the fault was swallowed. */
    PutStr("[faultdiag] result=FAIL the fault returned\n");
    return RETURN_FAIL;
}
