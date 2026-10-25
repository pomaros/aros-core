/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: Magnifier [LEFT n] [TOP n] [ZOOM n] - a window the size of the
          desktop's status widget, placed under it, that shows the screen
          around the mouse, enlarged. The - and + buttons change the factor, which is kept
          in ENVARC:Sys/Magnifier for the next start.

    The picture is read from the screen's bitmap with ReadPixelArray and
    enlarged by pixel repetition into a buffer the window receives with
    WritePixelArray. The mouse pointer is a sprite, never in the bitmap, so
    a crosshair marks where it is. It refreshes twenty times a second while
    the mouse moves and four times a second while it rests, and holds
    still while the mouse is over its own window.

    The size gadget resizes the window: the enlarged area follows the new
    inner size at the same factor, and the button row is the minimum. A
    screen scale change keeps the chosen size in 100% units.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/cybergraphics.h>
#include <aros/macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The status widget's outer size (coldstart_proof.c ALIVE_WIDTH and
 * ALIVE_HEIGHT) and its place: the magnifier sits right under it. */
#include "../include/widgetplace.h"
#include "../include/widgetstart.h"

#define WIDGET_WIDTH    WIDGET_COLUMN_WIDTH
#define WIDGET_HEIGHT   WIDGET_MAGNIFIER_HEIGHT

#define TEMPLATE        "LEFT/N,TOP/N,ZOOM/N"
#define PREFS_NAME      "Sys/Magnifier"
#define MOVING_MICROS   50000
#define RESTING_TICKS   5       /* a full refresh every 5 ticks at rest */
#define WAIT_SECONDS    60

/* RECTFMT_ARGB is A, R, G, B in memory order. */
#define RED             AROS_LONG2BE(0xffff0000)
#define WHITE           AROS_LONG2BE(0xffffffff)

#define GID_MINUS       1
#define GID_PLUS        2

static const UWORD factors[] = { 2, 3, 4, 6, 8, 12, 16 };
#define FACTOR_COUNT    (sizeof(factors) / sizeof(factors[0]))

struct Library *CyberGfxBase;
struct Library *GadToolsBase;

static int factor_index(UWORD zoom)
{
    int i, best = 0;

    for (i = 0; i < (int)FACTOR_COUNT; ++i)
        if (factors[i] <= zoom)
            best = i;
    return best;
}

static UWORD saved_zoom(void)
{
    char text[8];

    if (GetVar(PREFS_NAME, text, sizeof(text), GVF_GLOBAL_ONLY) > 0)
        return (UWORD)atoi(text);
    return 4;
}

static void save_zoom(UWORD zoom)
{
    char text[8];
    LONG length = snprintf(text, sizeof(text), "%u", zoom);
    BPTR file;

    SetVar(PREFS_NAME, text, length, GVF_GLOBAL_ONLY);
    file = Open("ENVARC:" PREFS_NAME, MODE_NEWFILE);
    if (file) {
        Write(file, text, length);
        Close(file);
    }
}

struct view {
    struct Screen *screen;
    struct Window *window;
    const struct ScreenRenderContext *context;
    struct Gadget *gadgets, *label;
    APTR visual;
    ULONG scale;
    LONG raster_width, raster_height;
    ULONG *source;          /* the screen region, ARGB */
    ULONG *picture;         /* the enlarged region, ARGB */
    ULONG capacity;         /* pixels each of source and picture can hold */
    UWORD x, y, width, height;      /* picture area in the window */
    WORD mx, my;            /* the point last drawn, redrawn after a resize */
    UWORD bar;              /* height of the button row */
    int index;              /* into factors[] */
    char *label_text;
    struct WidgetPlace place;
};

