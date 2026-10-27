/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 function dlopen().
*/

#include <aros/asmcall.h>
#include <aros/debug.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

#include "__dl.h"
#include "__upath.h"

/* Writes the full DOS name of the first candidate that exists into out
   and returns TRUE. A name with a ':' or '/' is used as given; any other is
   looked for in PROGDIR: and then LIBS:dl/, first as given and then without
   its trailing version numbers ("libvulkan.so.1" -> "libvulkan.so"). */
static BOOL __dl_resolve(const char *file, char *out, LONG size)
{
    char base[256], candidate[300];
    static const char *const dirs[] = { "PROGDIR:", "LIBS:dl/" };
    int pass, d;

    if (strchr(file, ':') != NULL || strchr(file, '/') != NULL)
    {
        BPTR lock = Lock((CONST_STRPTR)file, SHARED_LOCK);
        BOOL named = FALSE;

        if (lock != BNULL)
        {
            named = NameFromLock(lock, (STRPTR)out, size);
            UnLock(lock);
        }
        return named;
    }

    if (strlen(file) >= sizeof(base))
        return FALSE;
    strcpy(base, file);
    for (pass = 0; pass < 2; pass++)
    {
        if (pass == 1)
        {
            /* Strip ".N" groups that follow ".so". */
            char *so = strstr(base, ".so.");
            if (so == NULL)
                break;
            so[3] = '\0';
        }
        for (d = 0; d < 2; d++)
        {
            BPTR lock;

            snprintf(candidate, sizeof(candidate), "%s%s", dirs[d], base);
            lock = Lock((CONST_STRPTR)candidate, SHARED_LOCK);
            if (lock != BNULL)
            {
                BOOL named = NameFromLock(lock, (STRPTR)out, size);
                UnLock(lock);
                if (named)
                    return TRUE;
            }
        }
    }
    return FALSE;
}

/*****************************************************************************

    NAME */
#include <dlfcn.h>

        void *dlopen(

/*  SYNOPSIS */
        const char *file,
        int mode)

