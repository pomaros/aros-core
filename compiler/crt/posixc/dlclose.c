/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 function dlclose().
*/

#include <exec/lists.h>
#include <proto/exec.h>

#include "__dl.h"

/*****************************************************************************

    NAME */
#include <dlfcn.h>

        int dlclose(

/*  SYNOPSIS */
        void *handle)

/*  FUNCTION
        Releases a reference that dlopen() returned. The last one runs the
        module's destructors, closes the libraries it opened and unloads it,
        unless it was opened with RTLD_NODELETE.

    INPUTS
        handle - a handle from dlopen()

    RESULT
        0, or -1 with the reason in dlerror().

    NOTES
        The handle of dlopen(NULL) can be closed any number of times.

    EXAMPLE

    BUGS

    SEE ALSO
        dlopen(), dlsym(), dlerror()

    INTERNALS

******************************************************************************/
{
    struct PosixCIntBase *PosixCBase =
        __cwd_runtime_base();
    struct PosixCIntBase *owner = __dl_owner(PosixCBase);
    struct __dl_module *module;

    if (handle == __DL_SELF)
        return 0;

    ObtainSemaphore(&owner->dl_sem);
    module = __dl_find(owner, handle);
    if (module == NULL)
    {
        ReleaseSemaphore(&owner->dl_sem);
        __dl_seterror(PosixCBase, NULL, "invalid handle");
        return -1;
    }
    if (--module->refs == 0 && !module->nodelete)
    {
        REMOVE((struct Node *)module);
        __dl_unload(module);
    }
    ReleaseSemaphore(&owner->dl_sem);
    return 0;
}
