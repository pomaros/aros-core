/* SPDX-License-Identifier: APL-1.1
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Interactive TILE1/TILE5 probe, not an automatic acceptance verdict.
 */
#include <dos/dos.h>
#include <dos/dostags.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include <proto/workbench.h>
#include <proto/arossupport.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Keep evidence visible in the Shell and on the kernel debug transport. */
static void probe_log(const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    fputs(line,stdout);
    fflush(stdout);
    kprintf("%s",line);
}

static void probe_line(const char *line)
{
    probe_log("%s\n",line);
}

static void messages(struct MsgPort *port)
{
    struct AppMessage *message;
    while ((message = (struct AppMessage *)GetMsg(port))) {
        if (message->am_Class == AMCLASSICON_Region &&
            message->am_Message.mn_Length >= sizeof(struct AppIconTileMessage))
            probe_log("[TILE5] region=%s x=%ld y=%ld\n",
                   ((struct AppIconTileMessage *)message)->atm_Region,
                   (long)message->am_MouseX, (long)message->am_MouseY);
        else
            probe_log("[TILE5] class=%lu\n", (unsigned long)message->am_Class);
        ReplyMsg(&message->am_Message);
    }
}

/* The supervisor keeps only a registry handle. The registering task destroys
 * every borrowed source and its message port, then genuinely exits. */
static struct {
    struct Task *supervisor;
    ULONG signal;
    struct BitMap *bitmap;
    struct DiskObject *fallback;
    struct MsgPort *port;
    struct AppIcon *icon;
} exitProbe;

static LONG exitOwner(void)
{
    struct AppIconRegions regions = { 2, {
        {0, 0, 64, 128, "left"}, {64, 0, 64, 128, "right"}
    }};
    struct TagItem tags[] = {
        {WBAPPICONA_Cells, WBAPPICON_CELLS(2,2)},
        {WBAPPICONA_Surface, (IPTR)exitProbe.bitmap},
        {WBAPPICONA_Regions, (IPTR)&regions},
        {WBAPPICONA_Cadence, 500}, {TAG_DONE,0}
    };
    exitProbe.port=CreateMsgPort();
    exitProbe.icon=AddAppIconA(2, 0, "Exited tile owner", exitProbe.port, 0,
                             exitProbe.fallback, tags);
    FreeBitMap(exitProbe.bitmap);
    FreeDiskObject(exitProbe.fallback);
    /* Invalidate delivery before deleting a live task's port. The cached
     * icon remains registered for the supervisor's observation. */
    if (exitProbe.icon) {
        struct TagItem detach[] = {{WBCTRLA_DetachAppIcon, (IPTR)exitProbe.icon}, {TAG_DONE,0}};
        WorkbenchControlA(NULL, detach);
    }
    messages(exitProbe.port);
    if (exitProbe.port) DeleteMsgPort(exitProbe.port);
    Signal(exitProbe.supervisor, exitProbe.signal);
    /* The supervisor observes actual process removal after DOS finalization. */
    return RETURN_OK;
}

