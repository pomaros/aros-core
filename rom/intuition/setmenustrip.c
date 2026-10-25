/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2013, The AROS Development Team. All rights reserved.
    Copyright (C) 2001-2003, The MorphOS Development Team. All Rights Reserved.
*/

#include "intuition_intern.h"
#include "menus.h"
#include "menutask.h"
#include "menuscale.h"

#define HASSUBITEM 0x8000

void Characterize(struct Menu *menu);

/*****************************************************************************

    NAME */
#include <proto/intuition.h>
#include <proto/exec.h>
#include <intuition/intuition.h>

        AROS_LH2(BOOL, SetMenuStrip,

/*  SYNOPSIS */
        AROS_LHA(struct Window *, window, A0),
        AROS_LHA(struct Menu   *, menu  , A1),

/*  LOCATION */
        struct IntuitionBase *, IntuitionBase, 44, Intuition)

/*  FUNCTION
        This function adds a MenuStrip to the Window, which can be invoked
        by the user after this call by pressing the right mouse button.
        Menus with no MenuItems will not be attached.

    INPUTS
        window - The window to add the MenuStrip to
        menu   - The menu to be added to the window above.

    RESULT
        TRUE if all menus have at least one menuitem.

    NOTES
        This function calculates internal values and is therfore the
        official way to add a new MenuStrip to Window.
        Always do a ClearMenuStrip() before closing the Window or adding
        another MenuStrip to the Window.

    EXAMPLE

    BUGS

    SEE ALSO
        ResetMenuStrip(), ClearMenuStrip()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    SANITY_CHECKR(window,FALSE)

    const struct ScreenRenderContext *context = CaptureMenuScaleContext(window, IntuitionBase);
    return SetMenuStripContext(window, menu, context, !IW(window)->ScaleAware);

    AROS_LIBFUNC_EXIT
} /* SetMenuStrip */

/* Mark items that has subitems. This is necessary for the input handler
   code. It's not possible to check item->SubItem within it as we save
   the layer coordinates there. */
void Characterize(struct Menu *menu)
{
    while(menu != NULL)
    {
        struct MenuItem *item;

        item = menu->FirstItem;

        while(item != NULL)
        {
            if(item->SubItem != NULL)
                item->Flags |= HASSUBITEM;

            item = item->NextItem;
        }

        menu = menu->NextMenu;
    }
}
