/*
    Copyright 2012-2026, The AROS Development Team. All rights reserved.

    Copyright (C) 2026, John KNIPPER <code@jkn.me>.

    EGL driver backend for AROS — bridges EGL to mesa3dgl (GL/gla.h).
    Mesa 26.0.0 driver API (const _EGLDriver _eglDriver).

    Supports two platform types:
    - _EGL_PLATFORM_AROS:        window and pbuffer surfaces
    - _EGL_PLATFORM_SURFACELESS: pbuffer surfaces only (no Window needed),
      used by WebKit's PlatformDisplaySurfaceless for headless rendering.
*/

#include "eglsurface.h"
#include "eglcontext.h"
#include "eglcurrent.h"
#include "eglconfig.h"
#include "egldisplay.h"
#include "egldriver.h"
#include "egllog.h"

#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <GL/gla.h>

#include <aros/debug.h>

/* GL library base — opened via gl.library (pass-through wrapper).
 * gl.library reads ENVARC:SYS/GL.default to find the versioned
 * mesa3dgl implementation (e.g. mesa3dgl26-0.library).            */
struct Library * GLBase = NULL;

/* Keep dispatch entry points callable from OpenLibrary until expunge,
 * including lookups made before the first eglInitialize(). */
/* gl.library hands out the implementation's base. Drawables apart from
 * contexts (glACreateDrawable, glAMakeCurrentDrawables) came with mesa3dgl
 * 26.3; an older implementation has no vectors there at all, so it is
 * refused rather than called. */
#define MESA3DGL_DRAWABLES_VERSION  26
#define MESA3DGL_DRAWABLES_REVISION 3

static struct Library * open_gl(void)
{
    struct Library * gl = OpenLibrary((CONST_STRPTR)"gl.library", 20L);

    if (gl && (gl->lib_Version < MESA3DGL_DRAWABLES_VERSION ||
        (gl->lib_Version == MESA3DGL_DRAWABLES_VERSION &&
         gl->lib_Revision < MESA3DGL_DRAWABLES_REVISION)))
    {
        bug("[EGL] %s %u.%u has no GLA drawables (needs %u.%u)\n",
            gl->lib_Node.ln_Name, gl->lib_Version, gl->lib_Revision,
            MESA3DGL_DRAWABLES_VERSION, MESA3DGL_DRAWABLES_REVISION);
        CloseLibrary(gl);
        gl = NULL;
    }
    return gl;
}

static int OpenMesa(struct Library *libbase)
{
    (void)libbase;
    GLBase = open_gl();
    return GLBase != NULL;
}
ADD2INITLIB(OpenMesa, 5);

/* Resolve GL through the library that owns the current Mesa context. A
 * second static glapi in egl.library would have separate task-storage slots
 * and return dispatch entry points disconnected from glAMakeCurrent(). */
_EGLProc _mesa_glapi_get_proc_address(const char *name)
{
    if (!GLBase || !name)
        return NULL;
    return (_EGLProc)glAGetProcAddress((const GLubyte *)name);
}


/*
 * A display owns an anchor GLA context (offscreen, never current) whose pipe
 * screen and visual everything on the display shares. Each surface owns a
 * GLA drawable made for that screen (glACreateDrawable): a window, or a
 * pbuffer's offscreen bitmap of its size, with the framebuffer that keeps
 * its pixels whichever context draws to it. Each EGL context owns a GLA
 * context on the same screen (GLA_ShareScreen of the anchor, or
 * GLA_ShareContext of its share context). eglMakeCurrent binds the task's
 * native context to the draw and read drawables first and changes the
 * logical binding only once that succeeded; without a surface
 * (EGL_KHR_surfaceless_context) the context has no drawable at all.
 */

struct egl_arosmesa_context
{
    _EGLContext base;
    GLAContext amesactx;
};

static inline struct egl_arosmesa_context * egl_arosmesa_context(_EGLContext * ctx)
{
    return (struct egl_arosmesa_context *)ctx;
}

struct egl_arosmesa_surface
{
    _EGLSurface base;
    struct Window * win;        /* non-NULL for window surfaces */
    GLADrawable drawable;
};

static inline struct egl_arosmesa_surface * egl_arosmesa_surface(_EGLSurface * surf)
{
    return (struct egl_arosmesa_surface *)surf;
}

static inline GLAContext anchor_of(_EGLDisplay *disp)
{
    return (GLAContext)disp->DriverData;
}

/* The visual every context and drawable of a display has: RGBA with depth
 * and stencil, as the configuration announces. */