/* Read the region around (mx, my), enlarge it and show it. */
static void draw(struct view *v, WORD mx, WORD my)
{
    UWORD zoom = (factors[v->index] * v->scale + 50) / 100;
    UWORD sw = (v->width + zoom - 1) / zoom, sh = (v->height + zoom - 1) / zoom;
    LONG sx = mx - sw / 2, sy = my - sh / 2;
    LONG cx, cy;
    UWORD row, col;

    v->mx = mx;
    v->my = my;
    if (!v->width || !v->height)
        return;
    if (sx < 0)
        sx = 0;
    if (sy < 0)
        sy = 0;
    if (sx + sw > v->raster_width)
        sx = v->raster_width - sw;
    if (sy + sh > v->raster_height)
        sy = v->raster_height - sh;

    ReadPixelArray(v->source, 0, 0, sw * 4, &v->screen->RastPort, sx, sy, sw, sh,
                   RECTFMT_ARGB);

    for (row = 0; row < v->height; ++row) {
        const ULONG *src = v->source + (row / zoom) * sw;
        ULONG *dst = v->picture + row * v->width;

        for (col = 0; col < v->width; ++col)
            dst[col] = src[col / zoom];
    }

    /* The crosshair: where the pointer's hot spot is in the picture. */
    cx = (mx - sx) * zoom + zoom / 2;
    cy = (my - sy) * zoom + zoom / 2;
    if (cx >= 0 && cx < v->width && cy >= 0 && cy < v->height) {
        LONG d;

        LONG reach = (12 * v->scale + 50) / 100;
        LONG gap = (4 * v->scale + 50) / 100;
        LONG thickness = (v->scale + 50) / 100;
        for (d = -reach; d <= reach; ++d) {
            if (d > -gap && d < gap)
                continue;
            for (LONG stroke = 0; stroke < thickness; ++stroke) {
                if (cx + d >= 0 && cx + d < v->width) {
                    if (cy + stroke < v->height)
                        v->picture[(cy + stroke) * v->width + cx + d] = RED;
                    if (cy + thickness + stroke < v->height)
                        v->picture[(cy + thickness + stroke) * v->width + cx + d] = WHITE;
                }
                if (cy + d >= 0 && cy + d < v->height) {
                    if (cx + stroke < v->width)
                        v->picture[(cy + d) * v->width + cx + stroke] = RED;
                    if (cx + thickness + stroke < v->width)
                        v->picture[(cy + d) * v->width + cx + thickness + stroke] = WHITE;
                }
            }
        }
    }

    WritePixelArray(v->picture, 0, 0, v->width * 4, v->window->RPort, v->x, v->y,
                    v->width, v->height, RECTFMT_ARGB);
}

static void show_factor(struct view *v)
{
    snprintf(v->label_text, 8, "%uX", factors[v->index]);
    GT_SetGadgetAttrs(v->label, v->window, NULL, GTTX_Text, (IPTR)v->label_text, TAG_DONE);
}


/* The sampled raster and this aware window use physical pixels from one
 * immutable render generation, independent of logical viewport reporting. */
static const struct ScreenRenderContext *current_context(struct Screen *screen)
{
    IPTR value = 0;
    return GetAttr(SA_RenderContext, (Object *)screen, &value) ?
        (const struct ScreenRenderContext *)value : NULL;
}

static LONG pixels(const struct view *v, LONG logical)
{
    return (logical * v->scale + 50) / 100;
}

/* The minimum outer size: the complete button row over one sampled row. */
static void view_minimum(const struct view *v, LONG *width, LONG *height)
{
    *width = v->window->BorderLeft + v->window->BorderRight + pixels(v, 156);
    *height = v->window->BorderTop + v->window->BorderBottom + v->bar + pixels(v, 1);
}

/* Fit the picture area to the window's inner size. A larger area needs
 * larger buffers; when they cannot be had the old ones stay and only the
 * part of the area they cover is drawn, the rest erased. */
