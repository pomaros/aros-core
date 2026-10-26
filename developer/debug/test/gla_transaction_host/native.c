/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Appended to the target fixture; actual drawable/context APIs replace SOURCE. */
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
static unsigned live_framebuffers, native_bind_calls;
static struct st_context *current_tracker;
static struct pipe_frontend_drawable *current_draw, *current_read;
static void *AllocVec(size_t bytes, unsigned flags)
{ (void)flags; void *p=calloc(1,bytes); assert(p); return p; }
static void FreeVec(void *p) { free(p); }
static struct mesa3dgl_framebuffer *MESA3DGLNewFrameBuffer(struct mesa3dgl_context *ctx,
    const struct st_visual *visual)
{
    (void)visual; assert(ctx->stmanager && ctx->stmanager->alive);
    struct mesa3dgl_framebuffer *fb=calloc(1,sizeof(*fb)); assert(fb);
    fb->base.manager=ctx->stmanager; fb->stvis=*visual; ++live_framebuffers; return fb;
}
static void MESA3DGLFreeFrameBuffer(struct mesa3dgl_framebuffer *fb)
{
    if (!fb) return;
    assert(live_framebuffers && fb->base.manager->alive);
    --live_framebuffers; free(fb);
}
static void MESA3DGLFreeStManager(APTR driver, struct pipe_frontend_screen *manager)
{ (void)driver; assert(manager->alive && manager->trackers==0); manager->alive=0; }
static void MESA3DGLFreeContext(struct mesa3dgl_context *ctx) { free(ctx); }
static struct st_context *st_api_get_current(void) { return current_tracker; }
static void st_context_flush(struct st_context *ctx, unsigned flags,
    void *fence, void *a, void *b)
{ (void)flags; (void)fence; (void)a; (void)b; assert(ctx->manager->alive); }
static BOOL st_api_make_current(struct st_context *ctx,
    struct pipe_frontend_drawable *d, struct pipe_frontend_drawable *r)
{
    ++native_bind_calls;
    if (ctx) assert(ctx->manager->alive);
    current_tracker=ctx; current_draw=d; current_read=r; return TRUE;
}
static void st_api_destroy_drawable(struct pipe_frontend_drawable *d)
{ assert(d && d->manager->alive); }
static void st_destroy_context(struct st_context *ctx)
{ assert(ctx->manager->alive && ctx->manager->trackers); --ctx->manager->trackers; free(ctx); }

/* NATIVE_SOURCE */

