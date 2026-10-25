#ifndef ___DL_H
#define ___DL_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: internals of dlopen(), dlsym(), dlclose() and dlerror()
*/

#include <dos/dos.h>
#include <exec/nodes.h>
#include <aros/dlmodule.h>

#include "__posixc_intbase.h"

/* One loaded module. The list lives in the owner base (__dl_owner), so a
   Process and its pthread workers share it; dl_sem guards it. */
struct __dl_module
{
    struct MinNode              node;
    BPTR                        seglist;
    const struct AROSDLModule  *desc;
    ULONG                       refs;
    BOOL                        nodelete;
    char                        path[];     /* the module's full DOS name */
};

/* dlopen(NULL) returns this handle; dlsym() then searches every module. */
extern const char __dl_self[];
#define __DL_SELF ((void *)__dl_self)

struct PosixCIntBase *__dl_owner(struct PosixCIntBase *PosixCBase);
/* The calling task's dlerror() record. */
struct __dl_error
{
    struct MinNode  node;
    struct Task    *task;
    BOOL            pending;
    char            text[256];
};

/* Sets the text the calling task's next dlerror() returns. what may be
   NULL; the text is "what: why" otherwise. */
void __dl_seterror(struct PosixCIntBase *PosixCBase, const char *what, const char *why);
/* The calling task's pending text, now no longer pending, or NULL. */
char *__dl_takeerror(struct PosixCIntBase *PosixCBase);
/* The module handle names in owner's list, or NULL. Call with dl_sem held. */
struct __dl_module *__dl_find(struct PosixCIntBase *owner, void *handle);
/* The start of the code in the first hunk: LoadSeg() puts a FullJumpVec to
   the aligned code there when a code section needs alignment, and the
   module's entry and mark are where that jump leads. */
const UBYTE *__dl_code(BPTR seglist);
/* Runs the module's Expunge, unloads it and frees the node. */
void __dl_unload(struct __dl_module *module);
/* The export named name in module, or NULL: a bisection of the table. */
void *__dl_lookup(const struct AROSDLModule *desc, const char *name);
/* Queues the release of every module on the program's exit list, once. */
void __dl_queue_release(struct PosixCIntBase *owner);

#endif /* ___DL_H */