/*  FUNCTION
        Loads a module linked with dlmodule-link and returns a handle for
        dlsym() and dlclose(). A module already loaded by this process is not
        loaded again: its reference count grows and the same handle returns.

    INPUTS
        file - the module: a name with a ':' or '/' is a path; any other name
               is looked for in PROGDIR: and then LIBS:dl/, as given and
               without trailing version numbers after ".so". NULL returns a
               handle with which dlsym() searches every loaded module.
        mode - RTLD_LAZY or RTLD_NOW, with RTLD_LOCAL (the default),
               RTLD_NOLOAD and RTLD_NODELETE. RTLD_GLOBAL is refused.

    RESULT
        The handle, or NULL with the reason in dlerror().

    NOTES
        The first dlopen() of a module runs its initialisation in the
        calling task: the libraries it opens automatically and its
        constructors; the C library bases it opens are those of that task,
        so a program should load modules from its main task. Symbols are
        bound at load time, so RTLD_LAZY and RTLD_NOW are the same. A module
        never binds to another module's symbols, so RTLD_GLOBAL, which would
        promise that, is refused. Handles are counted: a module unloads at
        its last dlclose(), and pointers into it are then invalid. Modules
        still loaded when the program ends are unloaded after its exit
        functions.

    EXAMPLE

    BUGS

    SEE ALSO
        dlsym(), dlclose(), dlerror()

    INTERNALS
        A module is a LoadSeg() object whose first hunk starts with a branch
        over AROS_DLMODULE_MAGIC; see <aros/dlmodule.h>.

******************************************************************************/
{
    struct PosixCIntBase *PosixCBase =
        __cwd_runtime_base();
    struct PosixCIntBase *owner = __dl_owner(PosixCBase);
    struct __dl_module *module;
    const struct AROSDLModule *desc;
    const ULONG *mark;
    char path[512];
    const char *afile;
    BPTR seglist;

    if (mode & RTLD_GLOBAL)
    {
        /* No module binds to another module's symbols: refuse rather than
           pretend. */
        __dl_seterror(PosixCBase, NULL, "RTLD_GLOBAL is not supported");
        return NULL;
    }
    if (mode & ~(RTLD_LAZY | RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE))
    {
        __dl_seterror(PosixCBase, NULL, "invalid mode");
        return NULL;
    }
    if (file == NULL)
        return __DL_SELF;

    afile = __path_u2a(file);
    if (afile == NULL || !__dl_resolve(afile, path, sizeof(path)))
    {
        __dl_seterror(PosixCBase, file, "cannot find the module");
        return NULL;
    }

    ObtainSemaphore(&owner->dl_sem);
    ForeachNode(&owner->dl_modules, module)
    {
        if (strcmp(module->path, path) == 0)
        {
            module->refs++;
            if (mode & RTLD_NODELETE)
                module->nodelete = TRUE;
            ReleaseSemaphore(&owner->dl_sem);
            return module;
        }
    }
    if (mode & RTLD_NOLOAD)
    {
        ReleaseSemaphore(&owner->dl_sem);
        __dl_seterror(PosixCBase, file, "not loaded");
        return NULL;
    }

    seglist = LoadSeg((CONST_STRPTR)path);
    if (seglist == BNULL)
    {
        char why[80];

        Fault(IoErr(), NULL, (STRPTR)why, sizeof(why));
        ReleaseSemaphore(&owner->dl_sem);
        __dl_seterror(PosixCBase, path, why);
        return NULL;
    }

    /* Never call into an ordinary program: check the mark first. */
    mark = (const ULONG *)(__dl_code(seglist) + AROS_DLMODULE_MARK_OFFSET);
    if (mark[0] != AROS_DLMODULE_MAGIC || mark[1] != AROS_DLMODULE_VERSION)
    {
        UnLoadSeg(seglist);
        ReleaseSemaphore(&owner->dl_sem);
        __dl_seterror(PosixCBase, path, "not a dlopen module");
        return NULL;
    }
    desc = (const struct AROSDLModule *)AROS_UFC3(IPTR,
        (APTR)((UBYTE *)BADDR(seglist) + sizeof(BPTR)),
        AROS_UFCA(STRPTR, NULL, A0),
        AROS_UFCA(ULONG, AROS_DLMODULE_MAGIC, D0),
        AROS_UFCA(struct ExecBase *, SysBase, A6));
    if (desc == NULL || (IPTR)desc == RETURN_FAIL
        || desc->dm_Magic != AROS_DLMODULE_MAGIC || desc->dm_Version != AROS_DLMODULE_VERSION)
    {
        UnLoadSeg(seglist);
        ReleaseSemaphore(&owner->dl_sem);
        __dl_seterror(PosixCBase, path, "the module has no valid descriptor");
        return NULL;
    }

    module = AllocVec(sizeof(*module) + strlen(path) + 1, MEMF_ANY | MEMF_CLEAR);
    if (module == NULL)
    {
        UnLoadSeg(seglist);
        ReleaseSemaphore(&owner->dl_sem);
        __dl_seterror(PosixCBase, path, "out of memory");
        return NULL;
    }
    if (!desc->dm_Init())
    {
        FreeVec(module);
        UnLoadSeg(seglist);
        ReleaseSemaphore(&owner->dl_sem);
        __dl_seterror(PosixCBase, path, "the module's initialisation failed");
        return NULL;
    }
    module->seglist = seglist;
    module->desc = desc;
    module->refs = 1;
    module->nodelete = (mode & RTLD_NODELETE) ? TRUE : FALSE;
    strcpy(module->path, path);
    ADDTAIL((struct List *)&owner->dl_modules, (struct Node *)module);
    __dl_queue_release(owner);
    ReleaseSemaphore(&owner->dl_sem);

    D(bug("[dl] loaded %s, %u exports\n", path, (unsigned)desc->dm_ExportCount));
    return module;
}
