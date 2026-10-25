/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Real unchanged GadTools clients use both explicit public Screen.Font and
    default GetVisualInfo/DrawInfo fonts. Readback is physical screen pixels.
*/
#ifndef SCALELEGACY_GADTOOLS_H
#define SCALELEGACY_GADTOOLS_H
#include <libraries/gadtools.h>
#include <proto/gadtools.h>

struct Library *GadToolsBase;

#define GT_ORACLE_WIDTH 112
#define GT_ORACLE_HEIGHT 88

struct GTOracleClient {
    APTR visual;
    struct Gadget *list;
    struct Window *window;
};

static BOOL gt_oracle_open(struct GTOracleClient *client, struct Screen *screen,
                           WORD left, UWORD base)
{
    static STRPTR labels[] = {(STRPTR)"Legacy", NULL};
    struct NewGadget ng;
    struct Gadget *previous;
    struct TagItem tags[] = {{GTCY_Labels, (IPTR)labels}, {TAG_DONE, 0}};
    ULONG i;
    client->visual = GetVisualInfoA(screen, NULL);
    if (!client->visual) return FALSE;
    previous = CreateContext(&client->list);
    if (!previous) return FALSE;
    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = 16;
    ng.ng_Width = 96;
    ng.ng_Height = base + 8;
    ng.ng_GadgetText = (STRPTR)"";
    ng.ng_VisualInfo = client->visual;
    for (i = 0; i < 2; ++i)
    {
        ng.ng_TopEdge = 16 + 40 * i;
        ng.ng_TextAttr = i ? NULL : screen->Font;
        ng.ng_GadgetID = 701 + i;
        previous = CreateGadgetA(CYCLE_KIND, previous, &ng, tags);
        if (!previous) return FALSE;
    }
    /* No awareness opt-in or scale notification: ordinary legacy application. */
    client->window = OpenWindowTags(NULL,
        WA_CustomScreen, (IPTR)screen, WA_Left, left, WA_Top, 112,
        WA_Width, 128, WA_Height, 104, WA_Borderless, TRUE,
        WA_NoCareRefresh, TRUE, WA_Gadgets, (IPTR)client->list, TAG_DONE);
    if (!client->window) return FALSE;
    SetRast(client->window->RPort, 0);
    RefreshGList(client->list, client->window, NULL, -1);
    GT_RefreshWindow(client->window, NULL);
    return TRUE;
}

static void gt_oracle_close(struct GTOracleClient *client)
{
    if (client->window) CloseWindow(client->window);
    if (client->list) FreeGadgets(client->list);
    if (client->visual) FreeVisualInfo(client->visual);
}

static ULONG gt_oracle_compare(struct Screen *screen, struct Window *window,
                              ULONG factor, const UBYTE *reference)
{
    UBYTE row[GT_ORACLE_WIDTH * 2 * 3];
    ULONG x, y, bad = 0;
    for (y = 0; y < GT_ORACLE_HEIGHT * factor; ++y)
    {
        if (ReadPixelArray(row, 0, 0, GT_ORACLE_WIDTH * factor * 3,
            &screen->RastPort, (window->LeftEdge + 8) * factor,
            (window->TopEdge + 8) * factor + y,
            GT_ORACLE_WIDTH * factor, 1, RECTFMT_RGB) != GT_ORACLE_WIDTH * factor)
            return (ULONG)~0U;
        for (x = 0; x < GT_ORACLE_WIDTH * factor; ++x)
            if (memcmp(row + x * 3,
                reference + ((y / factor) * GT_ORACLE_WIDTH + x / factor) * 3, 3)) ++bad;
    }
    return bad;
}

/* Reject a blank label independently for explicit and default-font cycles.
 * The interior excludes the frame, separator and cycle arrow. */
static BOOL gt_oracle_label(const UBYTE *reference, ULONG cycle, UWORD base)
{
    ULONG x, y, changed = 0;
    const UBYTE *background = reference + ((10 + cycle * 40) * GT_ORACLE_WIDTH + 12) * 3;
    for (y = 10 + cycle * 40; y < 10 + cycle * 40 + base + 4; ++y)
        for (x = 12; x < 80; ++x)
            if (memcmp(background, reference + (y * GT_ORACLE_WIDTH + x) * 3, 3)) ++changed;
    return changed >= 4;
}

