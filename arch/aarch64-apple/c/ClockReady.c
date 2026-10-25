/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: ClockReady [WAIT seconds] - wait until the kernel has set the
          system clock from the PMIC clock.

    The kernel's desktop hold reads the PMIC clock and sets the system
    time while the Startup-Sequence already runs; until then the clock
    holds the boot volume's date. A program that keeps time from the clock
    (dhclient's lease deadlines, SNTP's correction) waits for it first:
    S:NetStart starts with this command. The kernel publishes what it did
    through KrnGetSystemAttr(AROS_APPLE_KATTR_SYSTEM_TIME), set or failed;
    nothing published yet means it has not got there.

    RC 0 when the clock was set from the PMIC, 5 when the kernel reported
    that it could not, or WAIT seconds (default 30) passed first.
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <aros/apple/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>

APTR KernelBase;

/* 1: set, -1: the kernel tried and failed, 0: nothing published yet. */
static int clock_state(void)
{
    IPTR p = KrnGetSystemAttr(AROS_APPLE_KATTR_SYSTEM_TIME);
    const struct AROSAppleSystemTimeInfo *info = (const struct AROSAppleSystemTimeInfo *)p;

    if (p == 0 || p == (IPTR)-1 || info->version != AROS_APPLE_SYSTEM_TIME_VERSION ||
        info->size < sizeof(*info))
        return 0;
    return (info->flags & AROS_APPLE_SYSTEM_TIME_RTC_SET) && info->unix_seconds ? 1 : -1;
}

int main(void)
{
    IPTR args[1] = { 0 };
    struct RDArgs *rda = ReadArgs("WAIT/N", args, NULL);
    LONG seconds = 30, tick;
    int state = 0;

    if (!rda) {
        PrintFault(IoErr(), "ClockReady");
        return RETURN_ERROR;
    }
    if (args[0])
        seconds = *(LONG *)args[0];
    FreeArgs(rda);
    KernelBase = OpenResource("kernel.resource");
    if (!KernelBase)
        return RETURN_WARN;
    for (tick = 0; tick <= seconds * 10; ++tick) {
        if ((state = clock_state()) != 0)
            break;
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
        Delay(TICKS_PER_SECOND / 10);
    }
    return state == 1 ? RETURN_OK : RETURN_WARN;
}
