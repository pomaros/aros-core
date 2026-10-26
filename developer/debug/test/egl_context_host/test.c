/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Actual backend functions are inserted at DRIVER_FUNCTIONS by run.py.
 * Mesa resource/native GL shims do not establish a native EGL/TLS ABI. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

typedef int EGLBoolean;
typedef void *GLAContext;
typedef void *GLADrawable;
#define EGL_TRUE 1
#define EGL_FALSE 0
#define EGL_BAD_SURFACE 0x300D
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_MATCH 0x3009
typedef struct Display { void *DriverData; } _EGLDisplay;
typedef struct Surface { int unused; } _EGLSurface;
typedef struct Context {
    struct { _EGLDisplay *Display; } Resource;
    _EGLSurface *DrawSurface, *ReadSurface;
} _EGLContext;

/* DRIVER_STRUCTS */

static _Thread_local _EGLContext *current;
static _EGLContext *last_bound;
struct st_context;
struct gl_context { struct st_context *st; };
struct st_context { struct gl_context *ctx; void *frontend_context; };
struct mesa3dgl_context { struct st_context *st; };
static _Thread_local struct gl_context *mesa_current;
static _Thread_local GLAContext swapped;
static _Thread_local unsigned presentations;
static _Thread_local int error;
static __attribute__((unused)) _EGLContext *_eglGetCurrentContext(void) { return current; }
static __attribute__((unused)) struct st_context *st_api_get_current(void)
{
    return mesa_current ? mesa_current->st : NULL;
}
#define GET_CURRENT_CONTEXT(ctx) struct gl_context *ctx = mesa_current
#define D(expression)
/* NATIVE_GETTER */
static void glAMakeCurrent(GLAContext context)
{
    struct mesa3dgl_context *native = context;
    mesa_current = native ? native->st->ctx : NULL;
}
static void glASwapBuffers(GLAContext context)
{
    swapped = context;
    ++presentations;
}
static __attribute__((unused)) EGLBoolean _eglError(int code, const char *where)
{
    (void)where;
    error = code;
    return EGL_FALSE;
}
static EGLBoolean _eglBindContext(_EGLContext *context, _EGLSurface *draw,
    _EGLSurface *read, _EGLContext **old_context, _EGLSurface **old_draw,
    _EGLSurface **old_read)
{
    *old_context = current;
    *old_draw = current ? current->DrawSurface : NULL;
    *old_read = current ? current->ReadSurface : NULL;
    current = context;
    last_bound = context;
    if (context) { context->DrawSurface = draw; context->ReadSurface = read; }
    return EGL_TRUE;
}
static EGLBoolean _eglPutSurface(_EGLSurface *surface) { (void)surface; return EGL_FALSE; }
static EGLBoolean _eglPutContext(_EGLContext *context) { (void)context; return EGL_FALSE; }
static void free_surface(_EGLSurface *surface) { (void)surface; assert(0); }
static void free_context(_EGLContext *context) { (void)context; assert(0); }
static EGLBoolean glAMakeCurrentDrawables(GLAContext context, GLADrawable draw, GLADrawable read)
{ (void)draw; (void)read; glAMakeCurrent(context); return EGL_TRUE; }

/* DRIVER_FUNCTIONS */

static _EGLDisplay display;
static struct egl_arosmesa_surface surface_a, surface_b;
static struct egl_arosmesa_context context_a, context_b;
static struct mesa3dgl_context native_a, native_b;
static struct gl_context mesa_a, mesa_b;
static struct st_context tracker_a, tracker_b;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static unsigned phase;

static void set_phase(unsigned next)
{
    phase = next;
    assert(pthread_cond_broadcast(&changed) == 0);
}
static void wait_phase(unsigned expected)
{
    while (phase != expected)
        assert(pthread_cond_wait(&changed, &gate) == 0);
}

