/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: JitDiag - qualify JIT code memory (<aros/apple/jit.h>).

    Reports the kernel's JIT mode, asks the kernel for one page that is
    readable, writable and executable at EL0, writes a tiny function into it
    with writing enabled, switches to executing, calls it and checks the
    answer; then switches back to writing, patches the constant, switches to
    executing and calls it again. Every step prints a line; the last one is
    "[JITDIAG] result=PASS" or "[JITDIAG] result=FAIL reason=...".

    Mode NONE ends with "result=REFUSED-AS-EXPECTED": the request for JIT
    pages must fail and nothing else is tried.

    JitDiag WRITEFAULT additionally stores into the page while it is
    executable. In the SPRR mode that is a data abort the kernel reports as
    a crashed task ("[KI] task crashed and suspended"), so the process never
    returns: run it from a Shell of its own, like FaultDiag. In the RWX mode
    the store succeeds and the line "[JITDIAG] writefault: no fault (RWX)"
    is the expected answer.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <aros/kernel.h>
#include <aros/apple/jit.h>
#include <aros/apple/page_arena.h>

#include <stdint.h>
#include <string.h>

#define TEMPLATE "WRITEFAULT/S"

static void line(const char *text)
{
    Printf("%s\n", text);
    Flush(Output());
}

static void fail(const char *reason)
{
    Printf("[JITDIAG] result=FAIL reason=%s\n", reason);
    Flush(Output());
}

/* mov w0, #imm16 ; ret */
static void emit(uint32_t *code, unsigned imm16)
{
    code[0] = 0x52800000U | ((uint32_t)imm16 << 5);
    code[1] = 0xd65f03c0U;
}

static void sync_code(void *address, size_t size)
{
    uintptr_t cursor, end = (uintptr_t)address + size;

    for (cursor = (uintptr_t)address & ~(uintptr_t)63; cursor < end; cursor += 64)
        __asm__ volatile("dc cvau, %0" :: "r"(cursor) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    for (cursor = (uintptr_t)address & ~(uintptr_t)63; cursor < end; cursor += 64)
        __asm__ volatile("ic ivau, %0" :: "r"(cursor) : "memory");
    __asm__ volatile("dsb ish; isb" ::: "memory");
}

int main(void)
{
    struct RDArgs *args;
    IPTR argv[1] = { 0 };
    struct KernelBase *KernelBase;
    intptr_t mode;
    uint32_t *page;
    int (*function)(void);
    uint64_t rc;
    int value;

    args = ReadArgs(TEMPLATE, argv, NULL);
    KernelBase = OpenResource("kernel.resource");
    if (KernelBase == NULL) {
        fail("no-kernel-resource");
        if (args) FreeArgs(args);
        return RETURN_FAIL;
    }
    mode = KrnGetSystemAttr(AROS_APPLE_KATTR_JIT_MODE);
    Printf("[JITDIAG] mode=%ld (%s)\n", (long)mode,
        mode == AROS_APPLE_JIT_SPRR ? "SPRR" :
        mode == AROS_APPLE_JIT_RWX ? "RWX" :
        mode == AROS_APPLE_JIT_NONE ? "NONE" : "kernel without JIT pages");
    /* A kernel that predates JIT pages answers -1: the same as NONE, and
     * nothing may ever jump into a page it was asked to make executable. */
    if (mode != AROS_APPLE_JIT_SPRR && mode != AROS_APPLE_JIT_RWX)
        mode = AROS_APPLE_JIT_NONE;

    page = KrnAllocPages(NULL, AROS_APPLE_PAGE_SIZE, MEMF_EXECUTABLE | MEMF_PUBLIC);
    if (page == NULL) {
        fail("no-page");
        if (args) FreeArgs(args);
        return RETURN_FAIL;
    }
    Printf("[JITDIAG] page=%p\n", page);

    /* The request: readable, writable and executable at EL0. */
    KrnSetProtection(page, AROS_APPLE_PAGE_SIZE,
        MAP_Readable | MAP_Writable | MAP_Executable);
    rc = AROSAppleJitWriteProtect(1);
    Printf("[JITDIAG] write-enable rc=%lx\n", (unsigned long)rc);
    if (mode == AROS_APPLE_JIT_NONE) {
        if (rc == UINT64_MAX) {
            line("[JITDIAG] result=REFUSED-AS-EXPECTED");
            KrnFreePages(page, AROS_APPLE_PAGE_SIZE);
            if (args) FreeArgs(args);
            return RETURN_OK;
        }
        fail("none-mode-accepted-write");
        KrnFreePages(page, AROS_APPLE_PAGE_SIZE);
        if (args) FreeArgs(args);
        return RETURN_FAIL;
    }
    if (rc != (uint64_t)mode) {
        fail("write-enable-rejected");
        KrnFreePages(page, AROS_APPLE_PAGE_SIZE);
        if (args) FreeArgs(args);
        return RETURN_FAIL;
    }

    emit(page, 42);
    sync_code(page, 8);
    rc = AROSAppleJitWriteProtect(0);
    Printf("[JITDIAG] execute-enable rc=%lx\n", (unsigned long)rc);
    function = (int (*)(void))page;
    value = function();
    Printf("[JITDIAG] call1=%ld\n", (long)value);
    if (value != 42) {
        fail("call1");
        return RETURN_FAIL;
    }

    rc = AROSAppleJitWriteProtect(1);
    emit(page, 4242);
    sync_code(page, 8);
    rc = AROSAppleJitWriteProtect(0);
    value = function();
    Printf("[JITDIAG] call2=%ld\n", (long)value);
    if (value != 4242) {
        fail("call2");
        return RETURN_FAIL;
    }

    if (argv[0]) {
        line("[JITDIAG] writefault: storing into an executable JIT page");
        page[2] = 0;
        line("[JITDIAG] writefault: no fault (RWX)");
    }

    line("[JITDIAG] result=PASS");
    if (args) FreeArgs(args);
    return RETURN_OK;
}
