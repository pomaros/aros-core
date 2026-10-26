/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Drawables apart from contexts: what an EGL surface owns, and the call
    that binds a context to a draw and a read drawable.
*/

#include <aros/debug.h>

#include <proto/exec.h>
#include <proto/utility.h>

#include <string.h>

#include "mesa3dgl_types.h"
#include "mesa3dgl_support.h"
#include "mesa3dgl_gallium.h"

/*****************************************************************************

    NAME */

      GLADrawable glACreateDrawable(

/*  SYNOPSIS */
      GLAContext ctx,
      struct TagItem * tagList)

/*  FUNCTION
        Creates a drawable apart from any context: a window, rastport or
        offscreen target with the framebuffer that keeps its pixels, made
        for CTX's pipe screen and visual. Any context on that screen (CTX,
        one made with GLA_ShareContext or GLA_ShareScreen of it) can draw
        to it and read from it through glAMakeCurrentDrawables, and the
        pixels stay with the drawable whichever context last used it.

    INPUTS
        ctx - the context whose screen and visual the drawable takes.
        tagList - the target, as for glACreateContext: GLA_Window,
                  GLA_Screen or GLA_RastPort, else GLA_Width and GLA_Height
                  for an offscreen bitmap; GLA_Left, GLA_Top, GLA_Right and
                  GLA_Bottom.

    RESULT
        The drawable, or NULL when its target could not be made or its depth
        does not fit the visual.

*****************************************************************************/
{
    struct mesa3dgl_context *_ctx = (struct mesa3dgl_context *)ctx;
    struct mesa3dgl_drawable *d;

    if (!_ctx)
        return NULL;
    if (!(d = AllocVec(sizeof(*d), MEMF_PUBLIC | MEMF_CLEAR)))
        return NULL;
    if (!MESA3DGLPrepareTarget(d, tagList))
    {
        FreeVec(d);
        return NULL;
    }
    if (!MESA3DGLVisualAccepts(&_ctx->stvis, d->BitsPerPixel) ||
        !(d->framebuffer = MESA3DGLNewFrameBuffer(_ctx, &_ctx->stvis)))
    {
        MESA3DGLFreeTarget(d);
        FreeVec(d);
        return NULL;
    }
    d->driver = _ctx->driver;
    d->shared = _ctx->shared;
    __atomic_add_fetch(&d->shared->refs, 1, __ATOMIC_ACQ_REL);
    MESA3DGLCheckAndUpdateBufferSize(d);
    D(bug("[MESA3DGL] %s: drawable %p %lux%lu\n", __func__, d,
        (unsigned long)d->framebuffer->width, (unsigned long)d->framebuffer->height));
    return (GLADrawable)d;
}

/*****************************************************************************

    NAME */

      void glADestroyDrawable(

/*  SYNOPSIS */
      GLADrawable drawable)

/*  FUNCTION
        Frees a drawable glACreateDrawable made, its pixels and its target.
        It must not be bound to any context.

*****************************************************************************/
{
    struct mesa3dgl_drawable *d = (struct mesa3dgl_drawable *)drawable;

    if (!d)
        return;
    st_api_destroy_drawable(&d->framebuffer->base);
    MESA3DGLFreeFrameBuffer(d->framebuffer);
    MESA3DGLFreeTarget(d);
    if (d->shared && __atomic_sub_fetch(&d->shared->refs, 1, __ATOMIC_ACQ_REL) == 0)
    {
        MESA3DGLFreeStManager(d->shared->driver, d->shared->stmanager);
        FreeVec(d->shared);
    }
    FreeVec(d);
}

/*****************************************************************************

    NAME */

      BOOL glAMakeCurrentDrawables(

/*  SYNOPSIS */
      GLAContext ctx,
      GLADrawable draw,
      GLADrawable read)

/*  FUNCTION
        Makes CTX the calling task's current context, drawing to DRAW and
        reading (glReadPixels, glCopyTex*) from READ. Both NULL: current
        without a drawable, for rendering to framebuffer objects only. CTX
        NULL: the task has no current context.

    RESULT
        TRUE when bound. FALSE when the state tracker refused the binding;
        the task's previous binding then stands.

*****************************************************************************/
{
    struct mesa3dgl_context *_ctx = (struct mesa3dgl_context *)ctx;
    struct mesa3dgl_drawable *d = (struct mesa3dgl_drawable *)draw;
    struct mesa3dgl_drawable *r = (struct mesa3dgl_drawable *)read;

    if (!_ctx)
    {
        st_api_make_current(NULL, NULL, NULL);
        return TRUE;
    }
    if ((d == NULL) != (r == NULL))
        return FALSE;
    if (d)
    {
        struct mesa3dgl_drawable *targets[2] = { d, r };
        for (unsigned i = 0; i < 2; i++)
        {
            const struct mesa3dgl_framebuffer *fb = targets[i]->framebuffer;
            if (!targets[i]->shared || targets[i]->shared != _ctx->shared || !fb ||
                fb->stvis.buffer_mask != _ctx->stvis.buffer_mask ||
                fb->stvis.color_format != _ctx->stvis.color_format ||
                fb->stvis.depth_stencil_format != _ctx->stvis.depth_stencil_format ||
                fb->stvis.accum_format != _ctx->stvis.accum_format ||
                fb->stvis.samples != _ctx->stvis.samples)
                return FALSE;
        }
    }
    /* A window may have been resized since the drawable last drew. */
    if (d)
        MESA3DGLCheckAndUpdateBufferSize(d);
    if (r && r != d)
        MESA3DGLCheckAndUpdateBufferSize(r);
    if (st_api_get_current() == _ctx->st && _ctx->draw == d && _ctx->read == r)
        return TRUE;
    if (!st_api_make_current(_ctx->st, d ? &d->framebuffer->base : NULL,
            r ? &r->framebuffer->base : NULL))
        return FALSE;
    _ctx->draw = d;
    _ctx->read = r;
    return TRUE;
}