static void *thread_a(void *unused)
{
    (void)unused;
    assert(pthread_mutex_lock(&gate) == 0);
    assert(egl_arosmesa_makecurrent(&display, &surface_a.base,
        &surface_a.base, &context_a.base));
    assert(glAGetCurrentContext() == &native_a);
    set_phase(1);
    wait_phase(2); /* B changes the shared display's last-bound context. */
    assert(egl_arosmesa_swapbuffers(&display, &surface_a.base));
    assert(presentations == 1 && swapped == &native_a);
    assert(egl_arosmesa_makecurrent(&display, NULL, NULL, NULL));
    assert(glAGetCurrentContext() == NULL);
    set_phase(3); /* A unbinds; B must still present its own context. */
    assert(pthread_mutex_unlock(&gate) == 0);
    return NULL;
}
static void *thread_b(void *unused)
{
    (void)unused;
    assert(pthread_mutex_lock(&gate) == 0);
    wait_phase(1);
    assert(egl_arosmesa_makecurrent(&display, &surface_b.base,
        &surface_b.base, &context_b.base));
    assert(glAGetCurrentContext() == &native_b);
    set_phase(2);
    wait_phase(3);
    assert(egl_arosmesa_swapbuffers(&display, &surface_b.base));
    assert(presentations == 1 && swapped == &native_b);
    assert(egl_arosmesa_makecurrent(&display, NULL, NULL, NULL));
    assert(glAGetCurrentContext() == NULL);
    assert(pthread_mutex_unlock(&gate) == 0);
    return NULL;
}

int main(void)
{
    pthread_t a, b;
    tracker_a.ctx = &mesa_a; tracker_b.ctx = &mesa_b;
    mesa_a.st = &tracker_a; mesa_b.st = &tracker_b;
    native_a.st = &tracker_a; native_b.st = &tracker_b;
    struct mesa3dgl_context *ctx = &native_a;
    /* NATIVE_OWNER */
    ctx = &native_b;
    /* NATIVE_OWNER */
    assert((void *)&native_a != (void *)&mesa_a);
    assert((void *)&native_b != (void *)&mesa_b);
    surface_a.win = (struct Window *)&native_a;
    surface_b.win = (struct Window *)&native_b;
    context_a.base.Resource.Display = &display;
    context_b.base.Resource.Display = &display;
    context_a.amesactx = &native_a; context_b.amesactx = &native_b;
    assert(pthread_create(&a, NULL, thread_a, NULL) == 0);
    assert(pthread_create(&b, NULL, thread_b, NULL) == 0);
    assert(pthread_join(a, NULL) == 0);
    assert(pthread_join(b, NULL) == 0);

    assert(egl_arosmesa_swapbuffers(&display, NULL));
    struct egl_arosmesa_surface pbuffer = {0};
    assert(egl_arosmesa_swapbuffers(&display, &pbuffer.base));
    assert(!presentations);
    assert(!egl_arosmesa_swapbuffers(&display, &surface_a.base));
    assert(error == EGL_BAD_SURFACE);
    assert(egl_arosmesa_makecurrent(&display, &surface_a.base,
        &surface_a.base, &context_a.base));
    assert(!egl_arosmesa_swapbuffers(&display, &surface_b.base));
    assert(error == EGL_BAD_SURFACE);
    _EGLDisplay other_display = {0};
    assert(!egl_arosmesa_swapbuffers(&other_display, &surface_a.base));
    assert(error == EGL_BAD_SURFACE);
    glAMakeCurrent(&native_b);
    assert(!egl_arosmesa_swapbuffers(&display, &surface_a.base));
    assert(error == EGL_BAD_CONTEXT && !presentations);
    glAMakeCurrent(&native_a);
    context_a.amesactx = NULL;
    assert(!egl_arosmesa_swapbuffers(&display, &surface_a.base));
    assert(error == EGL_BAD_CONTEXT && !presentations);
    context_a.amesactx = &native_a;
    assert(egl_arosmesa_swapbuffers(&display, &surface_a.base));
    assert(presentations == 1 && swapped == &native_a);
    puts("EGL context HOST: PASS (two tasks, unbind, surface/display/native checks)");
    return 0;
}
