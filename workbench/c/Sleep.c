/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.

    Sleep CLI command: the program lifecycle of a sleep transition.
*/

/******************************************************************************

    NAME

        Sleep

    SYNOPSIS

        DEPTH/K,FOR/N,QUIET/S

    LOCATION

        C:

    FUNCTION

        Perform the program side of a sleep transition through
        power.library: tell the subscribed programs and give them the
        depth's deadline to reply, stop every task except this one, hold
        the up time so relative timeouts measure awake time only, stay in
        that state for FOR seconds, let the up time run again, restart every
        task and tell the subscribers it is over. On a machine with no hardware for the
        depth this is the whole transition; on one that has it, the drivers
        quiesce and the cores sleep between the two halves.

        No file or console output happens between the freeze and the thaw:
        the handlers are tasks too.

    INPUTS

        DEPTH  --  light, deep or hibernate (recorded; the hardware part of
                   each depth arrives with its driver lifecycle)
        FOR    --  seconds to stay asleep (default 5)
        QUIET  --  print nothing

    RESULT

        One line: [SLEEP] depth=<d> for-s=<n> frozen=<n> thawed=<n>
        uptime-held=<0|1> wall-s=<n> subscribers=<n> replied=<n>
        proceeded-at-ms=<n> woken=<n>

        uptime-held is 1 when the up time did not advance during the sleep,
        which is what the relative timer requests of frozen programs rely on.

******************************************************************************/

#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <libraries/power.h>
#include <workbench/startup.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/power.h>

#include <aros/debug.h>
#include <string.h>

const TEXT version[] = "$VER: Sleep 1.1 (16.10.2026)\n";

int __nocommandline;
/* A CLI command: no Workbench startup message to wait for, no console
 * window of its own. The cold-start proof runs it from a process with no
 * CLI, and the Workbench startup object would wait there forever. */
int __nowbsupport = 1;
int __nostdiowin = 1;
struct WBStartup *WBenchMsg;

struct Library *PowerBase;

int main(void)
{
    IPTR                    args[3] = { 0, 0, 0 };
    struct RDArgs          *rda;
    CONST_STRPTR            depth_name = "light";
    ULONG                   depth = POWER_DEPTH_LIGHT;
    ULONG                   seconds = 5;
    struct PowerSleepResult result;
    struct TagItem          tags[3];
    LONG                    status;
    LONG                    error = RETURN_FAIL;

    kprintf("[SLEEP] start input=%p output=%p\n", BADDR(Input()), BADDR(Output()));
    rda = ReadArgs("DEPTH/K,FOR/N,QUIET/S", args, NULL);
    if (rda == NULL)
    {
        kprintf("[SLEEP] ReadArgs failed ioerr=%ld\n", (long)IoErr());
        PrintFault(IoErr(), "Sleep");
        return RETURN_FAIL;
    }
    if (args[0])
        depth_name = (CONST_STRPTR)args[0];
    if (args[1])
        seconds = *(ULONG *)args[1];

    if (!strcmp(depth_name, "light"))
        depth = POWER_DEPTH_LIGHT;
    else if (!strcmp(depth_name, "deep"))
        depth = POWER_DEPTH_DEEP;
    else if (!strcmp(depth_name, "hibernate"))
        depth = POWER_DEPTH_HIBERNATE;
    else
    {
        PutStr("Sleep: DEPTH is light, deep or hibernate\n");
        goto end;
    }

    PowerBase = OpenLibrary("power.library", 1);
    if (PowerBase == NULL)
    {
        kprintf("[SLEEP] power.library missing\n");
        PutStr("Sleep: power.library is missing\n");
        goto end;
    }

    /* Everything printed before the transition is out before the handler
     * freezes; nothing is printed again before the thaw. */
    if (!args[2])
    {
        Printf("Sleep: depth=%s for %lu s, freezing every other task\n", depth_name, seconds);
        Flush(Output());
    }

    tags[0].ti_Tag = POWER_Seconds; tags[0].ti_Data = seconds;
    tags[1].ti_Tag = POWER_Result;  tags[1].ti_Data = (IPTR)&result;
    tags[2].ti_Tag = TAG_DONE;
    status = PowerSleepA(depth, tags);

    if (status == POWER_ERROR_UNSUPPORTED)
    {
        if (!args[2])
            PutStr("Sleep: this build cannot freeze tasks (SMP); nothing done\n");
        goto end;
    }
    if (status != POWER_OK)
    {
        if (!args[2])
            Printf("Sleep: transition refused, code %ld\n", (LONG)status);
        goto end;
    }

    if (!args[2])
        Printf("[SLEEP] depth=%s for-s=%lu frozen=%lu thawed=%lu uptime-held=%ld wall-s=%lu "
               "subscribers=%lu replied=%lu proceeded-at-ms=%lu woken=%lu\n",
               depth_name, seconds, result.psr_Frozen, result.psr_Thawed,
               (LONG)(result.psr_UptimeHeldMs < 500), result.psr_WallSeconds,
               result.psr_Subscribers, result.psr_Replied, result.psr_ProceededAtMs, result.psr_Woken);

    error = (result.psr_Frozen == result.psr_Thawed && result.psr_UptimeHeldMs < 500) ? RETURN_OK : RETURN_WARN;

end:
    if (PowerBase != NULL)
        CloseLibrary(PowerBase);
    FreeArgs(rda);
    return error;
}
