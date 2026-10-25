/*
    Copyright (C) 2009-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/

#include <aros/debug.h>
#include <limits.h>

#include "util/os_misc.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/u_inlines.h"

#include <gallium/gallium.h>

#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/gallium.h>
#include <proto/oop.h>

#include "mesa3dgl_support.h"
#include "mesa3dgl_gallium.h"

/* GalliumCoreAPI table provider (generated gca_table.c). Passed to the
 * pipe hidd via CPS_GalliumCoreAPI so a runtime-bound pipe driver can
 * call mesa3dgl's one Mesa compiler core. Builds without a table pass 0;
 * table-less drivers (softpipe) never look. */
#ifdef MESA3DGL_HAVE_COREAPI
extern const void *gallium_core_get_api(void);
#else
static inline const void *gallium_core_get_api(void) { return (const void *)0; }
#endif

/*****************************************************************************

    NAME */

      GLAContext glACreateContext(

/*  SYNOPSIS */
      struct TagItem *tagList)

/*  FUNCTION

        Crates a GL rendering context that can be later used in subsequent
        calls.
 
    INPUTS

        tagList - a pointer to tags to be used during creation.
 
    TAGS

        GLA_Left   - specifies the left rendering offset on the rastport.
                     Typically equals to window->BorderLeft.

        GLA_Top    - specifies the top rendering offset on the rastport.
                     Typically equals to window->BorderTop.

        GLA_Right  - specifies the right rendering offset on the rastport.
                     Typically equals to window->BorderRight.

        GLA_Bottom - specifies the bottom rendering offset on the rastport.
                     Typically equals to window->BorderBottom.
    
        GLA_Width  - specifies the width of the rendering area.
                     GLA_Width + GLA_Left + GLA_Right should equal the width of
                     the rastport. The GLA_Width is interchangable at cration
                     time with GLA_Right. Later durring window resizing, width
                     is calculated from scalled left, righ and window width.

        GLA_Height - specifies the height of the rendering area.
                     GLA_Height + GLA_Top + GLA_Bottom should equal the height
                     of the rastport. The GLA_Height is interchangable at
                     cration time with GLA_Bottom. Later durring window resizing
                     , height is calculated from scalled top, bottom and window
                     height.

        GLA_Screen - pointer to Screen onto which scene is to be rendered. When
                     selecting RastPort has lower priority than GLA_Window.

        GLA_Window - pointer to Window onto which scene is to be rendered. Must
                     be provided.

        GLA_RastPort - ignored. Use GLA_Window.

        Without GLA_Window, GLA_Screen and GLA_RastPort the context renders
        offscreen, into a bitmap of GLA_Width by GLA_Height (1 by 1 by
        default) it allocates as a friend of the default public screen's;
        glASetRast moves it to a window or another offscreen size later.

        GLA_ShareContext - a context whose textures, buffers, shaders and
                     programs the new one shares. The new context uses that
                     context's pipe screen, whatever its own target.

        GLA_ShareScreen - a context whose pipe screen the new one uses
                     without sharing its objects: drawables made for one
                     (glACreateDrawable) can then be bound to the other.

        GLA_DoubleBuf - ignored. All rendering is always double buffered.

        GLA_RGBMode - ignored. All rendering is done in RGB. Indexed modes are
                      not supported.

        GLA_AlphaFlag - ignored. All rendering is done with alpha channel.

        GLA_NoDepth - disables the depth/Z buffer. Depth buffer is enabled by
                      default and is 16 or 24 bit based on rendering
                      capabilities.

        GLA_NoStencil - disables the stencil buffer. Stencil buffer is enabled
                        by default.

        GLA_NoAccum - disables the accumulation buffer. Accumulation buffer is
                      enabled by default.

        GLA_ContextProfile - GLA_PROFILE_COMPAT (default), GLA_PROFILE_CORE,
                     GLA_PROFILE_ES1, or GLA_PROFILE_ES2.
                     A core profile context can offer a newer OpenGL version
                     than a compatibility profile one.

        GLA_ContextMajorVersion, GLA_ContextMinorVersion - the lowest OpenGL
                     version the context must provide (a core profile one is
                     at least 3.2). Creation fails if it cannot be provided;
                     otherwise the highest available version is used.
                     ES1 accepts 1.0/1.1 (default 1.0); ES2 accepts 2.0 or
                     3.0-3.2 (default 2.0). These select native GLES APIs.
                     Invalid profiles or GLES versions fail without falling
                     back to desktop OpenGL.

    RESULT

        A valid GL context or NULL of creation was not succesfull.
 
    BUGS

    INTERNALS

    HISTORY

*****************************************************************************/
{
    struct mesa3dgl_context * ctx = NULL;
    struct TagItem pscreen_tags[] =
    {
            { CPS_PipeFriendBitMap,     0       },
            { CPS_PipeScreenDriver,     0       },
            { CPS_GalliumCoreAPI,       0       },
            { TAG_DONE,                 0       }
    };
    struct pipe_screen * pscreen = NULL;
    struct st_context_attribs attribs = {0};
    enum st_context_error st_error = 0;
    IPTR profile = GetTagData(GLA_ContextProfile, GLA_PROFILE_COMPAT, tagList);
    struct mesa3dgl_context *share =
        (struct mesa3dgl_context *)GetTagData(GLA_ShareContext, 0, tagList);
    /* Sharing objects implies sharing their pipe screen; GLA_ShareScreen
     * shares the screen only, so that drawables can pass between contexts. */
    struct mesa3dgl_context *same_screen = share ? share :
        (struct mesa3dgl_context *)GetTagData(GLA_ShareScreen, 0, tagList);
    IPTR major, minor;

    D(bug("[MESA3DGL] %s()\n", __func__));

    /* Validate before acquiring native resources. The ES2 state-tracker API
     * covers both GLES 2 and GLES 3; it must never use a desktop GL profile. */
    switch (profile)
    {
        case GLA_PROFILE_COMPAT:
            attribs.profile = API_OPENGL_COMPAT;
            major = GetTagData(GLA_ContextMajorVersion, 1, tagList);
            minor = GetTagData(GLA_ContextMinorVersion, 0, tagList);
            break;
        case GLA_PROFILE_CORE:
            attribs.profile = API_OPENGL_CORE;
            major = GetTagData(GLA_ContextMajorVersion, 3, tagList);
            minor = GetTagData(GLA_ContextMinorVersion, 2, tagList);
            break;
        case GLA_PROFILE_ES1:
            attribs.profile = API_OPENGLES;
            major = GetTagData(GLA_ContextMajorVersion, 1, tagList);
            minor = GetTagData(GLA_ContextMinorVersion, 0, tagList);
            if (major != 1 || minor > 1)
                return NULL;
            break;
        case GLA_PROFILE_ES2:
            attribs.profile = API_OPENGLES2;
            major = GetTagData(GLA_ContextMajorVersion, 2, tagList);
            minor = GetTagData(GLA_ContextMinorVersion, 0, tagList);
            if (!((major == 2 && minor == 0) || (major == 3 && minor <= 2)))
                return NULL;
            break;
        default:
            return NULL;
    }
    /* Tag values have pointer width; reject unrepresentable/negative values
     * before narrowing them to the state tracker's integer version fields. */
    if (major > INT_MAX || minor > INT_MAX ||
        (SIPTR)major < 1 || (SIPTR)minor < 0)
        return NULL;

    attribs.major = major;
    attribs.minor = minor;
    /* Preserve the existing desktop core minimum and desktop defaults. */
    if (profile == GLA_PROFILE_CORE &&
        (attribs.major < 3 || (attribs.major == 3 && attribs.minor < 2)))
    {
        attribs.major = 3;
        attribs.minor = 2;
    }

    if (!(ctx = (struct mesa3dgl_context *)AllocVec(sizeof(struct mesa3dgl_context), MEMF_PUBLIC | MEMF_CLEAR)))
    {
        bug("%s: ERROR - failed to allocate GLAContext\n", __func__);
        return NULL;
    }
    pscreen_tags[1].ti_Data = (IPTR)&ctx->driver;

    D(bug("[MESA3DGL] %s: ctx @ 0x%p\n", __func__, ctx));

    if (!MESA3DGLPrepareTarget(&ctx->own, tagList))
    {
        bug("%s: ERROR - failed to select visible rastport\n", __func__);
        goto error_out;
    }

    D(bug("[MESA3DGL] %s: visible_rp @ 0x%p\n", __func__, ctx->own.visible_rp));
    pscreen_tags[0].ti_Data = (IPTR)ctx->own.visible_rp->BitMap;
    D(bug("[MESA3DGL] %s:   _bmap @ 0x%p\n", __func__, pscreen_tags[0].ti_Data));

    if (same_screen)
    {
        /* Objects and drawables live in a pipe screen: use the other one's. */
        ctx->shared = same_screen->shared;
        /* Contexts come and go on any core. */
        __atomic_add_fetch(&ctx->shared->refs, 1, __ATOMIC_ACQ_REL);
        ctx->driver = ctx->shared->driver;
        ctx->stmanager = ctx->shared->stmanager;
    }
    else
    {
        pscreen_tags[2].ti_Data = (IPTR)gallium_core_get_api();

        if (CreatePipeV(pscreen_tags))
        {
            pscreen = CreatePipeScreen(ctx->driver);

            if (!pscreen)
            {
                bug("%s: ERROR -  failed to create gallium pipe screen\n", __func__);
                goto error_out;
            }
        }
        else
        {
            bug("%s: ERROR -  failed to create gallium pipe\n", __func__);
            goto error_out;
        }

        D(bug("[MESA3DGL] %s: pipe screen @ 0x%p\n", __func__, pscreen));
        D(bug("[MESA3DGL] %s: pipe driver @ 0x%p\n", __func__, ctx->driver));

        if (!(ctx->stmanager = MESA3DGLNewStManager(pscreen)))
        {
            bug("%s: ERROR - failed to create ST Manager\n", __func__);
            DestroyPipeScreen(ctx->driver, pscreen);
#if (0)
            DestroyPipe(ctx->driver);
#endif
            goto error_out;
        }
        if (!(ctx->shared = AllocVec(sizeof(*ctx->shared), MEMF_PUBLIC | MEMF_CLEAR)))
            goto error_out;
        ctx->shared->driver = ctx->driver;
        ctx->shared->stmanager = ctx->stmanager;
        ctx->shared->refs = 1;
    }

    D(bug("[MESA3DGL] %s: ST Manager @ 0x%p \n", __func__, ctx->stmanager));

    if (!MESA3DGLFillVisual(&ctx->stvis, ctx->stmanager->screen, ctx->own.BitsPerPixel, tagList))
    {
        bug("%s: ERROR -  failed to fill ST Visual\n", __func__);
        goto error_out;
    }

    attribs.visual = ctx->stvis;

    /* driconf defaults (driinfo_gallium.h) - we have no driconf, so the
     * options struct would otherwise be all-false. Without the compressed
     * fallback a driver lacking RGTC (v3d) fails compute_version()'s GL 3.0
     * test and gets pinned at GL 2.1. */
    attribs.options.allow_compressed_fallback = TRUE;

    ctx->st = st_api_create_context(ctx->stmanager, &attribs, &st_error,
        share ? share->st : NULL);
    if (!ctx->st)
    {
        bug("%s: ERROR -  failed to create mesa state tracker context\n", __func__);
        goto error_out;
    }

    ctx->own.framebuffer = MESA3DGLNewFrameBuffer(ctx, &ctx->stvis);

    if (!ctx->own.framebuffer)
    {
        bug("%s: ERROR -  failed to create frame buffer\n", __func__);
        goto error_out;
    }
    ctx->own.driver = ctx->driver;

    /* The state tracker is the current Mesa object; glAGetCurrentContext
     * answers with its frontend owner, this context (codex's fix). */
    ctx->st->frontend_context = ctx;
    return (GLAContext)ctx;

error_out:
    if (ctx->shared && __atomic_sub_fetch(&ctx->shared->refs, 1, __ATOMIC_ACQ_REL) == 0)
    {
        MESA3DGLFreeStManager(ctx->driver, ctx->stmanager);
        FreeVec(ctx->shared);
    }
    else if (!ctx->shared && ctx->stmanager)
        MESA3DGLFreeStManager(ctx->driver, ctx->stmanager);
    MESA3DGLFreeTarget(&ctx->own);
    if (ctx) MESA3DGLFreeContext(ctx);
    return (GLAContext)NULL;
}
