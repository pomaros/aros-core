/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License 1.1. */
#include LC_LIBDEFS_FILE
#include "__stdc_intbase.h"
#include <exec/memory.h>
#include <proto/exec.h>
#define __TASK_NOLIBBASE__
#include <proto/task.h>
#include <aros/symbolsets.h>
#include <aros/crt_thread_context.h>
#include <stddef.h>
#include <limits.h>

struct StdCErrnoState {
    int error; /* accessor returns this cell through the native task slot */
    ULONG references;
    ULONG owner_id;
};
_Static_assert(offsetof(struct StdCErrnoState, error) == 0,
    "native task error slot must address the error cell");
static struct Library *TaskResBase;
static LONG error_slot;
static struct CRTThreadContextHooks error_hooks;
static BOOL error_registered;

static int error_open(struct StdCIntBase *base)
{
    struct StdCErrnoState *state;
    ULONG owner = GetETaskID(FindTask(NULL));
    if (!owner || error_slot <= 0) return FALSE;
    state = (APTR)GetTaskStorageSlot(error_slot);
    if (state) {
        if (state->owner_id != owner || !state->references ||
            state->references == (ULONG)-1) return FALSE;
        state->references++;
    } else {
        state = AllocVec(sizeof(*state), MEMF_PUBLIC | MEMF_CLEAR);
        if (!state) return FALSE;
        state->owner_id = owner;
        state->references = 1;
        if (!SetTaskStorageSlot(error_slot, (IPTR)state)) {
            FreeVec(state);
            return FALSE;
        }
    }
    base->errno_state = state;
    base->StdCBase._errno_slot = error_slot;
    base->StdCBase._errno_task_resource = TaskResBase;
    return TRUE;
}
/* Run before generated CloseLib changes references, offsets or other bases.
 * Forced owner removal uses the separate post-stop module cleanup protocol. */
static int error_can_close(struct StdCIntBase *base)
{
    struct StdCErrnoState *state = base->errno_state;
    return !state || (state->owner_id &&
        state->owner_id == GetETaskID(FindTask(NULL)) &&
        (APTR)GetTaskStorageSlot(error_slot) == state);
}
static void error_close(struct StdCIntBase *base)
{
    struct StdCErrnoState *state = base->errno_state;
    if (!state) return;
    base->errno_state = NULL;
    if (--state->references == 0) {
        SetTaskStorageSlot(error_slot, 0);
        FreeVec(state);
    }
}

struct StdCErrnoAdmission { struct Library *worker; };
static APTR error_select(void)
{
    return error_slot > 0 ? (APTR)GetTaskStorageSlot(error_slot) : NULL;
}
static BOOL error_capture(APTR source, APTR *output)
{
    /* errno is not inherited. Capture only owns the worker admission token. */
    *output = NULL;
    if (!source) return FALSE;
    *output = AllocVec(sizeof(struct StdCErrnoAdmission), MEMF_PUBLIC | MEMF_CLEAR);
    return *output != NULL;
}
static BOOL error_enter(APTR context)
{
    struct StdCErrnoAdmission *admission = context;
    if (!admission) return FALSE;
    admission->worker = OpenLibrary((CONST_STRPTR)"stdc.library", 0);
    return admission->worker != NULL;
}
static struct Library *error_leave(APTR context)
{
    struct StdCErrnoAdmission *admission = context;
    struct Library *worker = admission ? admission->worker : NULL;
    FreeVec(admission);
    return worker; /* close only after leaving this provider's code */
}
static void error_cancel(APTR context)
{
    FreeVec(context); /* only before successful Enter */
}
static int error_init(struct StdCIntBase *base)
{
    (void)base;
    TaskResBase = OpenResource((CONST_STRPTR)"task.resource");
    /* Resource startup installs vectors but does not populate lib_NegSize.
     * task.resource publishes its API through the explicit version header. */
    if (!TaskResBase || TaskResBase->lib_Version < 2)
        return FALSE;
    error_slot = AllocTaskStorageSlot();
    if (error_slot <= 0) return FALSE;
    error_hooks.tc_Version = CRT_THREAD_CONTEXT_VERSION;
    error_hooks.tc_LibName = "stdc.library";
    error_hooks.tc_MinVersion = 0;
    error_hooks.tc_Select = error_select;
    error_hooks.tc_Capture = error_capture;
    error_hooks.tc_Enter = error_enter;
    error_hooks.tc_Leave = error_leave;
    error_hooks.tc_Cancel = error_cancel;
    error_registered = CRTThreadContextRegister(&error_hooks);
    if (!error_registered) {
        FreeTaskStorageSlot(error_slot);
        error_slot = 0;
    }
    return error_registered;
}
static int error_expunge(struct StdCIntBase *base)
{
    (void)base;
    if (error_registered) CRTThreadContextUnregister(&error_hooks);
    error_registered = FALSE;
    if (error_slot > 0) FreeTaskStorageSlot(error_slot);
    error_slot = 0;
    return TRUE;
}
ADD2INITLIB(error_init, 1);
ADD2EXPUNGELIB(error_expunge, 1);
ADD2OPENLIB(error_open, -100);
ADD2CANCLOSELIB(error_can_close, 0);
ADD2CLOSELIB(error_close, -100);
ADD2ABORTOPENLIB(error_close, -100);
