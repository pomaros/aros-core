/*
 * Copyright (C) 2026 John KNIPPER.
 * Distributed under the AROS Public License 1.1.
 * AArch64 CLI regression: requires more than 128 MiB of executable storage.
 */
#include <stdio.h>
int __nowbsupport = 1;
int __nostdiowin = 1;
void *WBenchMsg;

#if defined(__aarch64__)
extern unsigned long branch_forward(void);
extern unsigned long branch_backward(void);
__asm__(
    ".text\n"
    ".balign 16\n"
    ".global branch_near\n"
    ".type branch_near,%function\n"
    "branch_near:\n"
    "mov x0,#21\n"
    "ret\n"
    ".size branch_near,.-branch_near\n"
    ".global branch_forward\n"
    ".type branch_forward,%function\n"
    "branch_forward:\n"
    "stp x29,x30,[sp,#-16]!\n"
    "bl branch_far\n"
    "ldp x29,x30,[sp],#16\n"
    "ret\n"
    ".size branch_forward,.-branch_forward\n"
    ".space 134217792,0\n"
    ".global branch_far\n"
    ".type branch_far,%function\n"
    "branch_far:\n"
    "mov x0,#42\n"
    "ret\n"
    ".size branch_far,.-branch_far\n"
    ".global branch_backward\n"
    ".type branch_backward,%function\n"
    "branch_backward:\n"
    "stp x29,x30,[sp,#-16]!\n"
    "bl branch_near\n"
    "ldp x29,x30,[sp],#16\n"
    "ret\n"
    ".size branch_backward,.-branch_backward\n"
);
#endif

int main(void)
{
#if defined(__aarch64__)
    if (branch_forward() != 42 || branch_backward() != 21) {
        puts("ELF-LONG-BRANCH=FAIL");
        return 20;
    }
    puts("ELF-LONG-BRANCH=PASS");
    return 0;
#else
    puts("ELF-LONG-BRANCH=UNSUPPORTED");
    return 20;
#endif
}
