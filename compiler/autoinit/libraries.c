/*
    Copyright (C) 1995-2019, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>

    Desc: autoinit library - automatic library opening/closing handling
*/

#include <aros/debug.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include <aros/symbolsets.h>
#include <aros/autoinit.h>
#include <intuition/intuition.h>
#include <dos/dosextens.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <stdlib.h>

DEFINESET(LIBS);

AROS_MAKE_ASM_SYM(int, dummy, __includelibrarieshandling, 0);
AROS_EXPORT_ASM_SYM(__includelibrarieshandling);
                             
int _set_open_libraries_list(const void * const list[], struct ExecBase *SysBase)
{
    int pos;
    struct libraryset *set;

    D(bug("[Autoinit] Opening libraries...\n"));

    ForeachElementInSet(list, 1, pos, set)
    {
        LONG version = *set->versionptr;
        BOOL do_not_fail = 0;
        
        if (version < 0)
        {
            version = -(version + 1);
            do_not_fail = 1;
        }

        D(bug("[Autoinit] %s version %d... ", set->name, version));
        *set->baseptr = OpenLibrary(set->name, version);
        D(bug("0x%p\n", *set->baseptr));

        if (!do_not_fail && *set->baseptr == NULL)
        {
            __showerror("Could not open version %ld or higher of library \"%s\".", version, set->name);

            return 0;
        }
    }

    D(bug("[Autoinit] Done\n"));
    return 1;
}

void _set_close_libraries_list(const void * const list[], struct ExecBase *SysBase)
{
    int pos;
    struct libraryset *set;
    
    ForeachElementInSet(list, 1, pos, set)
    {
        if (*set->baseptr)
        {
            CloseLibrary(*set->baseptr);
            *set->baseptr = NULL;
        }
    }
}

DEFINESET(RELLIBS);

AROS_MAKE_ASM_SYM(int, dummyrel, __includerellibrarieshandling, 0);
AROS_EXPORT_ASM_SYM(__includerellibrarieshandling);

/* Every slot in the list belongs to this base once the call starts: a
   duplicated base arrives with its root's pointers copied in, which it does
   not own, so all slots are cleared before anything is opened. On failure
   the libraries opened by this call are closed again, in reverse order, and
   their slots cleared, so the base holds nothing and a later
   _set_close_rellibraries_list (libinit's rollback, or a failed OpenLib)
   closes nothing twice. */
int _set_open_rellibraries_list(APTR base, const void * const list[], struct ExecBase *SysBase)
{
    int pos;
    struct rellibraryset *set;

    D(bug("[Autoinit] Opening relative libraries for %s @ 0x%p...\n", ((struct Node *)base)->ln_Name, base));

    ForeachElementInSet(list, 1, pos, set)
        *(void **)((char *)base + *set->baseoffsetptr) = NULL;

    ForeachElementInSet(list, 1, pos, set)
    {
        LONG version = *set->versionptr;
        BOOL do_not_fail = 0;
        void **baseptr = (void **)((char *)base + *set->baseoffsetptr);

        if (version < 0)
        {
            version = -(version + 1);
            do_not_fail = 1;
        }

        D(bug("[Autoinit] Offset %d, %s version %d... ", *set->baseoffsetptr, set->name, version));
        *baseptr = OpenLibrary(set->name, version);
        D(bug("0x%p\n", *baseptr));

        if (!do_not_fail && *baseptr == NULL)
        {
            int failed = pos;

            __showerror("Could not open version %ld or higher of library \"%s\".", version, set->name);

            /* Unwind the prefix this call opened, last first. */
            for (pos = failed - 1; pos >= 1; pos--)
            {
                struct rellibraryset *opened = (struct rellibraryset *)list[pos];
                struct Library **openedptr = (struct Library **)((char *)base + *opened->baseoffsetptr);

                if (*openedptr)
                {
                    CloseLibrary(*openedptr);
                    *openedptr = NULL;
                }
            }
            return 0;
        }
    }

    D(bug("[Autoinit] %s Done\n", ((struct Node *)base)->ln_Name));
    return 1;
}

void _set_close_rellibraries_list(APTR base, const void * const list[], struct ExecBase *SysBase)
{
    int pos;
    struct rellibraryset *set;
    struct Library **baseptr;
    
    ForeachElementInSet(list, 1, pos, set)
    {
        baseptr = (struct Library **)((char *)base + *set->baseoffsetptr);
        
        if (*baseptr)
        {
            CloseLibrary(*baseptr);
            *baseptr = NULL;
        }
    }
}
