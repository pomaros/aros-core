/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/dos.h>
#include <dos/dostags.h>
#include <aros/debug.h>
#include <aros/config.h>
#include <dos/var.h>
int main(void)
{
#if defined(__AROSEXEC_SMP__)
    UBYTE accepted[8];
    unsigned attempt;
    for (attempt=0;attempt<200;++attempt) {
        if (GetVar((CONST_STRPTR)"Sys/DesktopAccepted",accepted,sizeof(accepted),GVF_GLOBAL_ONLY)>0) break;
        Delay(5);
    }
    if (attempt==200) { bug("[ISOVM] FAIL desktop barrier timeout\n");return 20; }
    Delay(500);
#endif
    LONG result = SystemTags((CONST_STRPTR)"VMWORK0:System/C/RestrictedTasks",
        NP_StackSize,(IPTR)(256*1024),TAG_DONE);
    bug("[ISOVM] observed-command-rc=%ld\n",(long)result);
    return result;
}