static BOOL gadtools_fonts(struct Screen *screen)
{
    struct GTOracleClient clients[2] = {{0}, {0}};
    struct Window *aware = NULL;
    struct DrawInfo *logical = NULL, *physical = NULL;
    UBYTE *reference = NULL;
    const struct ScreenRenderContext *context = NULL;
    IPTR context_value = 0, scale = 0;
    UWORD base = 0;
    ULONG public100 = 0, public200 = 0, draw100 = 0, draw200 = 0;
    ULONG physical200 = 0, raster200 = 0, aware_font = 0;
    ULONG existing_bad = ~0U, born_bad = ~0U, reset_bad = ~0U;
    ULONG nonblank = 0, reset_public = 0, reset_draw = 0, reset_raster = 0;
    BOOL good = FALSE;
    /* Fits both 640x480 and native; no overlap with the old five-phase ROIs. */
    if (!screen || screen->Width < 640 || screen->Height < 480)
        goto cleanup;
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 39);
    if (!GadToolsBase) goto cleanup;
    reference = AllocVec(GT_ORACLE_WIDTH * GT_ORACLE_HEIGHT * 3, MEMF_PUBLIC);
    if (!reference) goto cleanup;
    SetAttrs((Object *)screen, SA_Scale, 100, TAG_DONE);
    GetAttr(SA_RenderContext, (Object *)screen, &context_value);
    context = (const struct ScreenRenderContext *)context_value;
    if (!context) goto cleanup;
    scale = context->src_Scale;
    if (scale != 100) goto cleanup;
    /* SA_SysFont may give old window content another font (e.g. Topaz8).
     * GadTools screen defaults form an independent coordinate domain. */
    public100 = screen->Font ? screen->Font->ta_YSize : 0;
    base = public100;
    logical = GetScreenDrawInfo(screen);
    draw100 = logical && logical->dri_Font ? logical->dri_Font->tf_YSize : 0;
    if (logical) FreeScreenDrawInfo(screen, logical);
    logical = NULL;
    if (base < 6 || base > 20 || draw100 != base || !gt_oracle_open(&clients[0], screen, 16, base))
        goto cleanup;
    Delay(10);
    if (ReadPixelArray(reference, 0, 0, GT_ORACLE_WIDTH * 3, &screen->RastPort,
        clients[0].window->LeftEdge + 8, clients[0].window->TopEdge + 8,
        GT_ORACLE_WIDTH, GT_ORACLE_HEIGHT, RECTFMT_RGB) != GT_ORACLE_WIDTH * GT_ORACLE_HEIGHT)
        goto cleanup;
    nonblank = gt_oracle_label(reference, 0, base) && gt_oracle_label(reference, 1, base);
    if (!nonblank) goto cleanup;
    SetAttrs((Object *)screen, SA_Scale, 200, TAG_DONE);
    GetAttr(SA_RenderContext, (Object *)screen, &context_value);
    context = (const struct ScreenRenderContext *)context_value;
    if (!context) goto cleanup;
    scale = context->src_Scale;
    public200 = screen->Font ? screen->Font->ta_YSize : 0;
    logical = GetScreenDrawInfo(screen);
    draw200 = logical && logical->dri_Font ? logical->dri_Font->tf_YSize : 0;
    if (logical) FreeScreenDrawInfo(screen, logical);
    logical = NULL;
    physical = context->src_PhysicalDrawInfo; /* immutable screen-lifetime borrow */
    physical200 = physical && physical->dri_Font ? physical->dri_Font->tf_YSize : 0;
    raster200 = screen->RastPort.Font ? screen->RastPort.Font->tf_YSize : 0;
    if (scale != 200 || public200 != base || draw200 != base ||
        physical200 != base * 2 || raster200 != base * 2) goto cleanup;
    /* GT_RefreshWindow is a no-op on AROS; RefreshGList forces GM_RENDER. */
    SetRast(clients[0].window->RPort, 0);
    RefreshGList(clients[0].list, clients[0].window, NULL, -1);
    GT_RefreshWindow(clients[0].window, NULL);
    Delay(10);
    existing_bad = gt_oracle_compare(screen, clients[0].window, 2, reference);
    if (!gt_oracle_open(&clients[1], screen, 160, base)) goto cleanup;
    Delay(10);
    born_bad = gt_oracle_compare(screen, clients[1].window, 2, reference);
    aware = OpenWindowTags(NULL, WA_CustomScreen, (IPTR)screen,
        WA_Left, 320, WA_Top, 32, WA_Width, 128, WA_Height, 64,
        WA_Borderless, TRUE, WA_ScaleAware, TRUE, WA_NoCareRefresh, TRUE, TAG_DONE);
    if (!aware) goto cleanup;
    aware_font = aware->RPort && aware->RPort->Font ? aware->RPort->Font->tf_YSize : 0;
    CloseWindow(aware); aware = NULL;
    /* Close the born-at200 client before reset could move it over reference. */
    gt_oracle_close(&clients[1]); memset(&clients[1], 0, sizeof(clients[1]));
    SetAttrs((Object *)screen, SA_Scale, 100, TAG_DONE);
    GetAttr(SA_RenderContext, (Object *)screen, &context_value);
    context = (const struct ScreenRenderContext *)context_value;
    if (!context) goto cleanup;
    scale = context->src_Scale;
    reset_public = screen->Font ? screen->Font->ta_YSize : 0;
    logical = GetScreenDrawInfo(screen);
    reset_draw = logical && logical->dri_Font ? logical->dri_Font->tf_YSize : 0;
    if (logical) FreeScreenDrawInfo(screen, logical);
    logical = NULL;
    reset_raster = screen->RastPort.Font ? screen->RastPort.Font->tf_YSize : 0;
    SetRast(clients[0].window->RPort, 0);
    RefreshGList(clients[0].list, clients[0].window, NULL, -1);
    GT_RefreshWindow(clients[0].window, NULL);
    Delay(10);
    reset_bad = gt_oracle_compare(screen, clients[0].window, 1, reference);
    good = !existing_bad && !born_bad && !reset_bad && aware_font == base * 2 &&
        scale == 100 && reset_public == base && reset_draw == base && reset_raster == base;
cleanup:
    if (logical) FreeScreenDrawInfo(screen, logical);
    if (aware) CloseWindow(aware);
    gt_oracle_close(&clients[1]);
    gt_oracle_close(&clients[0]);
    if (reference) FreeVec(reference);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    GadToolsBase = NULL;
    report("[SCALE-LEGACY-GADTOOLS] result=%s base-font=%u public100=%u draw100=%u public200=%u draw200=%u physical200=%u raster200=%u aware-font=%u existing-bad=%u born-bad=%u reset-bad=%u nonblank=%u reset-public=%u reset-draw=%u reset-raster=%u cycles=2\n",
        good ? "PASS" : "FAIL", (ULONG)base, public100, draw100, public200, draw200,
        physical200, raster200, aware_font, existing_bad, born_bad, reset_bad, nonblank,
        reset_public, reset_draw, reset_raster);
    return good;
}
#endif
