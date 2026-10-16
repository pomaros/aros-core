/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>. All rights reserved.
    SPDX-License-Identifier: APL-1.1

    Owner geometry and reversible suspension fixture. External input and
    front-buffer captures supply the independent presentation assertions.
*/
#include <dos/dos.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/workbench.h>
#include <proto/arossupport.h>
#include <proto/timer.h>
#include <string.h>
#define LOG(...) do { Forbid(); kprintf(__VA_ARGS__); Permit(); } while (0)
#define AT(ms,bit) (elapsed >= (ms) && !(phases & (1UL << (bit))) && (phases |= 1UL << (bit), TRUE))
struct Device *TimerBase;
static struct MsgPort *port, *clockPort;
static struct timerequest *timer;
static struct AppIcon *icons[2];
static struct BitMap *maps[2];
static unsigned clicks;
static ULONG clock_ms(void)
{
    struct EClockVal e; ULONG frequency = ReadEClock(&e);
    unsigned long long ticks = ((unsigned long long)e.ev_hi << 32) | e.ev_lo;
    return frequency ? (ULONG)(ticks / frequency * 1000 + ticks % frequency * 1000 / frequency) : 0;
}
static BOOL publish(unsigned id, struct BitMap *bitmap)
{
    struct AppIconPublish update = {icons[id], bitmap};
    struct TagItem tags[] = {{WBCTRLA_PublishAppIcon, (IPTR)&update}, {TAG_DONE, 0}};
    return WorkbenchControlA(NULL, tags);
}
static BOOL geometry(unsigned id, const char *phase, struct AppIconGeometry *g)
{
    memset(g, 0, sizeof(*g)); g->aig_Icon = icons[id];
    struct TagItem tags[] = {{WBCTRLA_QueryAppIconGeometry, (IPTR)g}, {TAG_DONE, 0}};
    BOOL ok = WorkbenchControlA(NULL, tags);
    LOG("[GEOPROBE] geometry phase=%s id=%u handle=%p ok=%u flags=%lu x=%ld y=%ld w=%lu h=%lu tick=%lu\n",
        phase, id + 1, icons[id], ok, (unsigned long)g->aig_Flags,
        (long)g->aig_Left, (long)g->aig_Top, (unsigned long)g->aig_Width,
        (unsigned long)g->aig_Height, (unsigned long)clock_ms());
    return ok;
}
static void paint(unsigned id, unsigned pen)
{
    struct RastPort rp; InitRastPort(&rp); rp.BitMap = maps[id]; SetRast(&rp, pen);
    SetAPen(&rp, 1); RectFill(&rp, id ? 8 : 32, 16, id ? 24 : 63, id ? 47 : 111); WaitBlit();
}
static void messages(void)
{
    struct AppMessage *m;
    while ((m = (struct AppMessage *)GetMsg(port))) {
        LOG("[GEOPROBE] message id=%lu class=%lu x=%ld y=%ld tick=%lu\n",
            (unsigned long)m->am_ID, (unsigned long)m->am_Class,
            (long)m->am_MouseX, (long)m->am_MouseY, (unsigned long)clock_ms());
        if (icons[1] && m->am_ID == 2 && m->am_Class == AMCLASSICON_Region) {
            ++clicks; paint(1, clicks & 1 ? 3 : 2);
            LOG("[GEOPROBE] control-click count=%u published=%u tick=%lu\n", clicks,
                publish(1, maps[1]), (unsigned long)clock_ms());
        }
        ReplyMsg(&m->am_Message);
    }
}
static void wait_events(void)
{
    timer->tr_node.io_Command = TR_ADDREQUEST;
    timer->tr_time.tv_secs = 0; timer->tr_time.tv_micro = 20000;
    SendIO((struct IORequest *)timer);
    Wait((1UL << port->mp_SigBit) | (1UL << clockPort->mp_SigBit));
    if (!CheckIO((struct IORequest *)timer)) AbortIO((struct IORequest *)timer);
    WaitIO((struct IORequest *)timer); SetSignal(0, 1UL << clockPort->mp_SigBit);
}
int main(void)
{
    struct Screen *screen = NULL; struct Window *cover = NULL;
    struct DiskObject fallback = {0}; struct Image image = {0}; UWORD bits[64];
    struct AppIconGeometry g; BOOL timerOpen = FALSE, failed = TRUE;
    ULONG phases = 0, lastBeat = 0;
    port = CreateMsgPort(); clockPort = CreateMsgPort();
    if (!port || !clockPort) goto out;
    timer = (struct timerequest *)CreateIORequest(clockPort, sizeof(*timer));
    if (!timer || OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)timer, 0)) goto out;
    timerOpen = TRUE; TimerBase = timer->tr_node.io_Device;
    screen = LockPubScreen(NULL); if (!screen) goto out;
    fallback.do_Magic = 0xe310; fallback.do_Version = 1; fallback.do_Type = WBTOOL;
    fallback.do_CurrentX = fallback.do_CurrentY = NO_ICON_POSITION;
    fallback.do_Gadget.Width = fallback.do_Gadget.Height = 32;
    fallback.do_Gadget.Flags = GFLG_GADGIMAGE; fallback.do_Gadget.GadgetRender = &image;
    image.Width = image.Height = 32; image.Depth = 1; image.PlanePick = 1; image.ImageData = bits;
    for (unsigned y = 0; y < 32; ++y) bits[y * 2] = bits[y * 2 + 1] = (y / 8) & 1 ? 0x00ff : 0xff00;
    for (unsigned id = 0; id < 2; ++id) {
        unsigned size = id ? 64 : 128;
        struct AppIconRegions regions = {1, {{0, 0, size, size, "control"}}};
        maps[id] = AllocBitMap(size, size, GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH), BMF_CLEAR, screen->RastPort.BitMap);
        if (!maps[id]) goto out; paint(id, 2);
        struct TagItem tags[] = {
            {WBAPPICONA_Cells, id ? WBAPPICON_CELLS(1,1) : WBAPPICON_CELLS(2,2)},
            {WBAPPICONA_Surface, (IPTR)maps[id]}, {WBAPPICONA_Regions, (IPTR)&regions},
            {WBAPPICONA_Cadence, 500}, {WBAPPICONA_Pointer, TRUE},
            {WBAPPICONA_HoverCadence, 33}, {TAG_DONE, 0}};
        icons[id] = AddAppIconA(id + 1, 0, id ? "Geometry control" : "Geometry subject", port, 0, &fallback, tags);
        if (!icons[id]) goto out;
    }
    for (ULONG start = clock_ms(); clock_ms() - start < 10000;) {
        messages(); geometry(0, "initializing", &g);
        if (g.aig_Flags & APPICONGF_VALID) break;
        publish(0, NULL); publish(1, NULL); wait_events();
    }
    if (!(g.aig_Flags & APPICONGF_VALID)) goto out;
    failed = FALSE;
    LOG("[GEOPROBE] ready tick=%lu\n", (unsigned long)clock_ms());
    for (ULONG start = clock_ms(), elapsed = 0; elapsed < 32000; elapsed = clock_ms() - start) {
        messages();
        if (clock_ms() - lastBeat >= 500) { publish(0, NULL); publish(1, NULL); lastBeat = clock_ms(); }
        if (AT(6000, 0)) { geometry(0, "moved", &g); geometry(1, "control", &g); }
        if (AT(8000, 1)) {
            geometry(0, "before-partial", &g);
            cover = OpenWindowTags(NULL, WA_PubScreen, (IPTR)screen, WA_Left, g.aig_Left + 64,
                WA_Top, g.aig_Top, WA_Width, 64, WA_Height, 128, WA_Borderless, TRUE, WA_Activate, TRUE, TAG_DONE);
            if (!cover) failed = TRUE;
        }
        if (AT(10000, 2)) geometry(0, "partial", &g);
        if (AT(12000, 3)) { if (cover) { CloseWindow(cover); cover = NULL; } }
        if (AT(14000, 4)) {
            cover = OpenWindowTags(NULL, WA_PubScreen, (IPTR)screen, WA_Left, 0, WA_Top, 0,
                WA_Width, screen->Width, WA_Height, screen->Height, WA_Borderless, TRUE, WA_Activate, TRUE, TAG_DONE);
            if (!cover) failed = TRUE;
        }
        if (AT(15500, 5)) geometry(0, "hidden", &g);
        if (AT(17000, 6)) { if (cover) { CloseWindow(cover); cover = NULL; } }
        if (AT(18500, 7)) geometry(0, "revealed", &g);
        if (AT(20000, 8)) {
            struct AppIconQuery q = {0}; q.aiq_Icon = icons[0];
            struct TagItem query[] = {{WBCTRLA_QueryAppIcon, (IPTR)&q}, {TAG_DONE, 0}};
            BOOL before = WorkbenchControlA(NULL, query);
            LOG("[GEOPROBE] before-suspend ok=%u flags=%lu tick=%lu\n", before,
                (unsigned long)q.aiq_Flags, (unsigned long)clock_ms());
            if (!before || !(q.aiq_Flags & APPICONQF_HOVERED)) failed = TRUE;
            struct TagItem tags[] = {{WBCTRLA_SuspendAppIcon, (IPTR)icons[0]}, {TAG_DONE, 0}};
            ULONG tick = clock_ms(); BOOL ok = WorkbenchControlA(NULL, tags);
            LOG("[GEOPROBE] suspended handle=%p ok=%u tick=%lu\n", icons[0], ok, (unsigned long)tick);
            if (!ok) failed = TRUE;
            BOOL after = WorkbenchControlA(NULL, query);
            LOG("[GEOPROBE] after-suspend ok=%u flags=%lu tick=%lu\n", after,
                (unsigned long)q.aiq_Flags, (unsigned long)clock_ms());
            if (!after || (q.aiq_Flags & APPICONQF_HOVERED)) failed = TRUE;
        }
        if (AT(23000, 9)) {
            struct AppIconQuery q = {0}; q.aiq_Icon = icons[0];
            struct TagItem tags[] = {{WBCTRLA_QueryAppIcon, (IPTR)&q}, {TAG_DONE, 0}};
            geometry(0, "suspended-heartbeat", &g);
            BOOL ok = WorkbenchControlA(NULL, tags);
            LOG("[GEOPROBE] suspended-query ok=%u flags=%lu tick=%lu\n", ok, (unsigned long)q.aiq_Flags, (unsigned long)clock_ms());
            if (!ok || (q.aiq_Flags & APPICONQF_HOVERED)) failed = TRUE;
            BOOL invalid = publish(0, maps[1]);
            LOG("[GEOPROBE] invalid-resume accepted=%u tick=%lu\n", invalid, (unsigned long)clock_ms());
            if (invalid) failed = TRUE;
        }
        if (AT(25000, 10)) {
            paint(0, 3); ULONG tick = clock_ms(); BOOL ok = publish(0, maps[0]);
            LOG("[GEOPROBE] resumed handle=%p ok=%u tick=%lu\n", icons[0], ok, (unsigned long)tick);
            if (!ok) failed = TRUE;
        }
        if (AT(27500, 11)) geometry(0, "resumed", &g);
        wait_events();
    }
    if (clicks < 2) failed = TRUE;
out:
    if (cover) CloseWindow(cover);
    for (unsigned id = 0; id < 2; ++id) if (icons[id]) { RemoveAppIcon(icons[id]); icons[id] = NULL; }
    if (port) messages();
    WaitBlit();
    for (unsigned id = 0; id < 2; ++id) if (maps[id]) FreeBitMap(maps[id]);
    if (screen) UnlockPubScreen(NULL, screen);
    if (timerOpen) CloseDevice((struct IORequest *)timer);
    if (timer) DeleteIORequest((struct IORequest *)timer);
    if (clockPort) DeleteMsgPort(clockPort); if (port) DeleteMsgPort(port);
    LOG("[GEOPROBE] %s clicks=%u\n", failed ? "FAIL" : "COMPLETE", clicks);
    return failed ? RETURN_FAIL : RETURN_OK;
}