static BOOL layout_view(struct view *v)
{
    struct Window *w = v->window;
    LONG width = w->Width - v->x - w->BorderRight;
    LONG height = w->Height - v->y - w->BorderBottom;
    BOOL complete = TRUE;

    if (width < 0) width = 0;
    if (height < 0) height = 0;
    if ((ULONG)width * (ULONG)height > v->capacity) {
        ULONG *source = AllocVec((ULONG)width * height * 4, MEMF_ANY);
        ULONG *picture = source ? AllocVec((ULONG)width * height * 4, MEMF_ANY) : NULL;

        if (picture) {
            FreeVec(v->source);
            FreeVec(v->picture);
            v->source = source;
            v->picture = picture;
            v->capacity = (ULONG)width * height;
        } else {
            if (source) FreeVec(source);
            complete = FALSE;
        }
    }
    if (!complete) {
        LONG full_width = width, full_height = height;

        if (width > v->width) width = v->width;
        if (height > v->height) height = v->height;
        SetAPen(w->RPort, v->context->src_PhysicalDrawInfo->dri_Pens[BACKGROUNDPEN]);
        if (full_width > width)
            RectFill(w->RPort, v->x + width, v->y, v->x + full_width - 1, v->y + full_height - 1);
        if (full_height > height && width > 0)
            RectFill(w->RPort, v->x, v->y + height, v->x + width - 1, v->y + full_height - 1);
    }
    v->width = width;
    v->height = height;
    return complete;
}

/* IDCMP_NEWSIZE: the new area at once, around the point last shown. */
static void resize_view(struct view *v)
{
    layout_view(v);
    draw(v, v->mx, v->my);
}

static void close_view(struct view *v)
{
    if (v->window) CloseWindow(v->window);
    if (v->gadgets) FreeGadgets(v->gadgets);
    if (v->visual) FreeVisualInfo(v->visual);
    if (v->picture) FreeVec(v->picture);
    if (v->source) FreeVec(v->source);
    if (v->label_text) FreeVec(v->label_text);
    v->window = NULL;
    v->gadgets = v->label = NULL;
    v->visual = NULL;
    v->picture = v->source = NULL;
    v->capacity = 0;
    v->label_text = NULL;
}

/* At v->place: the Magnifier slot at the screen's current scale, or the
 * user's geometry in 100 % units (a size of 0 is the slot's design size). */
