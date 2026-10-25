/*
 * Copyright (C) 2013-2026, The AROS Development Team. All rights reserved.
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef AROS_AARCH64_NATIVE_TASK_CONTEXT_H
#define AROS_AARCH64_NATIVE_TASK_CONTEXT_H

#include "task_tls.h"

/*
 * FPU/NEON register block pointed to by ExceptionContext.fpuContext.
 * Layout is shared with the exception-frame FPU area and the
 * cpu_Save/Restore_FP_State() helpers: q0-q31 as 64 dwords, then
 * fpcr and fpsr.
 */
struct VFPContext
{
    UQUAD fpr[64];  /* q0-q31 (each 128 bits = 2 dwords) */
    UQUAD fpcr;
    UQUAD fpsr;
};

/*
 * AArch64 exception frame layout. The first part is struct
 * ExceptionContext (arch/aarch64-all/include/aros/cpucontext.h):
 *   [sp + 0]    x0-x28   (29 * 8 = 232 bytes)
 *   [sp + 232]  fp (x29)
 *   [sp + 240]  lr (x30)
 *   [sp + 248]  sp
 *   [sp + 256]  pc (elr_el1)
 *   [sp + 264]  cpsr (spsr_el1, ULONG)
 *   [sp + 268]  Flags (ULONG, ECF_FPU)
 *   [sp + 272]  fpuContext (points at sp + 288)
 * then, 16-byte aligned, the FPU block saved by vectors.S at entry
 * (C exception handlers and driver interrupt code freely use the
 * q-registers; only rom/kernel is compiled with -mgeneral-regs-only):
 *   [sp + 288]  q0-q31 (512 bytes)
 *   [sp + 800]  fpcr
 *   [sp + 808]  fpsr
 *   Total: 816 bytes, 16-byte aligned
 */
#define AARCH64_FRAME_SIZE  816
#define AARCH64_FRAME_FPU   288

/*
 * Store current task state from exception frame to ExceptionContext.
 * regs points to the exception frame on the kernel stack. The frame's
 * fpuContext points into the frame itself; the task context keeps its
 * own FPU block, so only the DATA is copied, never the pointer.
 */
#define STORE_TASKSTATE(task, regs)                                             \
    struct ExceptionContext *ctx = task->tc_UnionETask.tc_ETask->et_RegFrame;   \
    struct ExceptionContext *__frame = (struct ExceptionContext *)(regs);       \
    int __task_reg_no;                                                          \
    for (__task_reg_no = 0; __task_reg_no < 29; __task_reg_no++)                \
    {                                                                           \
        ctx->x[__task_reg_no] = __frame->x[__task_reg_no];                      \
    }                                                                           \
    ctx->fp = __frame->fp;                                                      \
    ctx->lr = __frame->lr;                                                      \
    ctx->sp = __frame->sp;                                                      \
    task->tc_SPReg = (void *)(IPTR)ctx->sp;                                     \
    ctx->pc = __frame->pc;                                                      \
    ctx->cpsr = __frame->cpsr;                                                  \
    ctx->Flags |= ECF_FPU;                                                      \
    for (__task_reg_no = 0; __task_reg_no < 66; __task_reg_no++)                \
    {                                                                           \
        ((UQUAD *)ctx->fpuContext)[__task_reg_no] =                             \
            ((UQUAD *)((char *)(regs) + AARCH64_FRAME_FPU))[__task_reg_no];     \
    }

/*
 * Restore task state from ExceptionContext to exception frame.
 */
#define RESTORE_TASKSTATE(task, regs)                                           \
    struct ExceptionContext *ctx = task->tc_UnionETask.tc_ETask->et_RegFrame;   \
    struct ExceptionContext *__frame = (struct ExceptionContext *)(regs);       \
    int __task_reg_no;                                                          \
    for (__task_reg_no = 0; __task_reg_no < 29; __task_reg_no++)                \
    {                                                                           \
        __frame->x[__task_reg_no] = ctx->x[__task_reg_no];                      \
    }                                                                           \
    __frame->fp = ctx->fp;                                                      \
    ctx->sp = (UQUAD)(IPTR)task->tc_SPReg;                                      \
    __frame->lr = ctx->lr;                                                      \
    __frame->sp = ctx->sp;                                                      \
    __frame->pc = ctx->pc;                                                      \
    __frame->cpsr = ctx->cpsr;                                                  \
    for (__task_reg_no = 0; __task_reg_no < 66; __task_reg_no++)                \
    {                                                                           \
        ((UQUAD *)((char *)(regs) + AARCH64_FRAME_FPU))[__task_reg_no] =        \
            ((UQUAD *)ctx->fpuContext)[__task_reg_no];                          \
    }                                                                           \
    AROSAArch64NativeTaskTLSRestore(ctx);

#endif
