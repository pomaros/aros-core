/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2007, The AROS Development Team. All rights reserved.

    Desc:
*/
#include <proto/exec.h>
#include <exec/memory.h>
#include <proto/intuition.h>
#include "gadtools_intern.h"

/*********************************************************************

    NAME */
#include <proto/gadtools.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>

        AROS_LH2(APTR, GetVisualInfoA,

/*  SYNOPSIS */
        AROS_LHA(struct Screen *, screen, A0),
        AROS_LHA(struct TagItem *, tagList, A1),

/*  LOCATION */
        struct Library *, GadToolsBase, 21, GadTools)

/*  FUNCTION
        GetVisualInfoA() creates a visual info structure, which is needed
        by several gadtools functions. When you're done using it, you have
        to call FreeVisualInfo().

    INPUTS
        screen -  pointer to the screen to create a visual info structure for
                  (may be NULL)
        taglist - GTVI_RenderContext supplies one immutable context already
                  sampled by the caller; GTVI_Logical selects its logical view.
                  With no context the traditional logical contract is used.
                  FreeVisualInfo never releases borrowed context metadata.

    RESULT
        A (private) visual info structure. NULL indicates an error.

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO
        FreeVisualInfo()

    INTERNALS

    HISTORY

***************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct VisualInfo *vi;

    if (screen == NULL)
        return NULL;

    vi = AllocVec(sizeof(struct VisualInfo), MEMF_ANY);
    if (!vi)
        return NULL;

    vi->vi_screen = screen;
    vi->vi_dri = NULL;
    vi->vi_context = (const struct ScreenRenderContext *)GetTagData(GTVI_RenderContext, 0, tagList);
    vi->vi_logical = GetTagData(GTVI_Logical, FALSE, tagList) != 0;
    if (vi->vi_context)
        vi->vi_dri = vi->vi_logical ? vi->vi_context->src_LogicalDrawInfo :
            vi->vi_context->src_PhysicalDrawInfo;
    else
        vi->vi_dri = GetScreenDrawInfo(screen);
    if (!vi->vi_dri || (vi->vi_context && vi->vi_dri->dri_Screen != screen))
    { FreeVec(vi); return NULL; }

    return vi;

    AROS_LIBFUNC_EXIT
    
} /* GetVisualInfoA */
