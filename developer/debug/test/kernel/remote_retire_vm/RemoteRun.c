/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <aros/debug.h>
int main(void)
{
    /* Startup must exit before the desktop starts the secondary CPU. This
     * runner is launched asynchronously and waits for that public barrier. */
    char accepted[8];
    unsigned attempt;
    for (attempt=0;attempt<200;++attempt) {
        if (GetVar((CONST_STRPTR)"Sys/DesktopAccepted",accepted,sizeof(accepted),
                   GVF_GLOBAL_ONLY)>0) break;
        Delay(5);
    }
    if (attempt==200) {
        bug("[RETIRE] FAIL desktop barrier timeout\n");return 20;
    }
    /* Leave the bounded native compute/wake proof its measurement interval.
     * RemoteRetire independently checks actual CPU identity for every victim. */
    Delay(500);
    LONG result = SystemTags((CONST_STRPTR)"VMWORK0:System/C/RemoteRetire",
        NP_StackSize,(IPTR)(256*1024),TAG_DONE);
    bug("[RETIRE] observed-command-rc=%ld\n",(long)result);
    return result;
}
