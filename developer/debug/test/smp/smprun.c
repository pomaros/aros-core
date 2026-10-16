/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL), Version 1.1.
 *
 * Run a loaded DOS command with explicit affinity. The parent owns its segment
 * until Exec publishes the child's exit, not merely until RunCommand returns.
 */
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <aros/debug.h>
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while (0)
#include <stdlib.h>
#include <string.h>

APTR KernelBase;

#if defined(__AROSEXEC_SMP__)
#define STACKSIZE (256UL * 1024UL)
struct Command
{
    BPTR segment;
    char *args;
    CONST_STRPTR name;
    ULONG length, signal, tid, first_cpu, last_cpu, done;
    LONG result, error;
    struct Task *parent;
};

static LONG CommandChild(void)
{
    struct Task *self = FindTask(NULL);
    struct Command *command = self->tc_UserData;

    command->first_cpu = KrnGetCPUNumber();
    __atomic_store_n(&command->tid,
        self->tc_UnionETask.tc_ETask->et_UniqueID, __ATOMIC_RELEASE);
    Signal(command->parent, command->signal);
    command->result = SetProgramName(command->name) ?
        RunCommand(command->segment, STACKSIZE, command->args, command->length) : RETURN_FAIL;
    command->error = IoErr();
    command->last_cpu = KrnGetCPUNumber();
    __atomic_store_n(&command->done, 1, __ATOMIC_RELEASE);
    return command->result;
}

int main(void)
{
    IPTR values[3] = {0};
    struct RDArgs *parsed;
    struct Command command = {0};
    struct Process *child;
    struct ETask *exited;
    BPTR executable, home = BNULL;
    APTR affinity;
    char *arguments;
    LONG cpu, result = RETURN_FAIL;
    BYTE signal;
    ULONG tid;
    size_t length;

    parsed = ReadArgs("CPU/N/A,COMMAND/A,ARGS/F", values, NULL);
    if (!parsed)
    {
        PrintFault(IoErr(), "SMP-Run");
        return RETURN_FAIL;
    }
    KernelBase = OpenResource("kernel.resource");
    cpu = *(LONG *)values[0];
    /* Kernel CPU IDs are dense on the supported SMP targets. */
    if (!KernelBase || cpu < -1 ||
        (cpu >= 0 && (ULONG)cpu >= KrnGetCPUCount()))
    {
        PutStr("SMP-Run: CPU must be -1 (any) or an online CPU index\n");
        goto free_args;
    }
    if (!(FindTask(NULL)->tc_Flags & TF_ETASK))
    {
        PutStr("SMP-Run: parent has no extended task state\n");
        goto free_args;
    }
    length = values[2] ? strlen((char *)values[2]) : 0;
    arguments = malloc(length + 2);
    if (!arguments)
        goto free_args;
    if (length)
        memcpy(arguments, (char *)values[2], length);
    arguments[length++] = '\n';
    arguments[length] = 0;
    command.args = arguments;
    command.length = length;
    command.parent = FindTask(NULL);
    command.segment = LoadSeg((CONST_STRPTR)values[1]);
    if (!command.segment)
    {
        PrintFault(IoErr(), "SMP-Run LoadSeg");
        goto free_string;
    }
    /* RunCommand does not install the executable's PROGDIR. Libraries and
     * applications use it to locate adjacent data (AROSTCP uses its parent
     * for db/). Give the child an owned home lock, not the launcher's. */
    executable = Lock((CONST_STRPTR)values[1], SHARED_LOCK);
    if (!executable)
        goto unload;
    home = ParentDir(executable);
    UnLock(executable);
    if (!home)
        goto unload;
    command.name = (CONST_STRPTR)values[1];
    signal = AllocSignal(-1);
    if (signal < 0)
        goto unload;
    command.signal = 1UL << signal;
    affinity = (APTR)TASKAFFINITY_ANY;
    if (cpu >= 0)
    {
        affinity = KrnAllocCPUMask();
        if (!affinity)
            goto free_signal;
        KrnClearCPUMask(affinity);
        KrnGetCPUMask(cpu, affinity);
    }
    REPORT("[SMP-Run] start cpu=%ld command=%s\n", (long)cpu, (char *)values[1]);
    Flush(Output());
    /* Omit NP_Input: DOS allocates a private NIL handle for RunCommand's
     * argument injection. Never inject into the parent's interactive input.
     * The asynchronous Process pointer is only a success indicator: a short
     * command may have exited before CreateNewProcTags returns. */
    child = CreateNewProcTags(NP_Entry, (IPTR)CommandChild,
        NP_Name, (IPTR)values[1], NP_StackSize, STACKSIZE,
        NP_HomeDir, (IPTR)home,
        NP_UserData, (IPTR)&command, NP_Affinity, (IPTR)affinity,
        NP_Cli, TRUE, NP_NotifyOnDeath, TRUE,
        NP_Output, (IPTR)Output(), NP_CloseOutput, FALSE,
        NP_Error, (IPTR)ErrorOutput(), NP_CloseError, FALSE,
        TAG_DONE);
    if (!child)
    {
        /* AArch64 PrepareContext can only fail before consuming the mask. */
        if (cpu >= 0)
            KrnFreeCPUMask(affinity);
        PrintFault(IoErr(), "SMP-Run CreateNewProc");
        goto free_signal;
    }
    home = BNULL; /* The successful child owns and releases NP_HomeDir. */
    while (!(tid = __atomic_load_n(&command.tid, __ATOMIC_ACQUIRE)))
        Wait(command.signal);
    /* No timeout may unwind this stack while the child still owns pointers
     * into it. Use a separate supervisor to diagnose a stuck command. */
    exited = (struct ETask *)ChildWait(tid);
    if ((IPTR)exited <= CHILD_ACTIVE)
    {
        PutStr("SMP-Run: invalid child wait; retaining live command context\n");
        for (;;)
            Wait(0);
    }
    ChildFree(exited->et_UniqueID);
    if (__atomic_load_n(&command.done, __ATOMIC_ACQUIRE))
    {
        result = command.result;
        if (cpu >= 0 && (command.first_cpu != (ULONG)cpu ||
                         command.last_cpu != (ULONG)cpu))
            result = RETURN_FAIL;
        REPORT("[SMP-Run] exit cpu=%ld first=%lu last=%lu rc=%ld ioerr=%ld reaped=1\n",
            (long)cpu, (unsigned long)command.first_cpu,
            (unsigned long)command.last_cpu, (long)result, (long)command.error);
    }
    else
        PutStr("SMP-Run: child exited without completing RunCommand\n");
    Flush(Output());
free_signal:
    FreeSignal(signal);
unload:
    if (home)
        UnLock(home);
    UnLoadSeg(command.segment);
free_string:
    free(arguments);
free_args:
    FreeArgs(parsed);
    return result;
}
#else
int main(void)
{
    PutStr("SMP-Run requires an SMP build\n");
    return RETURN_FAIL;
}
#endif