static struct mesa3dgl_context *make_context(struct pipe_frontend_screen *manager)
{
    struct mesa3dgl_context *ctx=calloc(1,sizeof(*ctx)); assert(ctx);
    ctx->shared=calloc(1,sizeof(*ctx->shared)); assert(ctx->shared);
    ctx->shared->refs=1; ctx->shared->stmanager=manager; ctx->shared->driver=manager;
    ctx->stmanager=manager; ctx->driver=manager;
    ctx->st=calloc(1,sizeof(*ctx->st)); assert(ctx->st); ctx->st->manager=manager;
    ++manager->trackers;
    ctx->stvis.color_format=PIPE_FORMAT_B8G8R8A8_UNORM;
    ctx->own.framebuffer=MESA3DGLNewFrameBuffer(ctx,&ctx->stvis);
    return ctx;
}
int main(int argc, char **argv)
{
    assert(argc==2);
    struct pipe_frontend_screen manager={.alive=1}, alien_manager={.alive=1};
    struct mesa3dgl_context *creator=make_context(&manager);
    if (strcmp(argv[1],"cleanup")==0) {
        MESA3DGLFreeFrameBuffer(creator->own.framebuffer);
        creator->own.framebuffer=NULL;
        assert(creation_error(creator)==NULL);
        assert(!manager.alive && !manager.trackers && !live_framebuffers);
        equal_live(0,0,0,0);
        puts("GLA context failure drops tracker before provider: HOST PASS");
        return 0;
    }
    struct TagItem tags[]={{GLA_Width,8},{GLA_Height,8},{TAG_DONE,0}};
    struct mesa3dgl_drawable *d=glACreateDrawable(creator,tags);
    struct mesa3dgl_drawable *r=glACreateDrawable(creator,tags);
    assert(d && r && d!=r && d->framebuffer!=r->framebuffer);
    d->framebuffer->pixels[0]=0xaaaa1234;
    r->framebuffer->pixels[0]=0xbbbb5678;
    if (strcmp(argv[1],"lifetime")==0) {
        glADestroyContext(creator);
        assert(manager.alive);
        assert(d->framebuffer->pixels[0]==0xaaaa1234 && r->framebuffer->pixels[0]==0xbbbb5678);
        glADestroyDrawable(d);
        assert(manager.alive);
        glADestroyDrawable(r);
        assert(!manager.alive && live_framebuffers==0);
        equal_live(0,0,0,0);
        puts("GLA independent drawable retains provider: HOST PASS");
        return 0;
    }
    struct mesa3dgl_context *alien=make_context(&alien_manager);
    struct mesa3dgl_context *different_visual=make_context(&manager);
    FreeVec(different_visual->shared);
    different_visual->shared=creator->shared; ++creator->shared->refs;
    different_visual->stvis.color_format=PIPE_FORMAT_B5G6R5_UNORM;
    struct mesa3dgl_drawable *ad=glACreateDrawable(alien,tags);
    assert(ad);
    assert(glAMakeCurrentDrawables(creator,d,r));
    assert(current_tracker==creator->st && current_draw==&d->framebuffer->base && current_read==&r->framebuffer->base);
    unsigned before=native_bind_calls;
    if (strcmp(argv[1],"success")==0) {
        different_visual->stvis.color_format=PIPE_FORMAT_B8G8R8A8_UNORM;
        assert(glAMakeCurrentDrawables(different_visual,d,r));
        assert(current_tracker==different_visual->st);
        assert(current_draw==&d->framebuffer->base && current_read==&r->framebuffer->base);
        assert(d->framebuffer->pixels[0]==0xaaaa1234 && r->framebuffer->pixels[0]==0xbbbb5678);
        assert(glAMakeCurrentDrawables(creator,d,r));
        before=native_bind_calls;
    } else if (strcmp(argv[1],"screen")==0)
        assert(!glAMakeCurrentDrawables(creator,ad,r));
    else {
        if (strcmp(argv[1],"visual")!=0) {
            different_visual->stvis=creator->stvis;
            if (strcmp(argv[1],"depth")==0) ++different_visual->stvis.depth_stencil_format;
            else if (strcmp(argv[1],"accum")==0) ++different_visual->stvis.accum_format;
            else if (strcmp(argv[1],"samples")==0) ++different_visual->stvis.samples;
            else { assert(strcmp(argv[1],"buffers")==0); ++different_visual->stvis.buffer_mask; }
        }
        assert(!glAMakeCurrentDrawables(different_visual,d,r));
        assert(!glAMakeCurrentDrawables(different_visual,r,d));
    }
    assert(native_bind_calls==before && current_tracker==creator->st);
    assert(current_draw==&d->framebuffer->base && current_read==&r->framebuffer->base);
    assert(d->framebuffer->pixels[0]==0xaaaa1234 && r->framebuffer->pixels[0]==0xbbbb5678);
    assert(glAMakeCurrentDrawables(NULL,NULL,NULL));
    glADestroyDrawable(ad); glADestroyContext(alien);
    glADestroyContext(different_visual);
    glADestroyDrawable(d); glADestroyDrawable(r); glADestroyContext(creator);
    assert(!manager.alive && !alien_manager.alive && live_framebuffers==0);
    equal_live(0,0,0,0);
    puts("GLA native screen/visual admission: HOST PASS");
    return 0;
}
