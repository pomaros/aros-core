/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Actual backend and Mesa _eglBindContext bodies are inserted by the runner. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int EGLBoolean;
typedef void *GLAContext;
typedef void *GLADrawable;
#define EGL_TRUE 1
#define EGL_FALSE 0
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_MATCH 0x3009
#define EGL_BAD_ACCESS 0x3002
#define D(x)
typedef struct Display { void *DriverData;
    struct { unsigned KHR_surfaceless_context, KHR_no_config_context; } Extensions; } _EGLDisplay;
typedef struct Surface { unsigned refs; int Width, Height;
    struct Context *CurrentContext; void *Config; } _EGLSurface;
typedef struct Context { unsigned refs;
    struct { _EGLDisplay *Display; } Resource;
    _EGLSurface *DrawSurface, *ReadSurface;
    struct Thread *Binding; void *Config; } _EGLContext;
typedef struct Thread { _EGLContext *CurrentContext; } _EGLThreadInfo;
/* STRUCTS */
struct native_context { unsigned identity; };
struct native_drawable { unsigned identity, pixel; };
static _EGLThreadInfo thread;
static GLAContext native_current;
static GLADrawable native_draw, native_read;
static unsigned native_fail, freed_allocations, last_error;
static unsigned native_calls;
static unsigned destroyed_contexts, destroyed_drawables;
static _EGLThreadInfo *_eglGetCurrentThread(void) { return &thread; }
static void _eglGetContext(_EGLContext *ctx) { if (ctx) { assert(ctx->refs); ++ctx->refs; } }
static void _eglGetSurface(_EGLSurface *s) { if (s) { assert(s->refs); ++s->refs; } }
static EGLBoolean _eglPutContext(_EGLContext *ctx)
{ if (!ctx) return 0; assert(ctx->refs); return --ctx->refs == 0; }
static EGLBoolean _eglPutSurface(_EGLSurface *s)
{ if (!s) return 0; assert(s->refs); return --s->refs == 0; }
static EGLBoolean _eglError(unsigned error, const char *where);
/* MESA_BIND */
static EGLBoolean _eglError(unsigned error, const char *where)
{ last_error=error; (void)where; return EGL_FALSE; }
static GLAContext glAGetCurrentContext(void) { return native_current; }
static void glAMakeCurrent(GLAContext ctx)
{ native_current=ctx; native_draw=native_read=NULL; }
static EGLBoolean glAMakeCurrentDrawables(GLAContext ctx, GLADrawable d, GLADrawable r)
{
    ++native_calls;
    if (native_fail) return EGL_FALSE;
    native_current=ctx; native_draw=d; native_read=r; return EGL_TRUE;
}
static void glADestroyContext(GLAContext ctx)
{ assert(ctx && ctx != native_current); ++destroyed_contexts; free(ctx); }
static void glADestroyDrawable(GLADrawable d)
{ assert(d && d != native_draw && d != native_read); ++destroyed_drawables; free(d); }
static void release_allocation(void *object)
{
    /* Backend frees are counted separately from native object destruction. */
    assert(object); ++freed_allocations;
    free(object);
}
#define free release_allocation
/* BACKEND */
#undef free

