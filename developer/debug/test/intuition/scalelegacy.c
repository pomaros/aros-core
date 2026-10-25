/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Runtime composition oracle. The client uses ordinary window drawing only.
    Readback is from the physical screen, never from Intuition private state.
    HOLD seconds leaves each phase available for an external pointer click;
    CLICKS requires a correct gadget release in every phase. An omitted CLICKS
    tests pixels only and explicitly reports input as unmeasured. STATUS names
    a per-run file rewritten and closed before each HOLD and on completion;
    exporting this file never requires reading an open stdout stream.
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <cybergraphx/cybergraphics.h>
#include <dos/rdargs.h>
#include <aros/debug.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define PW 32
#define PH 16
#define PX 8
#define PY 8
#define TW 64
#define TH 24
#define TX 8
#define TY 24

/* Debug serial drives the VM capture harness. Flushed stdout retains the
 * same phase records through native USB FileExport while the probe is alive. */
static void report(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void report(const char *format, ...)
{
    char line[384];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    bug("%s", line);
    fputs(line, stdout);
    fflush(stdout);
}

/* A failed write still closes its handle. A snapshot is usable only after
   this function returns TRUE; a close failure makes the final verdict FAIL. */
static BOOL status_snapshot(CONST_STRPTR path, const char *state, ULONG phase,
                            const char *format, ...)
{
    char line[512];
    va_list arguments;
    int length;
    BPTR file;
    LONG written, error = 0;
    BOOL closed;
    const char *reason;
    if (!path) return TRUE;
    va_start(arguments, format);
    length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (length < 0 || (ULONG)length >= sizeof(line))
    {
        reason = "format";
        goto failed;
    }
    file = Open(path, MODE_NEWFILE);
    if (!file)
    {
        error = IoErr();
        reason = "open";
        goto failed;
    }
    written = Write(file, line, length);
    if (written != length) error = IoErr();
    closed = Close(file);
    if (!closed)
    {
        error = IoErr();
        reason = "close";
        goto failed;
    }
    if (written != length)
    {
        reason = "write";
        goto failed;
    }
    return TRUE;
failed:
    report("[SCALE-LEGACY-STATUS-ERROR] state=%s phase=%u filename=%s reason=%s ioerr=%d\n",
        state, phase, path, reason, (int)error);
    return FALSE;
}

/* A capture task may briefly borrow the public screen. Never let this
 * module unload while a public node, visitor or screen still references it. */
static void oracle_retain(const char *reason)
{
    report("[SCALE-LEGACY-PUBLIC] result=FAIL name=ScaleLegacyOracle reason=%s retained=1\n", reason);
    for (;;) Wait(0);
}

static void oracle_make_private(struct Screen *screen)
{
    ULONG tick;
    for (tick = 0; tick < 250; ++tick)
    {
        if (PubScreenStatus(screen, PSNF_PRIVATE) & 1) return;
        Delay(1);
    }
    oracle_retain("visitor-timeout");
}

static ULONG compare(struct Screen *screen, struct Window *win, ULONG factor,
                     const UBYTE *reference)
{
    UBYTE row[PW * 4 * 3];
    ULONG x, y, bad = 0;
    for (y = 0; y < PH * factor; ++y)
    {
        if (ReadPixelArray(row, 0, 0, PW * factor * 3, &screen->RastPort,
            (win->LeftEdge + PX) * factor, (win->TopEdge + PY) * factor + y,
            PW * factor, 1, RECTFMT_RGB) != PW * factor)
            return (ULONG)~0U;
        for (x = 0; x < PW * factor; ++x)
            if (memcmp(row + x * 3, reference + ((y / factor) * PW + x / factor) * 3, 3))
                ++bad;
    }
    return bad;
}

static ULONG compare_text(struct Screen *screen, struct Window *win, ULONG factor,
                          const UBYTE *reference)
{
    UBYTE row[TW * 4 * 3];
    ULONG x, y, bad = 0;
    for (y = 0; y < TH * factor; ++y)
    {
        if (ReadPixelArray(row, 0, 0, TW * factor * 3, &screen->RastPort,
            (win->LeftEdge + TX) * factor, (win->TopEdge + TY) * factor + y,
            TW * factor, 1, RECTFMT_RGB) != TW * factor)
            return (ULONG)~0U;
        for (x = 0; x < TW * factor; ++x)
            if (memcmp(row + x * 3,
                reference + ((y / factor) * TW + x / factor) * 3, 3)) ++bad;
    }
    return bad;
}

#include "scalelegacy_copy.h"
#include "scalelegacy_gadtools.h"

struct LegacyPixels {
    UBYTE reference[PW * PH * 3], aware_pixels[PW * PH * 3];
    UBYTE text_reference[TW * TH * 3];
};

int main(void)
{
    SIPTR args[3] = {0};
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)"HOLD/N/K,CLICKS/S,STATUS/K", args, NULL);
    CONST_STRPTR status = (CONST_STRPTR)args[2];
    struct Screen *screen = NULL;
    struct Window *legacy = NULL, *aware = NULL;
    struct Gadget gadget;
    struct LegacyPixels *pixels = NULL;
    UBYTE *reference, *aware_pixels, *text_reference;
    ULONG phases[] = {100, 200, 300, 400, 100};
    LONG hold = args[0] ? *(LONG *)args[0] : 0;
    ULONG phase, x, y, failures = 0;
    UWORD legacy_font_height = 0;
    if (!rda) return RETURN_FAIL;
    if (hold < 0 || hold > 120 || (args[1] && !hold))
    {
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    pixels = AllocVec(sizeof(*pixels), MEMF_PUBLIC | MEMF_CLEAR);
    if (!pixels) goto failed;
    reference = pixels->reference;
    aware_pixels = pixels->aware_pixels;
    text_reference = pixels->text_reference;
    memset(&gadget, 0, sizeof(gadget));
    gadget.LeftEdge = 8; gadget.TopEdge = 48;
    gadget.Width = 64; gadget.Height = 16;
    gadget.Flags = GFLG_GADGHCOMP;
    gadget.Activation = GACT_RELVERIFY;
    gadget.GadgetType = GTYP_BOOLGADGET;
    gadget.GadgetID = 317;
    screen = OpenScreenTags(NULL, SA_LikeWorkbench, TRUE, SA_Scale, 100,
        SA_Title, (IPTR)"Legacy composition oracle",
        SA_PubName, (IPTR)"ScaleLegacyOracle", TAG_DONE);
    if (!screen || screen->Width < 640 || screen->Height < 480) goto failed;
    if (!(PubScreenStatus(screen, 0) & 1)) goto failed;
    {
        struct Screen *captured = LockPubScreen((CONST_STRPTR)"ScaleLegacyOracle");
        BOOL named = captured == screen;
        if (captured) UnlockPubScreen(NULL, captured);
        if (!named) goto failed;
    }
    report("[SCALE-LEGACY-PUBLIC] result=PASS name=ScaleLegacyOracle state=OPEN\n");
    legacy = OpenWindowTags(NULL, WA_CustomScreen, (IPTR)screen,
        WA_Left, 16, WA_Top, 32, WA_Width, 112, WA_Height, 80,
        WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
        WA_NoCareRefresh, TRUE, WA_Gadgets, (IPTR)&gadget,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_MOUSEBUTTONS, TAG_DONE);
    aware = OpenWindowTags(NULL, WA_CustomScreen, (IPTR)screen,
        WA_Left, screen->Width - 80, WA_Top, 64, WA_Width, 64, WA_Height, 64,
        WA_Borderless, TRUE, WA_ScaleAware, TRUE, WA_NoCareRefresh, TRUE, TAG_DONE);
    if (!legacy || !aware) goto failed;
    legacy_font_height = legacy->RPort->Font->tf_YSize;
    for (y = 0; y < PH; ++y)
        for (x = 0; x < PW; ++x)
        {
            SetAPen(legacy->RPort, ((x / 3 + y / 2) & 1) ? 1 : 2);
            WritePixel(legacy->RPort, PX + x, PY + y);
        }
    SetAPen(legacy->RPort, 1);
    Move(legacy->RPort, 8, 40);
    Text(legacy->RPort, (CONST_STRPTR)"Legacy", 6);
    SetAPen(aware->RPort, 2);
    RectFill(aware->RPort, PX, PY, PX + PW - 1, PY + PH - 1);
    Delay(10);
    if (ReadPixelArray(reference, 0, 0, PW * 3, &screen->RastPort,
        legacy->LeftEdge + PX, legacy->TopEdge + PY, PW, PH, RECTFMT_RGB) != PW * PH ||
        ReadPixelArray(aware_pixels, 0, 0, PW * 3, &screen->RastPort,
        aware->LeftEdge + PX, aware->TopEdge + PY, PW, PH, RECTFMT_RGB) != PW * PH ||
        ReadPixelArray(text_reference, 0, 0, TW * 3, &screen->RastPort,
        legacy->LeftEdge + TX, legacy->TopEdge + TY, TW, TH, RECTFMT_RGB) != TW * TH)
        goto failed;
    /* A blank/broken initial presentation must not be its own oracle. */
    for (x = 3; x < sizeof(pixels->reference); x += 3)
        if (memcmp(reference, reference + x, 3)) break;
    if (x == sizeof(pixels->reference)) goto failed;
    for (x = 3; x < sizeof(pixels->text_reference); x += 3)
        if (memcmp(text_reference, text_reference + x, 3)) break;
    if (x == sizeof(pixels->text_reference)) goto failed;
    for (phase = 0; phase < sizeof(phases) / sizeof(phases[0]); ++phase)
    {
        IPTR got = 0;
        ULONG factor = phases[phase] / 100, bad, other_bad, text_bad, clicks = 0;
        LONG tick;
        struct IntuiMessage *msg;
        SetAttrs((Object *)screen, SA_Scale, phases[phase], TAG_DONE);
        Delay(10);
        GetAttr(SA_Scale, (Object *)screen, &got);
        bad = compare(screen, legacy, factor, reference);
        other_bad = compare(screen, aware, 1, aware_pixels);
        text_bad = compare_text(screen, legacy, factor, text_reference);
        if (got != phases[phase] || bad || other_bad || text_bad ||
            legacy->RPort->Font->tf_YSize != legacy_font_height) ++failures;
        report("[SCALE-LEGACY] phase=%u scale=%u factor=%u pixels-bad=%u aware-bad=%u click-x=%u click-y=%u\n",
            phase, (ULONG)got, factor, bad, other_bad,
            (legacy->LeftEdge + 40) * factor, (legacy->TopEdge + 56) * factor);
        report("[SCALE-LEGACY-TEXT] phase=%u scale=%u pixels-bad=%u font=%u\n",
            phase, (ULONG)got, text_bad, (ULONG)legacy->RPort->Font->tf_YSize);
        if (hold)
        {
            if (!status_snapshot(status, "HOLD", phase,
                "[SCALE-LEGACY-STATUS] version=1 state=HOLD phase=%u scale=%u factor=%u click-x=%u click-y=%u pattern-bad=%u text-bad=%u aware-bad=%u failures=%u hold=%d font=%u base-font=%u\n",
                phase, (ULONG)got, factor,
                (legacy->LeftEdge + 40) * factor, (legacy->TopEdge + 56) * factor,
                bad, text_bad, other_bad, failures, (int)hold,
                (ULONG)legacy->RPort->Font->tf_YSize, (ULONG)legacy_font_height)) ++failures;
            report("[SCALE-CAPTURE] stage=legacy phase=%u scale=%u hold=%d\n",
                phase, phases[phase], hold);
        }
        for (tick = 0; tick < hold * 50; ++tick)
        {
            while ((msg = (struct IntuiMessage *)GetMsg(legacy->UserPort)))
            {
                if (msg->Class == IDCMP_GADGETUP && msg->IAddress == &gadget &&
                    msg->MouseX >= 8 && msg->MouseX < 72 &&
                    msg->MouseY >= 48 && msg->MouseY < 64) ++clicks;
                ReplyMsg((struct Message *)msg);
            }
            Delay(1);
        }
        if (args[1] && !clicks) ++failures;
        report("[SCALE-LEGACY-INPUT] phase=%u clicks=%u required=%d\n", phase, clicks, (LONG)args[1]);
        /* Single-pixel damage must propagate without repainting the window. */
        SetAPen(legacy->RPort, phase & 1 ? 1 : 2);
        WritePixel(legacy->RPort, PX, PY);
        Delay(5);
        if (ReadPixelArray(reference, 0, 0, PW * 3, legacy->RPort,
            PX, PY, 1, 1, RECTFMT_RGB) != 1) ++failures;
        bad = compare(screen, legacy, factor, reference);
        if (bad) ++failures;
        report("[SCALE-LEGACY-DAMAGE] phase=%u mode=one-pixel bad=%u\n", phase, bad);
        /* Drawing while hidden and exposing a previously covered proxy must
         * both retain the logical image. No client repaint is issued. */
        if (!HideWindow(legacy)) ++failures;
        Delay(5);
        SetAPen(legacy->RPort, phase & 1 ? 2 : 1);
        WritePixel(legacy->RPort, PX + 1, PY);
        if (ReadPixelArray(reference + 3, 0, 0, 3, legacy->RPort,
            PX + 1, PY, 1, 1, RECTFMT_RGB) != 1) ++failures;
        if (!ShowWindow(legacy, WINDOW_FRONTMOST)) ++failures;
        Delay(5);
        bad = compare(screen, legacy, factor, reference);
        if (bad) ++failures;
        report("[SCALE-LEGACY-DAMAGE] phase=%u mode=hidden bad=%u\n", phase, bad);
        {
            LONG original_left = aware->LeftEdge, original_top = aware->TopEdge;
            MoveWindow(aware, (legacy->LeftEdge + PX) * factor - original_left,
                (legacy->TopEdge + PY) * factor - original_top);
            WindowToFront(aware);
            Delay(5);
            MoveWindow(aware, original_left - aware->LeftEdge, original_top - aware->TopEdge);
            Delay(5);
            bad = compare(screen, legacy, factor, reference);
            other_bad = compare(screen, aware, 1, aware_pixels);
            if (bad || other_bad) ++failures;
            report("[SCALE-LEGACY-DAMAGE] phase=%u mode=exposure bad=%u aware-bad=%u\n",
                phase, bad, other_bad);
        }
    }
    /* Opening an unaware client after the scale change retains its base font.
     * Querying a scaled screen font is not an opt-in to aware presentation. */
    CloseWindow(legacy);
    legacy = NULL;
    SetAttrs((Object *)screen, SA_Scale, 200, TAG_DONE);
    legacy = OpenWindowTags(NULL, WA_CustomScreen, (IPTR)screen,
        WA_Left, 16, WA_Top, 32, WA_Width, 112, WA_Height, 80,
        WA_Borderless, TRUE, WA_NoCareRefresh, TRUE, TAG_DONE);
    if (!legacy) goto failed;
    if (legacy->RPort->Font->tf_YSize != legacy_font_height) ++failures;
    for (y = 0; y < PH; ++y)
        for (x = 0; x < PW; ++x)
        {
            SetAPen(legacy->RPort, ((x / 3 + y / 2) & 1) ? 1 : 2);
            WritePixel(legacy->RPort, PX + x, PY + y);
        }
    Delay(5);
    /* Read the logical reference independently of the physical output. */
    if (ReadPixelArray(reference, 0, 0, PW * 3, legacy->RPort,
        PX, PY, PW, PH, RECTFMT_RGB) != PW * PH) ++failures;
    x = compare(screen, legacy, 2, reference);
    if (x) ++failures;
    report("[SCALE-LEGACY-OPEN] scale=200 pixels-bad=%u font=%u base-font=%u\n",
        x, (unsigned)legacy->RPort->Font->tf_YSize, (unsigned)legacy_font_height);
    CloseWindow(legacy); legacy = NULL;
    CloseWindow(aware); aware = NULL;
    if (!gadtools_fonts(screen)) ++failures;
    for (phase = 1; phase <= 4; ++phase)
        if (!opposed_copies(screen, phase)) ++failures;
    goto cleanup;
failed:
    ++failures;
cleanup:
    /* Block new captures/visitors before disposing our own window resources. */
    if (screen) oracle_make_private(screen);
    if (aware) CloseWindow(aware);
    if (legacy) CloseWindow(legacy);
    if (screen && !CloseScreen(screen)) oracle_retain("close-screen");
    if (pixels) FreeVec(pixels);
    if (!status_snapshot(status, "DONE", 0,
        "[SCALE-LEGACY-STATUS] version=1 state=DONE result=%s failures=%u input=%s\n",
        failures ? "FAIL" : "PASS", failures, args[1] ? "required" : "unmeasured")) ++failures;
    report("[SCALE-LEGACY-CHECK] result=%s failures=%u input=%s\n",
        failures ? "FAIL" : "PASS", failures, args[1] ? "required" : "unmeasured");
    FreeArgs(rda);
    return failures ? RETURN_FAIL : RETURN_OK;
}
