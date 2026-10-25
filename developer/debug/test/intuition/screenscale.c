/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Desc: Open screens at several SA_Scale values and report what each got:
          the scale read back, the screen font's size and the title bar
          height. One window per screen shows the same text, so a
          screenshot compares them.

    Usage: screenscale SCALES/K,HOLD/N/K,FONT/K,SIZE/N/K
      SCALES  comma-separated percentages, default 100,200
      HOLD    seconds to keep the screens open, default 5
      FONT    font name for SA_Font, default the system font
      SIZE    its size in points, default 8
*/

#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/text.h>
#include <dos/dos.h>
#include <dos/rdargs.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>

#include <stdio.h>
#include <aros/debug.h>
#include <string.h>

#define MAX_SCREENS 4

static int scale_events(struct Window *window, struct Screen *screen, ULONG expected)
{
    struct IntuiMessage *msg;
    int count = 0;
    if (!window || !window->UserPort)
        return 0;
    while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)))
    {
        if (msg->Class == IDCMP_CHANGESCALE)
            count += msg->Code == expected && msg->IAddress == screen ? 1 : 1000;
        ReplyMsg((struct Message *)msg);
    }
    return count;
}

struct opened {
    struct Screen *screen;
    struct Window *window;
    ULONG asked;
    IPTR got;
};

