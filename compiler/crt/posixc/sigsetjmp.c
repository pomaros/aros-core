/*****************************************************************************
    NAME
        int sigsetjmp(

    SYNOPSIS
        sigjmp_buf env, int val)

    FUNCTION
        Save a calling environment for a later return with siglongjmp().

    INPUTS
        env - Writable sigjmp_buf object with its declared alignment. Keep it
            intact and alive while a later jump can use it.
        val - Signal-mask save request, conventionally named savemask.
            Currently ignored, including when nonzero.

    RESULT
        Zero initially. After siglongjmp(), the value supplied to that call,
        with zero replaced by 1.

    NOTES
        Include <setjmp.h>. Pair this call with siglongjmp(), not longjmp().
        The lifetime, thread, expression and local-variable restrictions of
        stdc/setjmp() also apply. This call saves CPU context, not a copy of
        program memory or resource ownership.

    BUGS
        The AROS implementations do not save a signal mask, and siglongjmp()
        does not restore one. A nonzero val therefore does not provide the
        POSIX signal-mask preservation contract. Do not use this pair where
        correctness depends on restoring the mask after a jump.

    SEE ALSO
        siglongjmp(), stdc/setjmp(), stdc/longjmp()

*****************************************************************************/

#if defined(__aarch64__)
#include <setjmp.h>

__asm__(
    ".text\n"
    ".balign 16\n"
    ".globl sigsetjmp\n"
    ".type sigsetjmp,%function\n"
    "sigsetjmp:\n"
    "    stp x19, x20, [x0, #8]\n"
    "    stp x21, x22, [x0, #24]\n"
    "    stp x23, x24, [x0, #40]\n"
    "    stp x25, x26, [x0, #56]\n"
    "    stp x27, x28, [x0, #72]\n"
    "    stp x29, x30, [x0, #88]\n"
    "    mov x1, sp\n"
    "    str x1, [x0, #104]\n"
    "    stp d8,  d9,  [x0, #112]\n"
    "    stp d10, d11, [x0, #128]\n"
    "    stp d12, d13, [x0, #144]\n"
    "    stp d14, d15, [x0, #160]\n"
    "    str x30, [x0, #0]\n"
    "    mov w0, #0\n"
    "    ret\n"
);
#else
#error sigsetjmp has to be implemented for each cpu
#endif
