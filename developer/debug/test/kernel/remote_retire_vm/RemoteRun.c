/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/dos.h>
#include <dos/dostags.h>
#include <aros/debug.h>
int main(void)
{
    LONG result = SystemTags((CONST_STRPTR)"VMWORK0:System/C/RemoteRetire",
        NP_StackSize,(IPTR)(256*1024),TAG_DONE);
    bug("[RETIRE] observed-command-rc=%ld\n",(long)result);
    return result;
}
