/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    GNU/BSD function dladdr().
*/

#include <proto/exec.h>

#include <stddef.h>

#include "__dl.h"

/* A LoadSeg() hunk: its size, then the BPTR the seglist points at. A hunk
   carved from a module arena records size 0; the arena's container hunk at
   the end of the chain then covers the whole module. */
struct __dl_hunk
{
    ULONG size;
    BPTR  next;
} __attribute__((packed));

static BOOL __dl_contains(BPTR seglist, const void *addr)
{
    BPTR seg = seglist;

    /* The link follows a ULONG, so it is read through the packed hunk,
       as the loader does, never as an aligned BPTR. */
    while (seg != BNULL)
    {
        const struct __dl_hunk *hunk = (const struct __dl_hunk *)
            ((const UBYTE *)BADDR(seg) - offsetof(struct __dl_hunk, next));

        if (hunk->size != 0 && (const UBYTE *)addr >= (const UBYTE *)hunk
            && (const UBYTE *)addr < (const UBYTE *)hunk + hunk->size)
            return TRUE;
        seg = hunk->next;
    }
    return FALSE;
}

/*****************************************************************************

    NAME */
#include <dlfcn.h>

        int dladdr(

/*  SYNOPSIS */
        const void *addr,
        Dl_info *info)

/*  FUNCTION
        Finds the loaded module that contains addr and the nearest symbol it
        exports at or below addr.

    INPUTS
        addr - an address
        info - filled with the module's name and base, and the symbol's name
               and address (NULL when no export lies at or below addr)

    RESULT
        Non-zero when a module contains addr, 0 otherwise.

    NOTES
        Only modules loaded by dlopen() are known; an address in the program
        itself returns 0. The names stay valid while the module is loaded.

    EXAMPLE

    BUGS

    SEE ALSO
        dlopen(), dlsym()

    INTERNALS

******************************************************************************/
{
    struct PosixCIntBase *PosixCBase =
        __cwd_runtime_base();
    struct PosixCIntBase *owner = __dl_owner(PosixCBase);
    struct __dl_module *module;
    int found = 0;

    if (info == NULL)
        return 0;

    ObtainSemaphore(&owner->dl_sem);
    ForeachNode(&owner->dl_modules, module)
    {
        const struct AROSDLModule *desc = module->desc;
        ULONG i;

        if (!__dl_contains(module->seglist, addr))
            continue;
        info->dli_fname = module->path;
        info->dli_fbase = (void *)__dl_code(module->seglist);
        info->dli_sname = NULL;
        info->dli_saddr = NULL;
        for (i = 0; i < desc->dm_ExportCount; i++)
        {
            void *at = desc->dm_Exports[i].de_Address;

            if ((const UBYTE *)at <= (const UBYTE *)addr
                && (info->dli_saddr == NULL || (UBYTE *)at > (UBYTE *)info->dli_saddr))
            {
                info->dli_sname = desc->dm_Exports[i].de_Name;
                info->dli_saddr = at;
            }
        }
        found = 1;
        break;
    }
    ReleaseSemaphore(&owner->dl_sem);
    return found;
}
