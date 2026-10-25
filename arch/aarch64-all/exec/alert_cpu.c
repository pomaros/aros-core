/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: CPU context parsing routines, AArch64 version: the register
          listing behind "More..." in a Software Failure requester, and
          one step of the frame-pointer chain for its backtrace.
*/

#include <exec/rawfmt.h>
#include <proto/exec.h>

#include <string.h>

#include "exec_intern.h"
#include "exec_util.h"

static const char *gpr_fmt = "X0 =%P  X1 =%P  X2 =%P  X3 =%P\n"
                             "X4 =%P  X5 =%P  X6 =%P  X7 =%P\n"
                             "X8 =%P  X9 =%P  X10=%P  X11=%P\n"
                             "X12=%P  X13=%P  X14=%P  X15=%P\n"
                             "X16=%P  X17=%P  X18=%P  X19=%P\n"
                             "X20=%P  X21=%P  X22=%P  X23=%P\n"
                             "X24=%P  X25=%P  X26=%P  X27=%P\n"
                             "X28=%P  FP =%P  LR =%P  SP =%P\n"
                             "PC =%P  PSTATE=%08lx";

char *FormatCPUContext(char *buffer, struct ExceptionContext *ctx, struct ExecBase *SysBase)
{
    VOID_FUNC dest = buffer ? RAWFMTFUNC_STRING : RAWFMTFUNC_SERIAL;
    char *buf;

    buf = NewRawDoFmt(gpr_fmt, dest, buffer,
                      ctx->x[0], ctx->x[1], ctx->x[2], ctx->x[3],
                      ctx->x[4], ctx->x[5], ctx->x[6], ctx->x[7],
                      ctx->x[8], ctx->x[9], ctx->x[10], ctx->x[11],
                      ctx->x[12], ctx->x[13], ctx->x[14], ctx->x[15],
                      ctx->x[16], ctx->x[17], ctx->x[18], ctx->x[19],
                      ctx->x[20], ctx->x[21], ctx->x[22], ctx->x[23],
                      ctx->x[24], ctx->x[25], ctx->x[26], ctx->x[27],
                      ctx->x[28], ctx->fp, ctx->lr, ctx->sp,
                      ctx->pc, (unsigned long)ctx->cpsr);

    return buf - 1;
}

/*
 * Unwind a single stack frame. The AAPCS64 frame record is two words:
 * the caller's frame pointer, then the return address. Code built with
 * -fno-omit-frame-pointer keeps the chain; the caller checks each frame
 * address with TypeOfMem before following it.
 */
APTR UnwindFrame(APTR fp, APTR *caller)
{
    APTR *record = fp;

    *caller = record[1];    /* the return address of this frame   */
    return record[0];       /* the previous frame record          */
}