int main(void)
{
    static const char template[] = "SCALES/K,HOLD/N/K,FONT/K,SIZE/N/K";
    IPTR args[4] = { 0, 0, 0, 0 };
    struct RDArgs *rda;
    struct opened opened[MAX_SCREENS];
    ULONG scales[MAX_SCREENS] = { 100, 200, 0, 0 };
    int count = 2;
    int failures = 0, live_events = 0, other_events = 0, legacy_events = 0;
    struct Window *legacy = NULL;
    struct TextFont *resident_font = NULL;
    int index;
    LONG hold = 5;
    struct TextAttr font;
    struct TextAttr *use_font = NULL;
    char line[256];
    int line_length = 0;

    rda = ReadArgs(template, args, NULL);
    if (rda == NULL) {
        PrintFault(IoErr(), "screenscale");
        return RETURN_FAIL;
    }
    if (args[0] != 0) {
        const char *at = (const char *)args[0];

        count = 0;
        while (*at != '\0' && count < MAX_SCREENS) {
            ULONG value = 0;

            while (*at >= '0' && *at <= '9')
                value = value * 10 + (ULONG)(*at++ - '0');
            if (value != 0)
                scales[count++] = value;
            if (*at == ',')
                ++at;
            else if (*at != '\0')
                break;
        }
    }
    if (args[1] != 0)
        hold = *(LONG *)args[1];
    if (args[2] != 0) {
        font.ta_Name = (STRPTR)args[2];
        font.ta_YSize = args[3] != 0 ? (UWORD)*(LONG *)args[3] : 8;
        font.ta_Style = FS_NORMAL;
        font.ta_Flags = FPF_DESIGNED;
        use_font = &font;
    }

    if (count < 1 || hold < 0 || hold > 3600)
    {
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    /* Intuition's SA_Font opens memory fonts first. Load an explicit disk
       font before opening the screens and keep it resident through cleanup. */
    if (use_font)
    {
        resident_font = OpenDiskFont(use_font);
        if (!resident_font)
        {
            bug("[SCALE-CHECK] result=FAIL reason=font-unavailable\n");
            Printf("[SCALE-CHECK] result=FAIL reason=font-unavailable\n");
            FreeArgs(rda);
            return RETURN_FAIL;
        }
    }
    memset(opened, 0, sizeof(opened));
    for (index = 0; index < count; ++index) {
        struct opened *o = &opened[index];
        char title[64];

        snprintf(title, sizeof(title), "Scale %lu%%", (unsigned long)scales[index]);
        o->asked = scales[index];
        o->screen = OpenScreenTags(NULL,
            SA_Title, (IPTR)title,
            SA_Scale, scales[index],
            SA_LikeWorkbench, TRUE,
            SA_ShowTitle, TRUE,
            use_font ? SA_Font : TAG_IGNORE, (IPTR)use_font,
            TAG_DONE);
        if (o->screen == NULL) {
            printf("screenscale: cannot open the screen at %lu%%\n",
                (unsigned long)scales[index]);
            ++failures;
            continue;
        }
        GetAttr(SA_Scale, (Object *)o->screen, &o->got);
        if (o->got != (scales[index] < 100 ? 100 : scales[index] > 400 ? 400 : scales[index] / 25 * 25))
            ++failures;
        if (use_font && o->screen->RastPort.Font->tf_YSize != use_font->ta_YSize * o->got / 100)
            ++failures;
        o->window = OpenWindowTags(NULL,
            WA_CustomScreen, (IPTR)o->screen,
            WA_ScaleAware, TRUE, WA_IDCMP, IDCMP_CHANGESCALE,
            WA_Left, 40, WA_Top, o->screen->BarHeight + 20,
            WA_Width, 360, WA_Height, 120,
            WA_Title, (IPTR)"The quick brown fox",
            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
            WA_Activate, TRUE,
            TAG_DONE);
        if (o->window == NULL)
            ++failures;
        if (o->window != NULL) {
            struct RastPort *rp = o->window->RPort;

            SetAPen(rp, 1);
            Move(rp, 16, o->window->BorderTop + rp->TxBaseline + 12);
            Text(rp, "Sphinx of black quartz, judge my vow", 36);
        }
    }

    line_length += snprintf(line + line_length, sizeof(line) - line_length,
        "[SCALE-SCREEN] screens=%d scales=", count);
    for (index = 0; index < count; ++index)
        line_length += snprintf(line + line_length, sizeof(line) - line_length,
            "%s%lu", index ? "," : "", (unsigned long)opened[index].got);
    line_length += snprintf(line + line_length, sizeof(line) - line_length,
        " font-ysize=");
    for (index = 0; index < count; ++index)
        line_length += snprintf(line + line_length, sizeof(line) - line_length,
            "%s%u", index ? "," : "", opened[index].screen ?
                (unsigned)opened[index].screen->RastPort.Font->tf_YSize : 0);
    line_length += snprintf(line + line_length, sizeof(line) - line_length,
        " bar-height=");
    for (index = 0; index < count; ++index)
        line_length += snprintf(line + line_length, sizeof(line) - line_length,
            "%s%u", index ? "," : "", opened[index].screen ?
                (unsigned)opened[index].screen->BarHeight : 0);
    bug("%s\n", line);
    printf("%s\n", line);
    fflush(stdout);

    if (hold > 0)
        Delay(50 * hold);

    if (opened[0].screen && opened[0].window)
    {
        struct Screen *screen = opened[0].screen;
        IPTR got = 0;
        ULONG initial = opened[0].got;
        ULONG changed = initial == 200 ? 100 : 200;
        UWORD base_height = screen->RastPort.Font->tf_YSize;
        UWORD legacy_height = 0;
        int n;
        legacy = OpenWindowTags(NULL, WA_CustomScreen, (IPTR)screen,
            WA_Width, 250, WA_Height, 80, WA_Title, (IPTR)"Legacy 100 percent",
            WA_IDCMP, IDCMP_CHANGESCALE, TAG_DONE);
        if (!legacy)
            ++failures;
        else
            legacy_height = legacy->RPort->Font->tf_YSize;
        SetAttrs((Object *)screen, SA_Scale, changed, TAG_DONE);
        GetAttr(SA_Scale, (Object *)screen, &got);
        live_events += scale_events(opened[0].window, screen, changed);
        legacy_events += scale_events(legacy, screen, changed);
        for (n = 1; n < count; ++n)
            other_events += scale_events(opened[n].window, opened[n].screen, changed);
        if (got != changed || live_events != 1 || other_events || legacy_events)
            ++failures;
        if (use_font && screen->RastPort.Font->tf_YSize != use_font->ta_YSize * changed / 100)
            ++failures;
        if (legacy && legacy->RPort->Font->tf_YSize != legacy_height)
            ++failures;
        /* Unchanged scale sends no event. Return to the original scale and
           check font size, which catches compounding on repeated changes. */
        SetAttrs((Object *)screen, SA_Scale, changed, TAG_DONE);
        if (scale_events(opened[0].window, screen, changed))
            ++failures;
        SetAttrs((Object *)screen, SA_Scale, initial, TAG_DONE);
        GetAttr(SA_Scale, (Object *)screen, &got);
        if (got != initial || scale_events(opened[0].window, screen, initial) != 1 ||
            screen->RastPort.Font->tf_YSize != base_height)
            ++failures;
        bug("[SCALE-LIVE] events=%ld other-screen=%ld legacy=%ld reset-font=%ld\n",
            (long)live_events, (long)other_events, (long)legacy_events,
            (long)screen->RastPort.Font->tf_YSize);
        Printf("[SCALE-LIVE] events=%ld other-screen=%ld legacy=%ld reset-font=%ld\n",
            (LONG)live_events, (LONG)other_events, (LONG)legacy_events,
            (LONG)screen->RastPort.Font->tf_YSize);
    }
    if (legacy)
        CloseWindow(legacy);
    bug("[SCALE-CHECK] result=%s failures=%ld\n", failures ? "FAIL" : "PASS", (long)failures);
    Printf("[SCALE-CHECK] result=%s failures=%ld\n", failures ? "FAIL" : "PASS", (LONG)failures);

    for (index = count - 1; index >= 0; --index) {
        if (opened[index].window != NULL)
            CloseWindow(opened[index].window);
        if (opened[index].screen != NULL)
            CloseScreen(opened[index].screen);
    }
    if (resident_font)
        CloseFont(resident_font);
    FreeArgs(rda);
    return failures ? RETURN_FAIL : RETURN_OK;
}
