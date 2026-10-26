/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Actual production bodies replace SOURCE below. HOST resource facades only. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int BOOL;
typedef void VOID;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uintptr_t IPTR;
typedef void *APTR;
typedef void *GLAContext;
typedef void *GLADrawable;
#define TRUE 1
#define FALSE 0
#define D(x)
enum { TAG_DONE, GLA_Screen, GLA_Window, GLA_RastPort, GLA_Width, GLA_Height,
    GLA_Left, GLA_Top, GLA_Right, GLA_Bottom, BMA_DEPTH, BMF_MINPLANES,
    LAYERSIMPLE, CYBRMATTR_BPPIX, WFLG_GIMMEZEROZERO,
    PIPE_FORMAT_B8G8R8A8_UNORM, PIPE_FORMAT_B5G6R5_UNORM,
    RPTAG_ClipRectangle, RPTAG_ClipRectangleFlags,
    RPCRF_RELRIGHT=1, RPCRF_RELBOTTOM=2 };
struct TagItem { IPTR ti_Tag, ti_Data; };
struct Rectangle { LONG MinX, MinY, MaxX, MaxY; };
struct BitMap { unsigned bytes; };
struct Layer;
struct RastPort { struct BitMap *BitMap; struct Layer *Layer; unsigned owned, released; };
struct Layer { struct Rectangle bounds; struct RastPort *rp; };
struct Layer_Info { unsigned marker; };
struct Screen { struct RastPort RastPort; };
struct Window { struct RastPort *RPort; struct Screen *WScreen;
    ULONG Flags; LONG Width, Height, LeftEdge, TopEdge;
    LONG BorderLeft, BorderTop, BorderRight, BorderBottom; };
struct st_visual { unsigned buffer_mask, color_format, depth_stencil_format, accum_format, samples; };
struct pipe_frontend_screen { unsigned alive, trackers; };
struct pipe_frontend_drawable { unsigned stamp; struct pipe_frontend_screen *manager; };
struct st_context { struct pipe_frontend_screen *manager; };
struct mesa3dgl_framebuffer { struct pipe_frontend_drawable base;
    struct st_visual stvis;
    ULONG width, height; BOOL resized;
    void *textures[4]; unsigned pixels[8]; };
/* STRUCTS */

enum fault { NONE, BITMAP, LAYERINFO, LAYER, CLONE, CLONE_LAYER };
static enum fault fault;
static unsigned live_bitmap, live_info, live_layer, live_clone, updates;
static struct BitMap public_bitmap = {4};
static struct Screen public_screen = { .RastPort = { .BitMap=&public_bitmap } };
static ULONG mesa3dgl_render_scale = 1;
static IPTR GetTagData(IPTR tag, IPTR fallback, const struct TagItem *tags)
{
    for (; tags && tags->ti_Tag != TAG_DONE; ++tags)
        if (tags->ti_Tag == tag) return tags->ti_Data;
    return fallback;
}
static void MESA3DGLReadRenderScale(void) {}
static struct Screen *LockPubScreen(const char *name) { (void)name; return &public_screen; }
static void UnlockPubScreen(const char *name, struct Screen *screen)
{ (void)name; assert(screen == &public_screen); }
static unsigned GetBitMapAttr(struct BitMap *bitmap, unsigned attr)
{ (void)attr; assert(bitmap); return bitmap->bytes * 8; }
static unsigned GetCyberMapAttr(struct BitMap *bitmap, unsigned attr)
{ (void)attr; assert(bitmap); return bitmap->bytes; }
static struct BitMap *AllocBitMap(LONG w, LONG h, unsigned depth,
    unsigned flags, struct BitMap *friend_bitmap)
{
    (void)flags; assert(w > 0 && h > 0 && friend_bitmap);
    if (fault == BITMAP) return NULL;
    struct BitMap *result = malloc(sizeof(*result)); assert(result);
    result->bytes = depth / 8; ++live_bitmap; return result;
}
static void FreeBitMap(struct BitMap *bitmap)
{ assert(live_bitmap); --live_bitmap; free(bitmap); }
static struct Layer_Info *NewLayerInfo(void)
{
    if (fault == LAYERINFO) return NULL;
    struct Layer_Info *result = calloc(1, sizeof(*result)); assert(result);
    ++live_info; return result;
}
static void DisposeLayerInfo(struct Layer_Info *info)
{ assert(live_info); --live_info; free(info); }
static struct Layer *CreateUpfrontLayer(struct Layer_Info *info,
    struct BitMap *bitmap, LONG x, LONG y, LONG right, LONG bottom,
    unsigned flags, void *unused)
{
    (void)flags; (void)unused; assert(info && bitmap);
    if (fault == LAYER) return NULL;
    struct Layer *result = calloc(1, sizeof(*result)); assert(result);
    result->rp = calloc(1, sizeof(*result->rp)); assert(result->rp);
    result->bounds = (struct Rectangle){x,y,right,bottom};
    result->rp->Layer = result; result->rp->BitMap = bitmap;
    ++live_layer; return result;
}
static void DeleteLayer(unsigned unused, struct Layer *layer)
{ (void)unused; assert(live_layer); --live_layer; free(layer->rp); free(layer); }
static struct RastPort *CloneRastPort(struct RastPort *original)
{
    assert(original);
    if (fault == CLONE) return NULL;
    struct RastPort *result = malloc(sizeof(*result)); assert(result);
    *result = *original; result->owned = 1; result->released = 0;
    if (fault == CLONE_LAYER) result->Layer = NULL;
    ++live_clone; return result;
}
static void FreeRastPort(struct RastPort *port)
{ assert(port && port->owned && live_clone); --live_clone; free(port); }
struct Library { unsigned lib_Version, lib_Revision; };
static struct Library gallium_library = {26, 1};
static struct Library *GalliumBase = &gallium_library;
static void ReleasePipeRastPort(void *driver, struct RastPort *port)
{
    assert(driver && port && port->owned && live_clone && !port->released);
    port->released = 1;
}
static void WaitBlit(void) {}
static void SetRPAttrsA(struct RastPort *port, struct TagItem *tags)
{ assert(port && tags); }
static void p_atomic_inc(unsigned *stamp) { ++*stamp; ++updates; }
static void MESA3DGLFreeTarget(struct mesa3dgl_drawable *d);
static void MESA3DGLCheckAndUpdateBufferSize(struct mesa3dgl_drawable *d);

