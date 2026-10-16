/*
    Copyright (C) 1995-2011, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER <code@jkn.me>

    Desc: CloseLocale() - Close a locale structure.
*/
#include <exec/types.h>
#include <proto/exec.h>
#include "locale_intern.h"

/*****************************************************************************

    NAME */
#include <proto/locale.h>

        AROS_LH1(void, CloseLocale,

/*  SYNOPSIS */
        AROS_LHA(struct Locale *, locale, A0),

/*  LOCATION */
        struct LocaleBase *, LocaleBase, 7, Locale)

/*  FUNCTION
        Finish accessing a Locale.

    INPUTS
        locale  -   An opened locale. Note that NULL is a valid
                    parameter here, and will simply be ignored.

    RESULT
        The locale is released back to the system.

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO
        OpenLocale()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    /* Best make sure we actually have something freeable. */
    if (locale
        && (locale != (struct Locale *)IntLB(LocaleBase)->lb_DefaultLocale))
    {
        /* Make sure we don't have any race conditions */
        ObtainSemaphore(&IntLB(LocaleBase)->lb_LocaleLock);
        if (--IntL(locale)->il_Count == 0)
        {
            /* Published objects back lock-free readers and escaped strings. */
            if (!IntL(locale)->il_Published)
            {
                /* Close old .language, if any */
                if (IntL(locale)->il_CurrentLanguage)
                    CloseLibrary(IntL(locale)->
                        il_CurrentLanguage);

                /* Close old dos.catalog */
                CloseCatalog(IntL(locale)->
                    il_DosCatalog);

                FreeMem(locale, sizeof(struct IntLocale));
            }
        }
        ReleaseSemaphore(&IntLB(LocaleBase)->lb_LocaleLock);
    }

    AROS_LIBFUNC_EXIT
}