static struct egl_arosmesa_context *new_context(_EGLDisplay *display)
{
    struct egl_arosmesa_context *ctx=calloc(1,sizeof(*ctx)); assert(ctx);
    ctx->base.refs=1; ctx->base.Resource.Display=display;
    ctx->amesactx=calloc(1,sizeof(struct native_context)); assert(ctx->amesactx);
    return ctx;
}
static struct egl_arosmesa_surface *new_surface(unsigned id)
{
    struct egl_arosmesa_surface *s=calloc(1,sizeof(*s)); assert(s);
    s->base.refs=1;
    struct native_drawable *d=calloc(1,sizeof(*d)); assert(d);
    d->identity=id; d->pixel=0xfeed0000+id; s->drawable=d;
    return s;
}
int main(int argc, char **argv)
{
    _EGLDisplay display={.Extensions={1,1}};
    struct egl_arosmesa_context *a=new_context(&display), *b=new_context(&display);
    struct egl_arosmesa_surface *ad=new_surface(1), *ar=new_surface(2), *bd=new_surface(3);
    assert(egl_arosmesa_makecurrent(&display,&ad->base,&ar->base,&a->base));
    assert(native_current==a->amesactx && native_draw==ad->drawable && native_read==ar->drawable);
    assert(a->base.refs==2 && ad->base.refs==2 && ar->base.refs==2);
    native_fail=1;
    assert(!egl_arosmesa_makecurrent(&display,&bd->base,&bd->base,&b->base));
    assert(thread.CurrentContext==&a->base && native_current==a->amesactx);
    assert(native_draw==ad->drawable && native_read==ar->drawable);
    assert(a->base.refs==2 && ad->base.refs==2 && ar->base.refs==2);
    assert(b->base.refs==1 && bd->base.refs==1);
    native_fail=0;
    _EGLThreadInfo other_thread = {.CurrentContext=&b->base};
    _EGLContext busy_context = {.Binding=&other_thread};
    int surface_case=argc>1 && strcmp(argv[1],"surface")==0;
    int config_case=argc>1 && strcmp(argv[1],"config")==0;
    if (surface_case)
        bd->base.CurrentContext=&busy_context;
    else if (config_case) {
        b->base.Config=&other_thread;
        bd->base.Config=&busy_context;
    } else
        b->base.Binding = &other_thread;
    unsigned calls_before = native_calls;
    assert(!egl_arosmesa_makecurrent(&display,&bd->base,&bd->base,&b->base));
    if (argc > 1)
        assert(native_calls == calls_before);
    assert(last_error==(config_case ? EGL_BAD_MATCH : EGL_BAD_ACCESS));
    if (surface_case)
        assert(bd->base.CurrentContext==&busy_context && !b->base.Binding);
    else if (!config_case)
        assert(b->base.Binding==&other_thread);
    assert(thread.CurrentContext==&a->base && native_current==a->amesactx);
    assert(native_draw==ad->drawable && native_read==ar->drawable);
    assert(a->base.refs==2 && ad->base.refs==2 && ar->base.refs==2);
    assert(b->base.refs==1 && bd->base.refs==1);
    b->base.Binding=NULL;
    bd->base.CurrentContext=NULL;
    b->base.Config=bd->base.Config=NULL;
    /* Deleting current EGL objects only releases application references. */
    assert(egl_arosmesa_destroycontext(&display,&a->base));
    assert(egl_arosmesa_destroysurface(&display,&ad->base));
    assert(egl_arosmesa_destroysurface(&display,&ar->base));
    assert(!destroyed_contexts && !destroyed_drawables);
    native_fail=1;
    assert(!egl_arosmesa_makecurrent(&display,&bd->base,&bd->base,&b->base));
    assert(thread.CurrentContext==&a->base && native_current==a->amesactx);
    assert(native_draw==ad->drawable && native_read==ar->drawable);
    assert(a->base.refs==1 && ad->base.refs==1 && ar->base.refs==1);
    assert(b->base.refs==1 && bd->base.refs==1);
    assert(!destroyed_contexts && !destroyed_drawables);
    native_fail=0;
    assert(egl_arosmesa_makecurrent(&display,&bd->base,&bd->base,&b->base));
    assert(destroyed_contexts==1 && destroyed_drawables==2);
    assert(native_current==b->amesactx && native_draw==bd->drawable && native_read==bd->drawable);
    assert(b->base.refs==2 && bd->base.refs==3);
    assert(((struct native_drawable *)bd->drawable)->pixel==0xfeed0003);
    assert(egl_arosmesa_destroycontext(&display,&b->base));
    assert(egl_arosmesa_destroysurface(&display,&bd->base));
    struct egl_arosmesa_context *c=new_context(&display);
    struct egl_arosmesa_surface *cd=new_surface(4);
    native_fail=1;
    assert(!egl_arosmesa_makecurrent(&display,&cd->base,&cd->base,&c->base));
    assert(thread.CurrentContext==&b->base && native_current==b->amesactx);
    assert(native_draw==bd->drawable && native_read==bd->drawable);
    assert(b->base.refs==1 && bd->base.refs==2);
    assert(c->base.refs==1 && cd->base.refs==1);
    assert(destroyed_contexts==1 && destroyed_drawables==2);
    native_fail=0;
    assert(egl_arosmesa_destroycontext(&display,&c->base));
    assert(egl_arosmesa_destroysurface(&display,&cd->base));
    assert(egl_arosmesa_makecurrent(&display,NULL,NULL,NULL));
    assert(!thread.CurrentContext && !native_current && !native_draw && !native_read);
    assert(destroyed_contexts==3 && destroyed_drawables==4);
    assert(freed_allocations==7);
    puts("EGL actual callbacks and Mesa Bind: HOST PASS");
    return 0;
}
