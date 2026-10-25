/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Drawables apart from contexts (gl.library's glACreateDrawable family):
    the host's GL binds a context to its own target only, so these refuse.
*/

#include <aros/debug.h>

#include <GL/gla.h>

/*****************************************************************************

    NAME */

      GLADrawable glACreateDrawable(

/*  SYNOPSIS */
      GLAContext ctx,
      struct TagItem * tagList)

/*  FUNCTION
        Not provided by the host's GL: always NULL.

*****************************************************************************/
{
    (void)ctx;
    (void)tagList;
    return NULL;
}

/*****************************************************************************

    NAME */

      void glADestroyDrawable(

/*  SYNOPSIS */
      GLADrawable drawable)

/*  FUNCTION
        Nothing to free: glACreateDrawable makes no drawable here.

*****************************************************************************/
{
    (void)drawable;
}

/*****************************************************************************

    NAME */

      BOOL glAMakeCurrentDrawables(

/*  SYNOPSIS */
      GLAContext ctx,
      GLADrawable draw,
      GLADrawable read)

/*  FUNCTION
        Not provided by the host's GL: always FALSE, the binding unchanged.

*****************************************************************************/
{
    (void)ctx;
    (void)draw;
    (void)read;
    return FALSE;
}
