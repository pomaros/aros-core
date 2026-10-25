/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 function dlsym().
*/

#include <proto/exec.h>

#include "__dl.h"

/*****************************************************************************

    NAME */
#include <dlfcn.h>

        void *dlsym(

/*  SYNOPSIS */
        void *__restrict handle,
        const char *__restrict name)

/*  FUNCTION
        Returns the address of the symbol name that the module handle
        exports.

    INPUTS
        handle - a handle from dlopen(); RTLD_DEFAULT, RTLD_NEXT or the
                 handle of dlopen(NULL) search every loaded module, oldest
                 first.
        name   - the symbol's name, as the linker sees it.

    RESULT
        The address, or NULL with the reason in dlerror().

    NOTES
        A module exports what dlmodule-link put in its table: by default
        the global symbols of its own objects.

    EXAMPLE

    BUGS
        The program itself exports nothing: RTLD_DEFAULT finds only the
        symbols of loaded modules.

    SEE ALSO
        dlopen(), dlclose(), dlerror()

    INTERNALS

******************************************************************************/
{
    struct PosixCIntBase *PosixCBase =
        (struct PosixCIntBase *)__aros_getbase_PosixCBase();
    struct PosixCIntBase *owner = __dl_owner(PosixCBase);
    struct __dl_module *module;
    void *address = NULL;

    if (name == NULL)
    {
        __dl_seterror(PosixCBase, NULL, "no symbol name");
        return NULL;
    }

    ObtainSemaphore(&owner->dl_sem);
    if (handle == RTLD_DEFAULT || handle == RTLD_NEXT || handle == __DL_SELF)
    {
        ForeachNode(&owner->dl_modules, module)
        {
            address = __dl_lookup(module->desc, name);
            if (address != NULL)
                break;
        }
    }
    else
    {
        module = __dl_find(owner, handle);
        if (module == NULL)
        {
            ReleaseSemaphore(&owner->dl_sem);
            __dl_seterror(PosixCBase, NULL, "invalid handle");
            return NULL;
        }
        address = __dl_lookup(module->desc, name);
    }
    ReleaseSemaphore(&owner->dl_sem);

    if (address == NULL)
        __dl_seterror(PosixCBase, name, "symbol not found");
    return address;
}
