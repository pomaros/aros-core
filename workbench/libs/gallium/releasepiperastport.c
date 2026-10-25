/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/

#include <graphics/rastport.h>
#include <proto/alib.h>
#include <aros/debug.h>

#include "gallium_intern.h"

/*****************************************************************************

    NAME */

      AROS_LH2(void, ReleasePipeRastPort,

/*  SYNOPSIS */
      AROS_LHA(PipeHandle_t, pipe, A0),
      AROS_LHA(struct RastPort *, destRP, A1),

/*  LOCATION */
      struct Library *, GalliumBase, 10, Gallium)

/*  FUNCTION
        Tells the driver that DESTRP, a rastport BltPipeResourceRastPort
        presented to, receives no more frames: its window is closing or its
        drawable goes. A driver that showed the window's frames on a display
        plane of their own (the DCP layer on Apple silicon) takes the plane
        down, so the window's own bitmap shows again; a driver that copied
        the frames into the bitmap has nothing to do.

    INPUTS
        pipe - the pipe the frames were presented through.
        destRP - the rastport they were presented to; it must still be
                 valid (call this before closing the window).

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct pHidd_Gallium_ReleaseRastPort msg = {
        .mID      = ((struct GalliumBase *)GalliumBase)->galliumMId_ReleaseRastPort,
        .rastport = destRP,
    };

    if (pipe && destRP)
        OOP_DoMethod((OOP_Object *)pipe, (OOP_Msg)&msg);

    AROS_LIBFUNC_EXIT
}