#define VISUAL_TAGS(attributes, i) do { \
    attributes[i].ti_Tag = GLA_DoubleBuf; attributes[i++].ti_Data = GL_TRUE; \
    attributes[i].ti_Tag = GLA_RGBMode;   attributes[i++].ti_Data = GL_TRUE; \
    attributes[i].ti_Tag = GLA_NoAccum;   attributes[i++].ti_Data = GL_TRUE; \
} while (0)

static void free_context(_EGLContext *ctx)
{
    struct egl_arosmesa_context * eglctx = egl_arosmesa_context(ctx);

    if (eglctx->amesactx)
    {
        if (glAGetCurrentContext() == eglctx->amesactx)
            glAMakeCurrent(NULL);
        glADestroyContext(eglctx->amesactx);
    }
    free(eglctx);
}

static void free_surface(_EGLSurface *surf)
{
    struct egl_arosmesa_surface * s = egl_arosmesa_surface(surf);

    if (s->drawable)
        glADestroyDrawable(s->drawable);
    free(s);
}

/* --- Driver callbacks (Mesa 26 API: no _EGLDriver* parameter) --- */

static EGLBoolean
egl_arosmesa_terminate(_EGLDisplay *disp)
{
    _eglReleaseDisplayResources(disp);
    _eglCleanupDisplay(disp);
    if (anchor_of(disp))
        glADestroyContext(anchor_of(disp));
    disp->DriverData = NULL;

    return EGL_TRUE;
}

static EGLBoolean
egl_arosmesa_destroycontext(_EGLDisplay *dpy, _EGLContext *ctx)
{
    (void)dpy;
    if (_eglPutContext(ctx))
        free_context(ctx);

    return EGL_TRUE;
}

static EGLBoolean
egl_arosmesa_destroysurface(_EGLDisplay *disp, _EGLSurface *surf)
{
    (void)disp;
    if (_eglPutSurface(surf))
        free_surface(surf);

    return EGL_TRUE;
}

/* The calling task's current context presents its draw surface; a pbuffer
 * swap has no effect. A display is shared by tasks, so the binding comes
 * from the task, never from the display (codex's fix). */
static EGLBoolean
egl_arosmesa_swapbuffers(_EGLDisplay *disp, _EGLSurface *draw)
{
    _EGLContext *current = _eglGetCurrentContext();
    GLAContext native;

    if (!draw || !egl_arosmesa_surface(draw)->win)
        return EGL_TRUE;
    if (!current || current->Resource.Display != disp || current->DrawSurface != draw)
        return _eglError(EGL_BAD_SURFACE, "eglSwapBuffers");
    native = egl_arosmesa_context(current)->amesactx;
    if (!native || glAGetCurrentContext() != native)
        return _eglError(EGL_BAD_CONTEXT, "eglSwapBuffers");
    glASwapBuffers(native);
    return EGL_TRUE;
}

static GLADrawable drawable_of(_EGLSurface *surf)
{
    return surf ? egl_arosmesa_surface(surf)->drawable : NULL;
}

static EGLBoolean
egl_arosmesa_makecurrent(_EGLDisplay *disp, _EGLSurface *dsurf,
        _EGLSurface *rsurf, _EGLContext *ctx)
{
    _EGLContext *before = _eglGetCurrentContext();
    _EGLContext *old_ctx;
    _EGLSurface *old_dsurf, *old_rsurf;
    GLAContext native = ctx ? egl_arosmesa_context(ctx)->amesactx : NULL;

    (void)disp;
    /* The native binding first: on failure nothing has changed. */
    if (ctx && !native)
        return _eglError(EGL_BAD_CONTEXT, "eglMakeCurrent");
    if (!glAMakeCurrentDrawables(native, drawable_of(dsurf), drawable_of(rsurf)))
        return _eglError(EGL_BAD_MATCH, "eglMakeCurrent");

    if (!_eglBindContext(ctx, dsurf, rsurf, &old_ctx, &old_dsurf, &old_rsurf))
    {
        /* The logical binding refused: the task goes back to what it had. */
        if (before)
            glAMakeCurrentDrawables(egl_arosmesa_context(before)->amesactx,
                drawable_of(before->DrawSurface), drawable_of(before->ReadSurface));
        else
            glAMakeCurrentDrawables(NULL, NULL, NULL);
        return EGL_FALSE;
    }

    /* The references the old binding held; an object destroyed while
     * current goes now. */
    if (old_dsurf && _eglPutSurface(old_dsurf))
        free_surface(old_dsurf);
    if (old_rsurf && _eglPutSurface(old_rsurf))
        free_surface(old_rsurf);
    if (old_ctx && _eglPutContext(old_ctx))
        free_context(old_ctx);
    return EGL_TRUE;
}

