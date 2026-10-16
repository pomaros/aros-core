/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/date.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <proto/utility.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct UtilityBase *UtilityBase;
struct Device *TimerBase;
#define ALIVE_GLYPH_W 5
#define ALIVE_GLYPH_H 7
#include <aros/monitor_render.h>

static void line(struct RastPort *rp, LONG y, const char *text, LONG scale)
{
    alive_draw_text(rp, 12, y, text, strlen(text), scale);
}

int main(int argc, char **argv)
{
    struct MsgPort *port = NULL;
    struct timerequest *request = NULL;
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    int diagnostic = argc == 2 && strcmp(argv[1], "--diagnostic") == 0;
    int result = 20, done = 0, opened = 0;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 0);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
    UtilityBase = (struct UtilityBase *)OpenLibrary((CONST_STRPTR)"utility.library", 0);
    if (!IntuitionBase || !GfxBase || !UtilityBase) goto cleanup;
    port = CreateMsgPort();
    if (!port) goto cleanup;
    request = (struct timerequest *)CreateIORequest(port, sizeof(*request));
    if (!request || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)request, 0))
        goto cleanup;
    opened = 1;
    TimerBase = request->tr_node.io_Device;
    screen = LockPubScreen(NULL);
    if (!screen) goto cleanup;
    {
        struct TagItem tags[] = {
            {WA_PubScreen, (IPTR)screen},
            {WA_Left, screen->Width > 650 ? screen->Width - 642 : 0},
            {WA_Top, 40}, {WA_Width, 632}, {WA_Height, 282},
            {WA_Title, (IPTR)"PomAROS  github.com/pomaros  |  github.com/jonx"},
            {WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                WFLG_SMART_REFRESH | WFLG_NOCAREREFRESH},
            {WA_IDCMP, IDCMP_CLOSEWINDOW}, {WA_Activate, FALSE}, {TAG_DONE, 0}
        };
        window = OpenWindowTagList(NULL, tags);
    }
    UnlockPubScreen(NULL, screen); screen = NULL;
    if (!window) goto cleanup;
    SetAPen(window->RPort, 0);
    RectFill(window->RPort, window->BorderLeft, window->BorderTop,
        window->Width - window->BorderRight - 1, window->Height - window->BorderBottom - 1);
    while (!done) {
        struct IntuiMessage *message;
        struct timeval now, uptime;
        struct ClockData date;
        uint64_t elapsed;
        char text[64];
        while ((message = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (message->Class == IDCMP_CLOSEWINDOW) done = 1;
            ReplyMsg((struct Message *)message);
        }
        if (done) break;
        GetSysTime(&now);
        Amiga2Date(now.tv_secs, &date);
        GetUpTime(&uptime);
        elapsed = uptime.tv_secs;
        snprintf(text, sizeof(text), "%02u:%02u:%02u", date.hour, date.min, date.sec);
        line(window->RPort, 28, text, 5);
        snprintf(text, sizeof(text), "%04u-%02u-%02u", date.year, date.month, date.mday);
        line(window->RPort, 78, text, 2);
        snprintf(text, sizeof(text), "UP %02lu:%02lu:%02lu", (unsigned long)(elapsed / 3600),
            (unsigned long)(elapsed / 60 % 60), (unsigned long)(elapsed % 60));
        line(window->RPort, 104, text, 2);
        line(window->RPort, 130, "HOSTED AARCH64 / DARWIN", 2);
        snprintf(text, sizeof(text), "RAM FREE %lu MB / TOTAL %lu MB",
            (unsigned long)(AvailMem(MEMF_ANY) / 1048576),
            (unsigned long)(AvailMem(MEMF_TOTAL) / 1048576));
        line(window->RPort, 156, text, 2);
        snprintf(text, sizeof(text), "LARGEST FREE %lu MB",
            (unsigned long)(AvailMem(MEMF_LARGEST) / 1048576));
        line(window->RPort, 182, text, 2);
        line(window->RPort, 208, "APPLE SENSORS N/A HOSTED", 2);
        line(window->RPort, 234, "CPU LOAD N/A", 2);
        if (diagnostic) {
            printf("MONITOR time=%lu uptime=%lu free=%lu total=%lu\n",
                (unsigned long)now.tv_secs, (unsigned long)elapsed,
                (unsigned long)AvailMem(MEMF_ANY), (unsigned long)AvailMem(MEMF_TOTAL));
            fflush(stdout);
        }
        Delay(50);
    }
    result = 0;
cleanup:
    if (window) CloseWindow(window);
    if (screen) UnlockPubScreen(NULL, screen);
    if (opened) CloseDevice((struct IORequest *)request);
    if (request) DeleteIORequest((struct IORequest *)request);
    if (port) DeleteMsgPort(port);
    if (UtilityBase) CloseLibrary((struct Library *)UtilityBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return result;
}
