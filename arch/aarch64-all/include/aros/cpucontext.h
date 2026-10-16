/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Desc: CPU context definition for AArch64.

    Shared architectural context for native and hosted AArch64 targets.
    Darwin hosted copies the integer register prefix to/from its thread state;
    native platforms use the same order for saved task state.
*/

#ifndef AROS_AARCH64_CPUCONTEXT_H
#define AROS_AARCH64_CPUCONTEXT_H

#include <exec/types.h>

struct ExceptionContext
{
    UQUAD  x[29];       /* General purpose registers x0-x28              */
    UQUAD  fp;          /* x29, frame pointer                            */
    UQUAD  lr;          /* x30, link register                            */
    UQUAD  sp;          /* Stack pointer                                 */
    UQUAD  pc;          /* Program counter (ELR when trapped)            */
    ULONG  cpsr;        /* Processor state (SPSR when trapped)           */
    ULONG  Flags;       /* ECF_* flags                                   */
    APTR   fpuContext;  /* FPU/NEON register state, if saved             */
};

/*
 * The first six members (x[], fp, lr, sp, pc, cpsr) follow the architectural
 * register order; hosted backends copy thread state to and from this struct
 * as a single block. Do not reorder them.
 */

/* ExceptionContext Flags */
#define ECF_FPU  0x0001 /* fpuContext is valid */

#endif /* AROS_AARCH64_CPUCONTEXT_H */
