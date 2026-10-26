/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1

    Stack bounds and stack switching for Rust crates that grow the stack
    (stacker). An exec task's stack is tc_SPLower..tc_SPUpper, and the
    kernel suspends a task whose stack pointer is outside that range at a
    task switch, so a new stack is entered with NewStackSwap(), which moves
    the bounds along and restores them on return.
*/
#include <exec/tasks.h>
#include <proto/exec.h>
#include <stddef.h>

void *aros_rs_stack_lower(void)
{
    return FindTask(NULL)->tc_SPLower;
}

void aros_rs_call_on_stack(void *lower, size_t size, void (*entry)(void *), void *arg)
{
    struct StackSwapStruct sss;
    struct StackSwapArgs args = {{ (IPTR)arg }};

    sss.stk_Lower = lower;
    sss.stk_Upper = (UBYTE *)lower + size;
    sss.stk_Pointer = sss.stk_Upper;
    NewStackSwap(&sss, (APTR)entry, &args);
}
