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

/*
 * Publish posixc's own per-task stdc/stdcio rellib bases in the public base
 * fields.  __posixc_startup() (which normally does this) is only linked into
 * the absolute autoinit, so a rel-linked program never runs it and posixc's
 * public StdCBase/StdCIOBase stay NULL.  posixc uses StdCBase both directly
 * (its rel wrappers) and through the public field (the errno macro), and
 * __posixc_nixmain() registers the program exit jmp_buf through it - so the
 * public field must name the very same per-task stdc base posixc uses
 * internally, otherwise a rel-linked program's exit()/errno resolve a
 * different base (bus fault "exit without proper initialization").
 *
 * This runs as an OpenLib hook, after set_open_rellibraries() has populated
 * posixc's rellib bases, so __aros_getbase_StdCBase()/StdCIOBase() here return
 * posixc's own per-task bases.  A rel-linked program's relbases.o then adopts
 * these via PosixCBase->StdCBase rather than opening its own.  The absolute
 * path is unchanged: __posixc_startup() still overwrites the fields with the
 * program globals afterwards, and we only fill them when NULL.
 */
static int __posixc_publish_stdbases(struct PosixCIntBase *PosixCBase)
{
    /* posixc reaches stdc/stdcio through the public StdCBase/StdCIOBase fields
       (its rel wrappers read them, and so does the errno macro).  Normally
       __posixc_startup() wires them to the program's bases, but that only runs
       in the absolute autoinit; a rel-linked program never runs it, leaving
       them NULL and faulting on the first stdc/stdcio call.  Open our own
       per-task bases and publish them; a rel-linked program's relbases.o then
       adopts these so posixc and the program share one stdc/stdcio base.  In
       the absolute case __posixc_startup() overwrites the fields afterwards, so
       behaviour there is unchanged. */
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