static _EGLSurface *
egl_arosmesa_createwindowsurface(_EGLDisplay *disp, _EGLConfig *conf,
        void *native_window, const EGLint *attrib_list)
{
    EGLNativeWindowType window = (EGLNativeWindowType)native_window;
    struct egl_arosmesa_surface * surf;
    struct TagItem target[] = { { GLA_Window, (IPTR)window }, { TAG_DONE, 0 } };

    if (!window)
    {
        _eglError(EGL_BAD_NATIVE_WINDOW, "eglCreateWindowSurface");
        return NULL;
    }
    surf = calloc(1, sizeof(struct egl_arosmesa_surface));
    if (!surf)
        return NULL;

    if (!_eglInitSurface(&surf->base, disp, EGL_WINDOW_BIT, conf, attrib_list, native_window))
    {
        free(surf);
        return NULL;
    }

    surf->base.Width = window->Width - window->BorderLeft - window->BorderRight;
    surf->base.Height = window->Height - window->BorderTop -  window->BorderBottom;
    surf->win = window;
    surf->drawable = glACreateDrawable(anchor_of(disp), target);
    if (!surf->drawable)
    {
        free(surf);
        _eglError(EGL_BAD_ALLOC, "eglCreateWindowSurface");
        return NULL;
    }

    return &surf->base;
}

/* A pbuffer: a drawable of its own, an offscreen bitmap of its size. */
static _EGLSurface *
egl_arosmesa_createpbuffersurface(_EGLDisplay *disp, _EGLConfig *conf,
        const EGLint *attrib_list)
{
    struct egl_arosmesa_surface * surf = calloc(1, sizeof(struct egl_arosmesa_surface));
    struct TagItem target[3];

    if (!surf)
        return NULL;

    if (!_eglInitSurface(&surf->base, disp, EGL_PBUFFER_BIT, conf, attrib_list, NULL))
    {
        free(surf);
        return NULL;
    }

    /* Width/Height come from attrib_list (EGL_WIDTH/EGL_HEIGHT),
     * already parsed by _eglInitSurface into surf->base.Width/Height */
    target[0].ti_Tag = GLA_Width;  target[0].ti_Data = surf->base.Width > 0 ? surf->base.Width : 1;
    target[1].ti_Tag = GLA_Height; target[1].ti_Data = surf->base.Height > 0 ? surf->base.Height : 1;
    target[2].ti_Tag = TAG_DONE;
    surf->drawable = glACreateDrawable(anchor_of(disp), target);
    if (!surf->drawable)
    {
        free(surf);
        _eglError(EGL_BAD_ALLOC, "eglCreatePbufferSurface");
        return NULL;
    }

    D(bug("[EGL] pbuffer surface %dx%d created\n",
          surf->base.Width, surf->base.Height));

    return &surf->base;
}

static _EGLContext *
egl_arosmesa_createcontext(_EGLDisplay *disp, _EGLConfig *conf,
        _EGLContext *share_list, const EGLint *attrib_list)
{
    struct egl_arosmesa_context * ctx = calloc(1, sizeof(struct egl_arosmesa_context));
    struct TagItem attributes[12];
    int i = 0;

    if (!ctx)
        return NULL;

    if (!_eglInitContext(&ctx->base, disp, conf, share_list, attrib_list))
    {
        free(ctx);
        return NULL;
    }

    VISUAL_TAGS(attributes, i);

    /* EGL metadata must select the actual native Mesa API/profile. */
    attributes[i].ti_Tag = GLA_ContextProfile;
    if (ctx->base.ClientAPI == EGL_OPENGL_ES_API)
        attributes[i++].ti_Data = ctx->base.ClientMajorVersion >= 2 ?
            GLA_PROFILE_ES2 : GLA_PROFILE_ES1;
    else
        attributes[i++].ti_Data =
            ((ctx->base.ClientMajorVersion > 3 ||
              (ctx->base.ClientMajorVersion == 3 && ctx->base.ClientMinorVersion >= 2)) &&
             (ctx->base.Profile & EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR)) ?
            GLA_PROFILE_CORE : GLA_PROFILE_COMPAT;
    attributes[i].ti_Tag = GLA_ContextMajorVersion;
    attributes[i++].ti_Data = ctx->base.ClientMajorVersion;
    attributes[i].ti_Tag = GLA_ContextMinorVersion;
    attributes[i++].ti_Data = ctx->base.ClientMinorVersion;
    /* Objects with the share context, the screen with the display's anchor
     * either way, so that every surface of the display fits. */
    if (share_list)
    {
        attributes[i].ti_Tag = GLA_ShareContext;
        attributes[i++].ti_Data = (IPTR)egl_arosmesa_context(share_list)->amesactx;
    }
    else
    {
        attributes[i].ti_Tag = GLA_ShareScreen;
        attributes[i++].ti_Data = (IPTR)anchor_of(disp);
    }
    attributes[i].ti_Tag = TAG_DONE;

    ctx->amesactx = glACreateContext(attributes);
    D(bug("[EGL] glACreateContext = %p (share %p)\n", ctx->amesactx, share_list));
    if (!ctx->amesactx)
    {
        free(ctx);
        _eglError(EGL_BAD_MATCH, "eglCreateContext");
        return NULL;
    }

    return &ctx->base;
}

