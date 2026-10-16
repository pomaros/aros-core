/*
    Copyright (C) 2026, John KNIPPER. All rights reserved.

    Desc: Fail-closed guard for the unimplemented EL0 SuperState transition.
*/

    .text
    .balign 4

    .global cpu_SuperState
    .type cpu_SuperState, %function

/*
 * The Apple bootstrap enters AROS at EL1, so KrnIsSuper() prevents the legacy
 * SuperState() path from calling this helper.  Do not manufacture a return
 * value if that invariant is broken: EL0 support requires an explicit SVC,
 * exception-frame and UserState design.
 */
cpu_SuperState:
    brk     #0
1:
    b       1b

    .size cpu_SuperState, . - cpu_SuperState
