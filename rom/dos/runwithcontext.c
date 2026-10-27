/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include "dos_intern.h"
#include <dos/context.h>
#include <dos/dostags.h>
#include <proto/dos.h>

struct ContextCall {
    struct Task *caller;
    ULONG signal;
    struct DosLibrary *dos;
    DosContextCallback callback;
    APTR data;
    SIPTR value, error;
};

/* This and the exit callback reside in DOS, which cannot be expunged. The
 * caller may release its module after receiving completion: no callback code in
 * that module remains on the helper's stack at that point. */
static SIPTR context_entry(void)
{
    struct ContextCall *call = FindTask(NULL)->tc_UserData;
    struct DosLibrary *DOSBase = call->dos;
    SetIoErr(0);
    call->value = call->callback(call->data, DOSBase);
    call->error = IoErr();
    /* DOS runs NP_ExitCode before normal directory cleanup. Release the two
     * owned copies here so they cannot cause transient OBJECT_IN_USE after
     * our synchronous caller has already resumed. */
    BPTR current = CurrentDir(BNULL);
    BPTR home = SetProgramDir(BNULL);
    UnLock(current);
    UnLock(home);
    return 0;
}

#if defined(__mc68000__) || defined(__m68k__)
/* DosEntry's m68k stack path supplies ExitData before the result. */
static void context_exited(IPTR data, IPTR result)
#else
static void context_exited(IPTR result, IPTR data)
#endif
{
    struct ContextCall *call = (struct ContextCall *)data;
    struct Task *caller = call->caller;
    ULONG signal = call->signal;
    (void)result;
    /* Last operation. The caller always waits for this signal before freeing
     * its request or reusing the bit. No port is published and polled early. */
    Signal(caller, signal);
}

static BPTR context_copy(BPTR lock, SIPTR *error, struct DosLibrary *DOSBase)
{
    struct FileLock *file = BADDR(lock);
    SIPTR args[7] = {(SIPTR)lock, 0, 0, 0, 0, 0, 0};
    BPTR copy;
    struct MsgPort *port = file ? file->fl_Task : DOSBase->dl_Root->rn_BootProc;
    LONG action = ACTION_COPY_DIR;
    if (!port) { *error = ERROR_INVALID_LOCK; return BNULL; }
    if (!file) {
        action = ACTION_LOCATE_OBJECT;
        args[1] = (SIPTR)AROS_CONST_BSTR("");
        args[2] = SHARED_LOCK;
    }
    copy = (BPTR)DoPktResult(port, action, args, error);
    if (!copy && !*error) *error = ERROR_INVALID_LOCK;
    return copy;
}

/* Run a DOS-only callback under owned cwd/progdir locks in a temporary
 * Process. Returns its primary result and explicit IoErr through result2.
 * Zero with a nonzero result2 can also mean admission failure. The caller
 * must keep data and callback code alive until return. This synchronous API
 * has no forced-caller-removal or cancellation guarantee: that transport
 * lifecycle remains a separate requirement. Introduced in DOS 50.86, after DoPktResult228. BNULL cwd denotes
 * the boot root and is materialized by LOCATE_OBJECT; BNULL progdir inherits
 * cwd. Callbacks must not change the helper's cwd or program directory.
 * The caller's Process pr_Result2 may be changed during admission; use the
 * explicit result2 output. DOS creation reads its ordinary Process defaults,
 * but this request retains no caller Process or caller-owned console port. */
AROS_LH5(SIPTR, RunWithContext,
    AROS_LHA(BPTR, cwd, D0),
    AROS_LHA(BPTR, progdir, D1),
    AROS_LHA(DosContextCallback, callback, A0),
    AROS_LHA(APTR, data, A1),
    AROS_LHA(SIPTR *, result2, A2),
    struct DosLibrary *, DOSBase, 229, Dos)
{
    AROS_LIBFUNC_INIT
    struct ContextCall *call = NULL;
    LONG signal_bit = -1;
    struct Process *process;
    SIPTR error = ERROR_NO_FREE_STORE, value = 0;
    BPTR current = BNULL, home = BNULL;
    struct TagItem tags[] = {
        {NP_Entry, (IPTR)context_entry}, {NP_Name, (IPTR)"DOS context call"},
        {NP_UserData, 0}, {NP_ExitCode, (IPTR)context_exited}, {NP_ExitData, 0},
        {NP_CurrentDir, 0}, {NP_HomeDir, 0},
        {NP_Input, 0}, {NP_Output, 0}, {NP_Error, 0},
        {NP_CloseInput, FALSE}, {NP_CloseOutput, FALSE}, {NP_CloseError, FALSE},
        {NP_CopyVars, FALSE}, {NP_Cli, FALSE}, {NP_Synchronous, FALSE},
        {NP_WindowPtr, (IPTR)-1}, {NP_StackSize, 65536},
        {NP_ConsoleTask, 0}, {TAG_DONE, 0}
    };
    if (!callback) { error = ERROR_BAD_NUMBER; goto done; }
    /* The gate does not cast its caller to Process. Capture locks by explicit
     * packets; DOS creation itself may consult its normal Process defaults. */
    current = context_copy(cwd, &error, DOSBase);
    if (!current) goto done;
    home = context_copy(progdir ? progdir : cwd, &error, DOSBase);
    if (!home) goto done;
    error = ERROR_NO_FREE_STORE;
    call = AllocVec(sizeof(*call), MEMF_PUBLIC | MEMF_CLEAR);
    if (!call) goto done;
    signal_bit = AllocSignal(-1);
    if (signal_bit < 0) goto done;
    call->caller = FindTask(NULL);
    call->signal = 1UL << signal_bit;
    SetSignal(0, call->signal);
    call->dos = DOSBase;
    call->callback = callback;
    call->data = data;
    tags[2].ti_Data = tags[4].ti_Data = (IPTR)call;
    tags[5].ti_Data = (IPTR)current;
    tags[6].ti_Data = (IPTR)home;
    process = CreateNewProc(tags);
    if (!process) goto done;
    /* Successful creation transfers both copies to Process cleanup. */
    current = home = BNULL;
    /* Do not poll a completion flag before Wait: the helper may still be
     * about to send the signal and the allocated bit must remain owned. */
    Wait(call->signal);
    value = call->value;
    error = call->error;
done:
    if (signal_bit >= 0) FreeSignal(signal_bit);
    if (call) FreeVec(call);
    if (current) UnLock(current);
    if (home) UnLock(home);
    if (result2) *result2 = error;
    return value;
    AROS_LIBFUNC_EXIT
}