static BOOL open_view(struct view *v)
{
    struct ScaleWindowGeometry geometry;
    struct WidgetRect place;
    struct Gadget *gad;
    struct NewGadget ng;
    struct TTextAttr fontattr;
    struct TextFont *font;
    LONG fh, min_width, min_height;
    int attempt;
    struct TagItem visual_tags[] = {
        { GTVI_RenderContext, 0 }, { TAG_DONE, 0 }
    };
    v->context = current_context(v->screen);
    if (!v->context ||
        !WidgetPlaceRect(&v->place, v->screen, v->context, WIDGET_SLOT_MAGNIFIER, &place))
        return FALSE;
    v->label_text = AllocVec(8, MEMF_ANY);
    if (!v->label_text) goto fail;
    snprintf(v->label_text, 8, "%uX", factors[v->index]);
    v->scale = v->context->src_Scale;
    v->raster_width = GetBitMapAttr(v->screen->RastPort.BitMap, BMA_WIDTH);
    v->raster_height = GetBitMapAttr(v->screen->RastPort.BitMap, BMA_HEIGHT);
    visual_tags[0].ti_Data = (IPTR)v->context;
    v->visual = GetVisualInfoA(v->screen, visual_tags);
    if (!v->visual) goto fail;
    geometry.Left = place.Left;
    geometry.Top = place.Top;
    geometry.Width = place.Width > 0 ? place.Width : WIDGET_WIDTH;
    geometry.Height = place.Height > 0 ? place.Height : WIDGET_HEIGHT;
    /* Establish viewport minima from the actual hidden window frame below.
     * Explicit positive limits avoid OpenWindow's initial-size defaults. */
    geometry.MinWidth = geometry.MinHeight = 1;
    geometry.MaxWidth = geometry.MaxHeight = -1;
    {
        struct TagItem tags[] = {
            { WA_Title, (IPTR)"Magnifier" },
            { WA_PubScreen, (IPTR)v->screen },
            { WA_ScaleAware, TRUE }, { WA_ScaleGeometry, (IPTR)&geometry },
            { WA_RenderContext, (IPTR)v->context }, { WA_Hidden, TRUE },
            { WA_Activate, FALSE },
            { WA_SizeGadget, TRUE }, { WA_SizeBBottom, TRUE },
            { WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                WFLG_SMART_REFRESH | WFLG_RMBTRAP },
            { WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW |
                IDCMP_NEWSIZE | IDCMP_CHANGEWINDOW | IDCMP_CHANGESCALE | BUTTONIDCMP },
            { TAG_DONE, 0 }
        };
        /* A size chosen at another scale can round below this frame's
         * measured minimum: then open once more at the minimum. */
        font = v->context->src_PhysicalDrawInfo->dri_Font;
        fh = font->tf_YSize;
        v->bar = fh + pixels(v, 10);
        for (attempt = 0; ; ++attempt) {
            v->window = OpenWindowTagList(NULL, tags);
            if (!v->window) goto fail;
            view_minimum(v, &min_width, &min_height);
            if (v->window->Width >= min_width && v->window->Height >= min_height) break;
            if (attempt) goto fail;
            if (v->window->Width < min_width)
                geometry.Width = (min_width * 100 + v->scale - 1) / v->scale;
            if (v->window->Height < min_height)
                geometry.Height = (min_height * 100 + v->scale - 1) / v->scale;
            CloseWindow(v->window);
            v->window = NULL;
        }
    }
    fontattr.tta_Name = font->tf_Message.mn_Node.ln_Name;
    fontattr.tta_YSize = font->tf_YSize;
    fontattr.tta_Style = font->tf_Style & ~FSF_TAGGED;
    fontattr.tta_Flags = font->tf_Flags;
    fontattr.tta_Tags = NULL;
    if ((font->tf_Style & FSF_TAGGED) && font->tf_Extension) {
        fontattr.tta_Tags = ((struct TextFontExtension *)font->tf_Extension)->tfe_Tags;
        if (fontattr.tta_Tags) fontattr.tta_Style |= FSF_TAGGED;
    }
    /* Keep the complete navigation row and a positive sampled viewport.
     * WindowLimits records these measured physical minima in the canonical
     * geometry at this context's scale, before anything can be shown; the
     * maximum is the screen. */
    if (!WindowLimits(v->window, min_width, min_height, (UWORD)-1, (UWORD)-1)) goto fail;
    WidgetPlaceOpened(&v->place, v->window);
    v->x = v->window->BorderLeft;
    v->y = v->window->BorderTop + v->bar;
    if (v->window->Width <= v->x + v->window->BorderRight ||
        v->window->Height <= v->y + v->window->BorderBottom) goto fail;
    v->width = v->window->Width - v->x - v->window->BorderRight;
    v->height = v->window->Height - v->y - v->window->BorderBottom;
    /* Only the sampled viewport shrinks at a panel edge; controls retain
     * scaled metrics and must fit completely before the window is shown. */
    if (v->width < pixels(v, 156)) goto fail;
    memset(&ng, 0, sizeof(ng));
    gad = CreateContext(&v->gadgets);
    if (!gad) goto fail;
    /* TEXT_KIND opens its own font. Supply the complete captured attributes,
     * including DPI tags; its implicit default loses style/tag information. */
    ng.ng_TextAttr = (struct TextAttr *)&fontattr;
    ng.ng_VisualInfo = v->visual;
    ng.ng_TopEdge = v->window->BorderTop + pixels(v, 4);
    ng.ng_Height = fh + pixels(v, 4);
    ng.ng_Width = pixels(v, 40);
    ng.ng_LeftEdge = v->x + pixels(v, 4);
    ng.ng_GadgetText = (UBYTE *)"-";
    ng.ng_GadgetID = GID_MINUS;
    gad = CreateGadgetA(BUTTON_KIND, gad, &ng, NULL);
    if (!gad) goto fail;
    ng.ng_LeftEdge = v->x + pixels(v, 52);
    ng.ng_Width = pixels(v, 48);
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = 0;
    {
        struct TagItem tags[] = {
            { GTTX_Border, TRUE }, { GTTX_Justification, GTJ_CENTER },
            { GTTX_Text, (IPTR)v->label_text }, { TAG_DONE, 0 }
        };
        gad = CreateGadgetA(TEXT_KIND, gad, &ng, tags);
    }
    if (!gad) goto fail;
    v->label = gad;
    ng.ng_LeftEdge = v->x + pixels(v, 108);
    ng.ng_Width = pixels(v, 40);
    ng.ng_GadgetText = (UBYTE *)"+";
    ng.ng_GadgetID = GID_PLUS;
    if (!CreateGadgetA(BUTTON_KIND, gad, &ng, NULL)) goto fail;
    v->source = AllocVec((ULONG)v->width * v->height * 4, MEMF_ANY);
    v->picture = AllocVec((ULONG)v->width * v->height * 4, MEMF_ANY);
    if (!v->source || !v->picture) goto fail;
    v->capacity = (ULONG)v->width * v->height;
    AddGList(v->window, v->gadgets, -1, -1, NULL);
    GT_RefreshWindow(v->window, NULL);
    show_factor(v);
    draw(v, v->screen->MouseX, v->screen->MouseY);
    return TRUE;
fail:
    close_view(v);
    return FALSE;
}

