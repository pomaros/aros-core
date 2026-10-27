/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 * Owned creator capture and worker admission; no legacy callback registry. */
#include LC_LIBDEFS_FILE
#include <proto/exec.h>
#include <aros/symbolsets.h>
#include <aros/crt_thread_context.h>
#include "__posixc_intbase.h"
#include "__cwd_runtime.h"

static struct CRTThreadContextHooks context_hooks;
static BOOL context_registered;
static APTR context_select(void) { return __cwd_runtime_source(); }
static BOOL context_capture(APTR source, APTR *out)
{
    *out = __cwd_admission_capture(source);
    return *out != NULL;
}
static BOOL context_enter(APTR context) { return __cwd_admission_enter(context); }
static struct Library *context_leave(APTR context) { return __cwd_admission_leave(context); }
static void context_cancel(APTR context) { __cwd_admission_cancel(context); }
static int context_init(struct PosixCIntBase *base)
{
    (void)base;
    context_hooks.tc_Version = CRT_THREAD_CONTEXT_VERSION;
    context_hooks.tc_LibName = "posixc.library";
    context_hooks.tc_MinVersion = 0;
    context_hooks.tc_Select = context_select;
    context_hooks.tc_Capture = context_capture;
    context_hooks.tc_Enter = context_enter;
    context_hooks.tc_Leave = context_leave;
    context_hooks.tc_Cancel = context_cancel;
    context_registered = CRTThreadContextRegister(&context_hooks);
    return context_registered;
}
static int context_expunge(struct PosixCIntBase *base)
{
    (void)base;
    if (context_registered) CRTThreadContextUnregister(&context_hooks);
    context_registered = FALSE;
    return TRUE;
}
ADD2INITLIB(context_init, 2);
ADD2EXPUNGELIB(context_expunge, 2);

/* Publish relative C-runtime bases for generated wrappers and program-exit
 * registration. Absolute startup later supplies program bases; relative
 * startup adopts these fields. Errno itself is selected by native task state,
 * independently of which stdc opener supplies these relative entry points. */
static int __posixc_publish_stdbases(struct PosixCIntBase *PosixCBase)
{
    if (PosixCBase->PosixCBase.StdCBase == NULL)
        PosixCBase->PosixCBase.StdCBase =
            (struct StdCBase *)OpenLibrary((CONST_STRPTR)"stdc.library", 0);
    if (PosixCBase->PosixCBase.StdCIOBase == NULL)
        PosixCBase->PosixCBase.StdCIOBase =
            (struct StdCIOBase *)OpenLibrary((CONST_STRPTR)"stdcio.library", 0);

    return TRUE;
}

/* Relative library opens normally populate these public fields before all
   OpenLib hooks. Publish any missing explicit opens before returning the base. */
ADD2OPENLIB(__posixc_publish_stdbases, 90);
