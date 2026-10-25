/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: internals of dlopen(), dlsym(), dlclose() and dlerror()
*/

#include <aros/cpu.h>
#include <aros/debug.h>
#include <aros/symbolsets.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdlib.h>
#include <string.h>

#include "__dl.h"

/* stdc internals, as __optionallibs.c uses them: the program's exit list,
   which stdc's __callexitfuncs drains after main(). */
#include "__stdc_intbase.h"
#include "__exitfunc.h"

const char __dl_self[1];

struct PosixCIntBase *__dl_owner(struct PosixCIntBase *PosixCBase)
{
    return PosixCBase->fd_owner ? PosixCBase->fd_owner : PosixCBase;
}

/* The calling task's record, made on first use if make is TRUE. Call with
   dl_sem held. */
static struct __dl_error *__dl_taskerror(struct PosixCIntBase *owner, BOOL make)
{
    struct Task *me = FindTask(NULL);
    struct __dl_error *record;

    ForeachNode(&owner->dl_errors, record)
    {
        if (record->task == me)
            return record;
    }
    if (!make)
        return NULL;
    record = AllocVec(sizeof(*record), MEMF_ANY | MEMF_CLEAR);
    if (record != NULL)
    {
        record->task = me;
        ADDTAIL((struct List *)&owner->dl_errors, (struct Node *)record);
    }
    return record;
}

void __dl_seterror(struct PosixCIntBase *PosixCBase, const char *what, const char *why)
{
    struct PosixCIntBase *owner = __dl_owner(PosixCBase);
    struct __dl_error *record;

    ObtainSemaphore(&owner->dl_sem);
    record = __dl_taskerror(owner, TRUE);
    if (record != NULL)
    {
        char *buf = record->text;
        size_t size = sizeof(record->text), used = 0;

        buf[0] = '\0';
        if (what != NULL)
        {
            strncpy(buf, what, size - 1);
            buf[size - 1] = '\0';
            used = strlen(buf);
            if (used + 2 < size)
            {
                strcpy(buf + used, ": ");
                used += 2;
            }
        }
        strncpy(buf + used, why, size - 1 - used);
        buf[size - 1] = '\0';
        record->pending = TRUE;
    }
    ReleaseSemaphore(&owner->dl_sem);
}

char *__dl_takeerror(struct PosixCIntBase *PosixCBase)
{
    struct PosixCIntBase *owner = __dl_owner(PosixCBase);
    struct __dl_error *record;
    char *text = NULL;

    ObtainSemaphore(&owner->dl_sem);
    record = __dl_taskerror(owner, FALSE);
    if (record != NULL && record->pending)
    {
        /* The text stays where it is until this task's next failure. */
        record->pending = FALSE;
        text = record->text;
    }
    ReleaseSemaphore(&owner->dl_sem);
    return text;
}

const UBYTE *__dl_code(BPTR seglist)
{
    const UBYTE *code = (const UBYTE *)BADDR(seglist) + sizeof(BPTR);
    struct FullJumpVec jump;

    /* The hunk's data follows a ULONG and a BPTR: copy, never assume the
       vector's alignment. */
    memcpy(&jump, code, sizeof(jump));
#if defined(__aarch64__)
    if (jump.jmp == 0xd61f020058000050UL)       /* ldr x16,#8 ; br x16 */
        return (const UBYTE *)jump.vec;
#elif defined(__x86_64__)
    if (jump.movabsq == 0xBB49 && jump.jmp1 == 0xFF41 && jump.jmp2 == 0xE3)
        return (const UBYTE *)jump.vec;
#endif
    return code;
}

struct __dl_module *__dl_find(struct PosixCIntBase *owner, void *handle)
{
    struct __dl_module *module;

    ForeachNode(&owner->dl_modules, module)
    {
        if ((void *)module == handle)
            return module;
    }
    return NULL;
}

void __dl_unload(struct __dl_module *module)
{
    D(bug("[dl] unload %s\n", module->path));
    module->desc->dm_Expunge();
    UnLoadSeg(module->seglist);
    FreeVec(module);
}

void *__dl_lookup(const struct AROSDLModule *desc, const char *name)
{
    ULONG low = 0, high = desc->dm_ExportCount;

    while (low < high)
    {
        ULONG mid = low + (high - low) / 2;
        int order = strcmp(name, desc->dm_Exports[mid].de_Name);

        if (order == 0)
            return desc->dm_Exports[mid].de_Address;
        if (order < 0)
            high = mid;
        else
            low = mid + 1;
    }
    return NULL;
}

/* Unloads what the program left loaded, newest first. Runs from the exit
   list, after the program's own exit functions registered after the first
   dlopen(): a module holds the C library open from its Init, so the
   library's close would never come to do it. */
static void __dl_release_all(int status, void *arg)
{
    struct PosixCIntBase *owner = (struct PosixCIntBase *)arg;
    struct __dl_module *module;
    struct __dl_error *record;

    (void)status;
    ObtainSemaphore(&owner->dl_sem);
    while ((module = (struct __dl_module *)REMTAIL((struct List *)&owner->dl_modules)) != NULL)
        __dl_unload(module);
    while ((record = (struct __dl_error *)REMHEAD((struct List *)&owner->dl_errors)) != NULL)
        FreeVec(record);
    owner->dl_release = NULL;
    ReleaseSemaphore(&owner->dl_sem);
}

void __dl_queue_release(struct PosixCIntBase *owner)
{
    struct StdCIntBase *StdCBase =
        (struct StdCIntBase *)__aros_getbase_StdCBase();
    struct AtExitNode *aen;

    /* Without a program C library base there is no exit list: the modules
       then stay until their last dlclose(). */
    if (owner->dl_release != NULL || StdCBase == NULL)
        return;
    aen = malloc(sizeof(*aen));
    if (aen == NULL)
        return;
    aen->node.ln_Type = AEN_ON;
    aen->func.on.fn = __dl_release_all;
    aen->func.on.arg = owner;
    ADDHEAD((struct List *)&StdCBase->atexit_list, (struct Node *)aen);
    owner->dl_release = aen;
}

static int __init_dl(struct PosixCIntBase *PosixCBase)
{
    NEWLIST((struct List *)&PosixCBase->dl_modules);
    NEWLIST((struct List *)&PosixCBase->dl_errors);
    InitSemaphore(&PosixCBase->dl_sem);
    PosixCBase->dl_release = NULL;
    return 1;
}

ADD2OPENLIB(__init_dl, 3);