/* SOURCE */

static void equal_live(unsigned b, unsigned i, unsigned l, unsigned c)
{ assert(live_bitmap == b && live_info == i && live_layer == l && live_clone == c); }
static void unchanged(struct mesa3dgl_context *ctx,
    const struct mesa3dgl_drawable *saved, const struct mesa3dgl_framebuffer *pixels,
    unsigned previous_updates)
{
    assert(memcmp(&ctx->own, saved, sizeof(*saved)) == 0);
    assert(memcmp(ctx->own.framebuffer, pixels, sizeof(*pixels)) == 0);
    assert(updates == previous_updates);
    equal_live(1,1,1,1);
}
int main(int argc, char **argv)
{
    struct mesa3dgl_framebuffer framebuffer = {.width=8,.height=8};
    struct mesa3dgl_context ctx = {0};
    struct TagItem initial[]={{GLA_Width,8},{GLA_Height,8},{TAG_DONE,0}};
    struct TagItem replacement[]={{GLA_Width,16},{GLA_Height,12},{TAG_DONE,0}};
    ctx.stvis.color_format = PIPE_FORMAT_B8G8R8A8_UNORM;
    ctx.own.framebuffer = &framebuffer;
    ctx.own.driver = &framebuffer;
    framebuffer.textures[0] = &public_bitmap;
    framebuffer.pixels[0] = 0x13579bdf;
    assert(glASetRast(&ctx, initial));
    equal_live(1,1,1,1);
    struct mesa3dgl_drawable saved = ctx.own;
    struct mesa3dgl_framebuffer saved_pixels = framebuffer;
    unsigned previous_updates = updates;
    for (fault=BITMAP; fault<=CLONE_LAYER; ++fault) {
        assert(!glASetRast(&ctx, replacement));
        unchanged(&ctx, &saved, &saved_pixels, previous_updates);
    }
    fault = NONE;
    struct Layer layer = {.bounds={0,0,31,31}};
    struct BitMap incompatible = {2};
    struct RastPort borrowed = {.BitMap=&incompatible,.Layer=&layer};
    struct Screen other_screen = {.RastPort=borrowed};
    struct TagItem mismatch[]={{GLA_Screen,(IPTR)&other_screen},{TAG_DONE,0}};
    assert(!glASetRast(&ctx, mismatch));
    unchanged(&ctx, &saved, &saved_pixels, previous_updates);
    /* Borrowed visible targets are never freed when clone preparation fails. */
    other_screen.RastPort.BitMap = &public_bitmap;
    for (fault=CLONE; fault<=CLONE_LAYER; ++fault) {
        assert(!glASetRast(&ctx, mismatch));
        unchanged(&ctx, &saved, &saved_pixels, previous_updates);
    }
    fault = NONE;
    if (argc > 1 && strcmp(argv[1], "geometry") == 0) {
        const struct TagItem rejected[]={{GLA_Left,17},{GLA_Top,13},
            {GLA_Right,17},{GLA_Bottom,13},{GLA_Left,(IPTR)-1},{GLA_Top,(IPTR)-1},
            {GLA_Left,16},{GLA_Top,12},{GLA_Right,16},{GLA_Bottom,12},
            {GLA_Left,(IPTR)1 << 32},{GLA_Top,(IPTR)1 << 32},
            {GLA_Right,(IPTR)1 << 32},{GLA_Bottom,(IPTR)1 << 32}};
        for (unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);++i) {
            struct TagItem bad[]={{GLA_Width,16},{GLA_Height,12},rejected[i],{TAG_DONE,0}};
            assert(!glASetRast(&ctx, bad));
            unchanged(&ctx, &saved, &saved_pixels, previous_updates);
        }
        const IPTR invalid_sizes[] = {0, (IPTR)-1, (IPTR)-2,
            (IPTR)1 << 32, ((IPTR)1 << 32) + 16};
        for (unsigned i=0;i<sizeof(invalid_sizes)/sizeof(invalid_sizes[0]);++i) {
            struct TagItem bad_width[]={{GLA_Width,invalid_sizes[i]},{GLA_Height,12},{TAG_DONE,0}};
            struct TagItem bad_height[]={{GLA_Width,16},{GLA_Height,invalid_sizes[i]},{TAG_DONE,0}};
            assert(!glASetRast(&ctx, bad_width));
            unchanged(&ctx, &saved, &saved_pixels, previous_updates);
            assert(!glASetRast(&ctx, bad_height));
            unchanged(&ctx, &saved, &saved_pixels, previous_updates);
        }
    }
    assert(glASetRast(&ctx, replacement));
    equal_live(1,1,1,1);
    assert(ctx.own.framebuffer == &framebuffer && ctx.own.driver == &framebuffer);
    assert(framebuffer.textures[0] == &public_bitmap && framebuffer.pixels[0] == 0x13579bdf);
    assert(framebuffer.width == 16 && framebuffer.height == 12);
    MESA3DGLFreeTarget(&ctx.own);
    equal_live(0,0,0,0);
    puts("GLA actual target preparation: HOST PASS");
    return 0;
}