int main(int argc, char **argv)
{
    struct Screen *screen = NULL;
    struct MsgPort *port = NULL;
    struct DiskObject *fallback = NULL;
    struct BitMap *bitmap = NULL;
    struct AppIcon *icon = NULL;
    struct RastPort rp;
    int result = RETURN_FAIL;
    BOOL ownerExit = argc > 1 && !strcmp(argv[1], "exit");
    BOOL invalid = argc > 1 && !strcmp(argv[1], "invalid");
    struct AppIconRegions regions = { 2, {
        {0, 0, 64, 128, "left"}, {64, 0, 64, 128, "right"}
    }};

    screen = LockPubScreen(NULL);
    port = CreateMsgPort();
    fallback = GetDefDiskObject(WBTOOL);
    if (!screen || !port || !fallback) goto out;
    bitmap = AllocBitMap(invalid ? 64 : 128, 128,
                         GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH),
                         BMF_CLEAR, screen->RastPort.BitMap);
    if (!bitmap) goto out;
    InitRastPort(&rp);
    rp.BitMap = bitmap;
    SetRast(&rp, 1);
    if (ownerExit) {
        LONG signal=AllocSignal(-1);
        BYTE oldPriority;
        struct Process *child;
        if (signal<0) goto out;
        exitProbe.supervisor=FindTask(NULL);
        exitProbe.signal=1UL<<signal;
        exitProbe.bitmap=bitmap;
        exitProbe.fallback=fallback;
        /* MsgPort signal allocation belongs to the creating task. The
         * retiring child must create and delete its own port. */
        DeleteMsgPort(port);
        port=NULL;
        exitProbe.port=NULL;
        oldPriority=SetTaskPri(exitProbe.supervisor,0);
        child=CreateNewProcTags(NP_Name,(IPTR)"Tile exit owner",
                                NP_Priority,1,NP_Entry,(IPTR)exitOwner,
                                NP_StackSize,16384, TAG_DONE);
        if (!child) {
            SetTaskPri(exitProbe.supervisor,oldPriority);
            FreeSignal(signal);
            goto out;
        }
        Wait(exitProbe.signal);
        FreeSignal(signal);
        SetTaskPri(exitProbe.supervisor,oldPriority);
        bitmap=NULL; fallback=NULL; port=NULL;
        icon=exitProbe.icon;
        for (unsigned attempt=0; attempt<50 && FindTask((CONST_STRPTR)"Tile exit owner"); ++attempt)
            Delay(1);
        if (!icon || FindTask((CONST_STRPTR)"Tile exit owner")) {
            probe_line("[TILE5] FAIL owner-exit registration or task termination");
            goto out;
        }
        probe_line("[TILE5] owner-exit retired=1 source-freed=1 port-deleted=1");
        Delay(300);
        probe_line("[TILE5] owner-exit observation complete; removing retained registry handle");
        result=RETURN_OK;
        goto out;
    }
    {
        struct TagItem tags[] = {
            {WBAPPICONA_Cells, WBAPPICON_CELLS(2,2)},
            {WBAPPICONA_Surface, (IPTR)bitmap},
            {WBAPPICONA_Regions, (IPTR)&regions},
            {WBAPPICONA_Cadence, 500},
            {TAG_DONE, 0}
        };
        icon = AddAppIconA(1, 0, "Tile probe", port, 0, fallback, tags);
    }
    if (invalid) {
        probe_log("[TILE5] invalid-size registration=%s\n", icon ? "accepted (FAIL)" : "refused");
        result = icon ? RETURN_FAIL : RETURN_OK;
        goto out;
    }
    if (!icon) goto out;
    probe_line("[TILE5] registered cells=2x2 regions=left,right cadence=500ms");
    probe_line("[TILE5] click both halves; select/drag/clean up while frames update for 20 seconds");
    for (unsigned frame=0; frame<40; ++frame) {
        struct AppIconPublish update = {icon, bitmap};
        struct TagItem tags[] = {{WBCTRLA_PublishAppIcon, (IPTR)&update}, {TAG_DONE,0}};
        char label[24];
        Delay(25);
        SetAPen(&rp, frame & 1 ? 1 : 2);
        RectFill(&rp, 0, 0, 63, 127);
        SetAPen(&rp, frame & 1 ? 2 : 1);
        RectFill(&rp, 64, 0, 127, 127);
        SetAPen(&rp, 0);
        snprintf(label, sizeof(label), "frame %u", frame);
        Move(&rp, 8, 24);
        Text(&rp, (CONST_STRPTR)label, strlen(label));
        WaitBlit();
        probe_log("[TILE5] frame=%u published=%ld\n", frame,
               (long)WorkbenchControlA(NULL, tags));
        messages(port);
    }
    probe_line("[TILE5] stopped publishing; static fallback must appear within five seconds");
    Delay(300);
    result = RETURN_OK;
out:
    if (icon) RemoveAppIcon(icon);
    if (port) messages(port);
    if (bitmap) FreeBitMap(bitmap);
    if (fallback) FreeDiskObject(fallback);
    if (port) DeleteMsgPort(port);
    if (screen) UnlockPubScreen(NULL, screen);
    return result;
}
