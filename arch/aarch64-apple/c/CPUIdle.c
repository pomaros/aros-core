/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: CPUIdle [PLAIN|DEEP|DEFAULT] - show or choose how the cores idle.

    DEEP is the deep WFI (CYC_OVRD WFI_MODE 3: the core powers down until
    an interrupt), PLAIN the clock-gated WFI m1n1 leaves, DEFAULT the
    kernel's own choice (deep on an Apple core unless chosen
    aros,plain-idle = 1). The choice takes effect at each core's next idle
    entry and lasts until the next boot. Without an argument it prints the
    current mode and the idle entries so far.

    Measured with SleepDiag on the M1 (eight cores, panel on): deep idle
    takes about 1.5 W off the CPU awake and 2 W off the system asleep.
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <aros/apple/idle.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>

APTR KernelBase;

static const char *mode_name(uint32_t mode)
{
    return mode == 1 ? "plain" : mode == 2 ? "deep" : "default";
}

int main(void)
{
    IPTR args[1] = { 0 };
    struct RDArgs *rda = ReadArgs("MODE", args, NULL);
    const struct AROSAppleIdleControl *idle;
    IPTR attr;

    if (!rda) {
        PrintFault(IoErr(), "CPUIdle");
        return RETURN_ERROR;
    }
    KernelBase = OpenResource("kernel.resource");
    attr = KernelBase ? KrnGetSystemAttr(AROS_APPLE_KATTR_IDLE_CONTROL) : (IPTR)-1;
    idle = (attr != (IPTR)-1 && attr != 0) ? (const struct AROSAppleIdleControl *)attr : NULL;
    if (!idle || idle->version != AROS_APPLE_IDLE_CONTROL_VERSION || idle->size < sizeof(*idle)) {
        PutStr("CPUIdle: this kernel has no idle control\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    if (args[0]) {
        STRPTR m = (STRPTR)args[0];

        if (!Stricmp(m, "DEEP"))
            *idle->mode = 2;
        else if (!Stricmp(m, "PLAIN"))
            *idle->mode = 1;
        else if (!Stricmp(m, "DEFAULT"))
            *idle->mode = 0;
        else {
            PutStr("CPUIdle: MODE is PLAIN, DEEP or DEFAULT\n");
            FreeArgs(rda);
            return RETURN_ERROR;
        }
    }
    Printf("CPUIdle: %s, %lu idle entries\n", (IPTR)mode_name(*idle->mode),
           (unsigned long)*idle->entries);
    FreeArgs(rda);
    return RETURN_OK;
}