/* A surfaceless-platform display has pbuffer configurations only. */
static inline EGLBoolean is_surfaceless(_EGLDisplay *disp)
{
    return disp->Platform == _EGL_PLATFORM_SURFACELESS;
}

static void create_configs(_EGLDisplay *dpy)
{
    _EGLConfig * cfg = calloc(1, sizeof(_EGLConfig));
    _eglInitConfig(cfg, dpy, 1);

    /* Every API the GLA profiles create: OpenGL, OpenGL ES 1 and ES 2/3 (an
     * ES2 profile gives ES 3.1 on softpipe, 3.2 on agxgallium). Without the
     * ES3 bit, EGL refuses an ES 3 context on the configuration. */
    cfg->RenderableType = EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT |
        EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR;
    cfg->Conformant = cfg->RenderableType;

    if (is_surfaceless(dpy))
        cfg->SurfaceType = EGL_PBUFFER_BIT;
    else
        cfg->SurfaceType = EGL_WINDOW_BIT | EGL_PBUFFER_BIT;

    _eglSetConfigKey(cfg, EGL_RED_SIZE, 8);
    _eglSetConfigKey(cfg, EGL_GREEN_SIZE, 8);
    _eglSetConfigKey(cfg, EGL_BLUE_SIZE, 8);
    _eglSetConfigKey(cfg, EGL_ALPHA_SIZE, 8);
    _eglSetConfigKey(cfg, EGL_DEPTH_SIZE, 24);
    _eglSetConfigKey(cfg, EGL_STENCIL_SIZE, 8);

    _eglLinkConfig(cfg);
}

static EGLBoolean
egl_arosmesa_initialize(_EGLDisplay *dpy)
{
    D(bug("[EGL] Initialize: platform=%d (AROS=%d, surfaceless=%d)\n",
          dpy->Platform, _EGL_PLATFORM_AROS, _EGL_PLATFORM_SURFACELESS));

    if (!GLBase)
    {
        GLBase = open_gl();
        D(bug("[EGL] gl implementation = %p\n", GLBase));
    }

    if (!GLBase)
    {
        _eglLog(_EGL_WARNING, "egl_arosmesa: cannot open gl.library");
        return EGL_FALSE;
    }

    if (!anchor_of(dpy))
    {
        /* The display's screen: an offscreen 1x1 context never made current. */
        struct TagItem attributes[6];
        int i = 0;

        VISUAL_TAGS(attributes, i);
        attributes[i].ti_Tag = TAG_DONE;
        dpy->DriverData = glACreateContext(attributes);
        if (!dpy->DriverData)
        {
            _eglLog(_EGL_WARNING, "egl_arosmesa: no GL context for the display");
            return EGL_FALSE;
        }
    }

    create_configs(dpy);

    dpy->ClientAPIs |= EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT;
    dpy->Version = 14; /* EGL 1.4 */

    /* Extensions supported by our Mesa/softpipe backend */
    /* Only what this driver does: config attributes, versioned and profiled
     * contexts (GLA tags), and contexts current without a surface. */
    dpy->Extensions.KHR_config_attribs = EGL_TRUE;
    dpy->Extensions.KHR_create_context = EGL_TRUE;
    dpy->Extensions.KHR_surfaceless_context = EGL_TRUE;

    D(bug("[EGL] display initialized: platform=%d surfaceless=%d\n",
          dpy->Platform, is_surfaceless(dpy)));

    return EGL_TRUE;
}

/* Mesa 26.0.0: single const global driver struct */
const _EGLDriver _eglDriver = {
    .Initialize          = egl_arosmesa_initialize,
    .Terminate           = egl_arosmesa_terminate,
    .CreateContext        = egl_arosmesa_createcontext,
    .DestroyContext       = egl_arosmesa_destroycontext,
    .MakeCurrent          = egl_arosmesa_makecurrent,
    .CreateWindowSurface  = egl_arosmesa_createwindowsurface,
    .CreatePbufferSurface = egl_arosmesa_createpbuffersurface,
    .DestroySurface       = egl_arosmesa_destroysurface,
    .SwapBuffers          = egl_arosmesa_swapbuffers,
};

static int CloseMesa(struct Library *libbase)
{
    (void)libbase;
    if (GLBase)
    {
        CloseLibrary(GLBase);
        GLBase = NULL;
    }
    return TRUE;
}

ADD2EXPUNGELIB(CloseMesa, 5)