/* A new view at the current context, in its slot at the new scale or at
 * the user's own geometry: never the place Intuition's refit chose. */
static BOOL replace_view(struct view *v)
{
    struct view next;
    BOOL was_active;

    memset(&next, 0, sizeof(next));
    next.screen = v->screen;
    next.index = v->index;
    WidgetPlaceCopy(&next.place, &v->place);
    if (!open_view(&next) || !ShowWindow(next.window, NULL)) {
        close_view(&next);
        return FALSE;
    }
    was_active = (v->window->Flags & WFLG_WINDOWACTIVE) != 0;
    close_view(v);
    *v = next;
    if (was_active) ActivateWindow(v->window);
    return TRUE;
}

int main(void)
{
    IPTR args[3] = { 0, 0, 0 };
    struct RDArgs *rda;
    struct view v;
    struct MsgPort *timer_port = NULL;
    struct timerequest *timer = NULL;
    BOOL timer_open = FALSE, running = TRUE;
    WORD last_x = -1, last_y = -1;
    ULONG rest = 0;
    int rc = RETURN_FAIL;

    memset(&v, 0, sizeof(v));
    rda = ReadArgs(TEMPLATE, args, NULL);
    if (!rda) {
        PrintFault(IoErr(), "Magnifier");
        return RETURN_ERROR;
    }
    /* LEFT and TOP are 100 % units and make the place the user's. */
    WidgetPlaceAutomatic(&v.place);
    if (args[0] || args[1])
        WidgetPlaceUser(&v.place, args[0] ? *(LONG *)args[0] : 0,
            args[1] ? *(LONG *)args[1] : 20, 0, 0);
    v.index = factor_index(args[2] ? (UWORD)*(LONG *)args[2] : saved_zoom());

    CyberGfxBase = OpenLibrary("cybergraphics.library", 41);
    GadToolsBase = OpenLibrary("gadtools.library", 39);
    if (!CyberGfxBase || !GadToolsBase) {
        Printf("Magnifier: needs cybergraphics.library and gadtools.library\n");
        goto out;
    }
    /* Started before the desktop: open only once the kernel has accepted
     * it (widgetstart.h), at the slot of widgetplace.h. */
    v.screen = WidgetWaitForDesktop(WAIT_SECONDS);
    if (!v.screen)
        goto out;
    if (!open_view(&v) || !ShowWindow(v.window, NULL))
        goto out;

    timer_port = CreateMsgPort();
    timer = timer_port ? (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer)) : NULL;
    if (!timer || OpenDevice("timer.device", UNIT_MICROHZ, &timer->tr_node, 0) != 0)
        goto out;
    timer_open = TRUE;
    timer->tr_node.io_Command = TR_ADDREQUEST;
    timer->tr_time.tv_secs = 0;
    timer->tr_time.tv_micro = MOVING_MICROS;
    SendIO(&timer->tr_node);

    rc = RETURN_OK;
    while (running) {
        ULONG wsig = 1UL << v.window->UserPort->mp_SigBit;
        ULONG tsig = 1UL << timer_port->mp_SigBit;
        ULONG got = Wait(wsig | tsig | SIGBREAKF_CTRL_C);
        struct IntuiMessage *msg;
        BOOL relayout = FALSE;

        if (got & SIGBREAKF_CTRL_C)
            running = FALSE;
        while ((msg = GT_GetIMsg(v.window->UserPort))) {
            ULONG class = msg->Class;
            struct Gadget *g = (struct Gadget *)msg->IAddress;

            GT_ReplyIMsg(msg);
            if (class == IDCMP_CLOSEWINDOW)
                running = FALSE;
            else if (class == IDCMP_CHANGESCALE)
                relayout = TRUE;
            else if (class == IDCMP_NEWSIZE || class == IDCMP_CHANGEWINDOW) {
                if (current_context(v.screen) == v.context) {
                    /* The user's move or size: kept at every later scale. */
                    WidgetPlaceChanged(&v.place, v.window, v.scale);
                    if (class == IDCMP_NEWSIZE)
                        resize_view(&v);
                } else
                    relayout = TRUE;
            } else if (class == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(v.window);
                if (current_context(v.screen) == v.context)
                    draw(&v, v.mx, v.my);
                GT_EndRefresh(v.window, TRUE);
                last_x = -1;
            } else if (class == IDCMP_GADGETUP) {
                int before = v.index;

                if (g->GadgetID == GID_MINUS && v.index > 0)
                    --v.index;
                else if (g->GadgetID == GID_PLUS && v.index + 1 < (int)FACTOR_COUNT)
                    ++v.index;
                if (v.index != before) {
                    show_factor(&v);
                    save_zoom(factors[v.index]);
                    last_x = -1;
                }
            }
        }
        /* The private application's new view is complete and generation-bound
         * before publication. A refused/OOM replacement retains the old view;
         * the timer retries, including same-scale base-font changes. */
        if (running && (relayout || current_context(v.screen) != v.context) &&
                replace_view(&v))
            last_x = last_y = -1;
        if (got & tsig) {
            WORD mx = v.screen->MouseX, my = v.screen->MouseY;

            struct Window *w = v.window;
            BOOL inside = mx >= w->LeftEdge && mx < w->LeftEdge + w->Width &&
                my >= w->TopEdge && my < w->TopEdge + w->Height;

            WaitIO(&timer->tr_node);
            /* Over its own window the picture holds still: it would enlarge
             * itself, and the buttons are clicked there. */
            if (inside)
                rest = 0;
            else if (mx != last_x || my != last_y || ++rest >= RESTING_TICKS) {
                draw(&v, mx, my);
                last_x = mx;
                last_y = my;
                rest = 0;
            }
            timer->tr_time.tv_secs = 0;
            timer->tr_time.tv_micro = MOVING_MICROS;
            SendIO(&timer->tr_node);
        }
    }

out:
    if (timer_open) {
        if (!CheckIO(&timer->tr_node))
            AbortIO(&timer->tr_node);
        WaitIO(&timer->tr_node);
        CloseDevice(&timer->tr_node);
    }
    if (timer)
        DeleteIORequest((struct IORequest *)timer);
    if (timer_port)
        DeleteMsgPort(timer_port);
    close_view(&v);
    if (v.screen)
        UnlockPubScreen(NULL, v.screen);
    if (GadToolsBase)
        CloseLibrary(GadToolsBase);
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    FreeArgs(rda);
    return rc;
}
