/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER <code@jkn.me>

    Desc: Tell locale that the preferences have been changed.
*/

#include <aros/isoascii.h>
#include <string.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <dos/var.h>
#include <utility/utility.h>
#include "locale_intern.h"

static const char *langlist[] =
{
    "english",
    "deutsch",
    "fran" ISOASCII_ccedil "ais",
    "espa" ISOASCII_ntilde "ol",
    "italiano",
    "portugu" ISOASCII_ecirc "s",
    "dansk",
    "nederlands",
    "norsk",
    "suomi",
    "svenska",
    NULL
};

/*****i***********************************************************************

    NAME */
#include <proto/locale.h>

        AROS_LH1(struct Locale *, LocalePrefsUpdate,

/*  SYNOPSIS */
        AROS_LHA(struct Locale  *, locale, A0),

/*  LOCATION */
        struct LocaleBase *, LocaleBase, 28, Locale)

/*  FUNCTION
        This internal function is called by the IPrefs program to
        notify the locale.library that the system preferences have
        been changed.

    INPUTS
        locale        -        A pointer to the new locale preferences.

    RESULT
        The address of the old locale preferences data. This should
        have CloseLocale() called upon it.

    NOTES
        You can safely call CloseLocale() on the new Locale as this
        function will ensure that the Locale will not be freed until
        PrefsUpdate() is called again.

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct IntLocale *old = NULL;
    char language[sizeof(((struct IntLocale *)0)->LanguageName)];
    STRPTR dotptr;
    ULONG index = 0;

    if(locale != NULL)
    {
        /* Lock the locale structures */
        ObtainSemaphore(&IntLB(LocaleBase)->lb_LocaleLock);

        /* Transfer the old current reference to the caller. */
        old = LocaleCurrentLoad(LocaleBase);
        
        /* Update the count and install the new locale. */
        if (IntL(locale) != IntLB(LocaleBase)->lb_DefaultLocale)
            IntL(locale)->il_Count++;
        IntL(locale)->il_Published = TRUE;
        LocaleCurrentStore(LocaleBase, IntL(locale));
        ReleaseSemaphore(&IntLB(LocaleBase)->lb_LocaleLock);

        /* Readers may use the published locale on another CPU. Strip the
           suffix only in this private copy, never in the shared object. */
        strncpy(language, locale->loc_LanguageName, sizeof(language) - 1);
        language[sizeof(language) - 1] = 0;
        if ((dotptr = strchr(language, '.')))
            *dotptr = 0;

        /* Update ENV:Language */
        SetVar("Language", language, -1, LV_VAR | GVF_GLOBAL_ONLY);

        /* Update UtilityBase->ub_Language */
        while (langlist[index] != NULL)
        {
            if (strcmp(language, langlist[index++]) == 0)
            {
                /* Check for american-english */
                if (index == 1 && locale->loc_MeasuringSystem == MS_AMERICAN)
                    --index;

                UtilityBase->ub_Language = index+1;
                break;
            }
        }

    }
    return (struct Locale *)old;
        
    AROS_LIBFUNC_EXIT
}
