/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Real opposed ClipBlit calls and a physical-screen oracle. Parent-owned
    windows, worker data and executable remain alive until DOS reaps children.
*/
#ifndef SCALELEGACY_COPY_H
#define SCALELEGACY_COPY_H
#include <exec/tasks.h>
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <graphics/layers.h>
#include <aros/libcall.h>

#define COPY_ROUNDS 96
#define COPY_WIDTH 16
#define COPY_HEIGHT 2
#define COPY_GUARD 1
#define COPY_PATCHES 6
#define COPY_BYTES ((COPY_WIDTH + 2) * (COPY_HEIGHT + 2) * 3)

struct CopyPatch { WORD x, y; BOOL border; };
/* Immutable inner/border sources followed by four distinct destinations. */
static const struct CopyPatch copy_patches[COPY_PATCHES] = {
    {4, 5, FALSE}, {4, 2, TRUE}, {4, 13, FALSE},
    {24, 2, TRUE}, {44, 2, TRUE}, {4, 19, FALSE}
};
/* Pair order: inner/inner, border/border, inner/border, border/inner. */
static const UBYTE copy_source[4] = {0, 1, 0, 1};
static const UBYTE copy_destination[4] = {2, 4, 3, 5};
struct CopyGroup { ULONG go, stop; };
struct CopyWorker {
    struct CopyGroup *group;
    struct Window *source, *destination;
    ULONG tid, ready, base_ok, rounds, pairs[4], error, done, reaped, progress_samples;
};
struct CopyOracle {
    UBYTE reference[2][COPY_PATCHES][COPY_BYTES];
    UBYTE pixels[COPY_BYTES * 4 * 4];
};
struct CopyClock {
    struct MsgPort *port;
    struct timerequest *request;
    BOOL opened, pending;
};

static struct RastPort *copy_rp(struct Window *window, ULONG patch)
{
    return copy_patches[patch].border ? window->BorderRPort : window->RPort;
}

/* Explicit bases: an NP_Entry child has no second executable CRT startup.
   Neither child writes the parent's auto-opened global library variables. */
static void copy_delay(struct DosLibrary *base)
{
    AROS_LC1NR(void, Delay, AROS_LCA(ULONG, 1, D1),
        struct DosLibrary *, base, 33, Dos);
}

static void copy_blit(struct GfxBase *base, struct RastPort *source,
                     const struct CopyPatch *sp, struct RastPort *destination,
                     const struct CopyPatch *dp)
{
    AROS_LC9NR(void, ClipBlit,
        AROS_LCA(struct RastPort *, source, A0),
        AROS_LCA(WORD, sp->x, D0), AROS_LCA(WORD, sp->y, D1),
        AROS_LCA(struct RastPort *, destination, A1),
        AROS_LCA(WORD, dp->x, D2), AROS_LCA(WORD, dp->y, D3),
        AROS_LCA(WORD, COPY_WIDTH, D4), AROS_LCA(WORD, COPY_HEIGHT, D5),
        AROS_LCA(UBYTE, 0xc0, D6), struct GfxBase *, base, 92, Graphics);
}

static LONG copy_worker(void)
{
    struct Task *self = FindTask(NULL);
    struct CopyWorker *worker = self->tc_UserData;
    struct GfxBase *graphics = NULL;
    struct DosLibrary *dos = NULL;
    ULONG round, pair;
    if (!(self->tc_Flags & TF_ETASK))
    {
        __atomic_store_n(&worker->error, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&worker->ready, 1, __ATOMIC_RELEASE);
        return RETURN_FAIL;
    }
    __atomic_store_n(&worker->tid,
        self->tc_UnionETask.tc_ETask->et_UniqueID, __ATOMIC_RELEASE);
    graphics = (struct GfxBase *)OpenLibrary("graphics.library", 0);
    dos = (struct DosLibrary *)OpenLibrary("dos.library", 0);
    if (!graphics || !dos)
        __atomic_store_n(&worker->error, 2, __ATOMIC_RELEASE);
    else
        __atomic_store_n(&worker->base_ok, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&worker->ready, 1, __ATOMIC_RELEASE);
    if (!graphics || !dos) goto finished;
    while (!__atomic_load_n(&worker->group->go, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&worker->group->stop, __ATOMIC_ACQUIRE))
        copy_delay(dos);
    for (round = 0; round < COPY_ROUNDS &&
         !__atomic_load_n(&worker->group->stop, __ATOMIC_ACQUIRE); ++round)
    {
        for (pair = 0; pair < 4; ++pair)
        {
            ULONG source = copy_source[pair], destination = copy_destination[pair];
            copy_blit(graphics, copy_rp(worker->source, source),
                &copy_patches[source], copy_rp(worker->destination, destination),
                &copy_patches[destination]);
            __atomic_add_fetch(&worker->pairs[pair], 1, __ATOMIC_RELEASE);
        }
        __atomic_store_n(&worker->rounds, round + 1, __ATOMIC_RELEASE);
        copy_delay(dos);
    }
finished:
    if (dos) CloseLibrary((struct Library *)dos);
    if (graphics) CloseLibrary((struct Library *)graphics);
    __atomic_store_n(&worker->done, 1, __ATOMIC_RELEASE);
    return __atomic_load_n(&worker->error, __ATOMIC_ACQUIRE) ? RETURN_FAIL : RETURN_OK;
}

/* A timer bounds the client's own waits. A deadlocked graphics API can also
   hold its caller; the external independent debugger remains the observer. */
static void copy_retain(struct CopyWorker *workers, ULONG created,
                        ULONG factor, const char *reason)
{
    ULONG i;
    if (created) __atomic_store_n(&workers[0].group->stop, 1, __ATOMIC_RELEASE);
    report("[SCALE-LEGACY-COPY-FAIL] factor=%u workers=%u reason=%s timeout=1 retained=1\n",
        factor, created, reason);
    for (i = 0; i < created; ++i)
        report("[SCALE-LEGACY-COPY-STATE] worker=%u tid=%u ready=%u rounds=%u done=%u error=%u\n",
            i, __atomic_load_n(&workers[i].tid, __ATOMIC_ACQUIRE),
            __atomic_load_n(&workers[i].ready, __ATOMIC_ACQUIRE),
            __atomic_load_n(&workers[i].rounds, __ATOMIC_ACQUIRE),
            __atomic_load_n(&workers[i].done, __ATOMIC_ACQUIRE),
            __atomic_load_n(&workers[i].error, __ATOMIC_ACQUIRE));
    for (;;) Wait(0);
}

static BOOL copy_clock_open(struct CopyClock *clock)
{
    memset(clock, 0, sizeof(*clock));
    clock->port = CreateMsgPort();
    if (!clock->port) return FALSE;
    clock->request = (struct timerequest *)CreateIORequest(clock->port, sizeof(*clock->request));
    if (!clock->request) return FALSE;
    if (OpenDevice(TIMERNAME, UNIT_VBLANK, &clock->request->tr_node, 0)) return FALSE;
    clock->opened = TRUE;
    clock->request->tr_node.io_Command = TR_ADDREQUEST;
    clock->request->tr_time.tv_secs = 10;
    clock->request->tr_time.tv_micro = 0;
    SendIO(&clock->request->tr_node);
    clock->pending = TRUE;
    return TRUE;
}

static void copy_clock_close(struct CopyClock *clock)
{
    if (clock->pending)
    {
        AbortIO(&clock->request->tr_node);
        WaitIO(&clock->request->tr_node);
    }
    if (clock->opened) CloseDevice(&clock->request->tr_node);
    if (clock->request) DeleteIORequest(&clock->request->tr_node);
    if (clock->port) DeleteMsgPort(clock->port);
}

static void copy_deadline(struct CopyClock *clock, struct CopyWorker *workers,
                          ULONG created, ULONG factor, const char *reason)
{
    if (CheckIO(&clock->request->tr_node))
        copy_retain(workers, created, factor, reason);
}

static BOOL copy_read(struct Screen *screen, struct Window *window, ULONG patch,
                      ULONG factor, UBYTE *pixels)
{
    const struct CopyPatch *p = &copy_patches[patch];
    LONG left = window->LeftEdge + p->x - COPY_GUARD;
    LONG top = window->TopEdge + p->y - COPY_GUARD;
    ULONG width = (COPY_WIDTH + COPY_GUARD * 2) * factor;
    ULONG height = (COPY_HEIGHT + COPY_GUARD * 2) * factor;
    if (!p->border)
    {
        left += window->BorderLeft;
        top += window->BorderTop;
    }
    return ReadPixelArray(pixels, 0, 0, width * 3, &screen->RastPort,
        left * factor, top * factor, width, height, RECTFMT_RGB) == width * height;
}

static void copy_compare(const UBYTE *pixels, const UBYTE *before,
                         const UBYTE *source, ULONG factor,
                         ULONG *bad, ULONG *guard_bad)
{
    ULONG x, y, width = (COPY_WIDTH + 2) * factor;
    for (y = 0; y < (COPY_HEIGHT + 2) * factor; ++y)
        for (x = 0; x < width; ++x)
        {
            ULONG lx = x / factor, ly = y / factor;
            const UBYTE *expected = before + (ly * (COPY_WIDTH + 2) + lx) * 3;
            BOOL inside = lx >= 1 && lx <= COPY_WIDTH && ly >= 1 && ly <= COPY_HEIGHT;
            if (inside) expected = source + (ly * (COPY_WIDTH + 2) + lx) * 3;
            if (memcmp(pixels + (y * width + x) * 3, expected, 3))
            {
                if (inside) ++*bad;
                else ++*guard_bad;
            }
        }
}

static BOOL copy_validate(ULONG created, const struct CopyWorker *workers,
                          ULONG samples, ULONG active_samples, ULONG concurrent_checks,
                          ULONG pair_bad, ULONG source_bad, ULONG guard_bad)
{
    ULONG i, pair;
    if (created != 2 || !samples || active_samples < 2 || concurrent_checks < 2 ||
        pair_bad || source_bad || guard_bad) return FALSE;
    for (i = 0; i < created; ++i)
    {
        if (!workers[i].tid || !workers[i].ready || !workers[i].base_ok ||
            workers[i].error || !workers[i].done || !workers[i].reaped ||
            workers[i].rounds != COPY_ROUNDS || workers[i].progress_samples < 2) return FALSE;
        for (pair = 0; pair < 4; ++pair)
            if (workers[i].pairs[pair] != COPY_ROUNDS) return FALSE;
    }
    return TRUE;
}

static BOOL opposed_copies(struct Screen *screen, ULONG factor)
{
    struct Window *windows[2] = {NULL, NULL};
    struct CopyGroup group = {0};
    struct CopyWorker workers[2];
    struct CopyClock clock;
    struct CopyOracle *oracle = NULL;
    UBYTE (*reference)[COPY_PATCHES][COPY_BYTES];
    UBYTE *pixels;
    ULONG i, j, x, y, created = 0, samples = 0, active_samples = 0;
    ULONG concurrent_checks = 0, pair_bad = 0, source_bad = 0, guard_bad = 0;
    ULONG last_rounds[2] = {0, 0};
    BOOL good = FALSE;
    memset(workers, 0, sizeof(workers));
    memset(&clock, 0, sizeof(clock));
    if (!(FindTask(NULL)->tc_Flags & TF_ETASK) || factor < 1 || factor > 4)
        goto cleanup;
    oracle = AllocVec(sizeof(*oracle), MEMF_PUBLIC | MEMF_CLEAR);
    if (!oracle) goto cleanup;
    reference = oracle->reference;
    pixels = oracle->pixels;
    SetAttrs((Object *)screen, SA_Scale, factor * 100, TAG_DONE);
    for (i = 0; i < 2; ++i)
    {
        windows[i] = OpenWindowTags(NULL, WA_CustomScreen, (IPTR)screen,
            WA_Left, 8 + i * 76, WA_Top, 24, WA_Width, 68, WA_Height, 72,
            WA_GimmeZeroZero, TRUE, WA_Title, (IPTR)"", WA_NoCareRefresh, TRUE,
            TAG_DONE);
        if (!windows[i] || !windows[i]->RPort || !windows[i]->BorderRPort ||
            !windows[i]->RPort->Layer || !windows[i]->BorderRPort->Layer ||
            windows[i]->RPort->Layer == windows[i]->BorderRPort->Layer ||
            windows[i]->BorderTop < 5 || windows[i]->GZZWidth < 23 ||
            windows[i]->GZZHeight < 24 || windows[i]->Width < 63 ||
            (windows[i]->LeftEdge + windows[i]->Width) * factor > screen->Width ||
            (windows[i]->TopEdge + windows[i]->Height) * factor > screen->Height)
            goto cleanup;
    }
    for (i = 0; i < 2; ++i)
    {
        for (j = 0; j < COPY_PATCHES; ++j)
        {
            struct RastPort *rp = copy_rp(windows[i], j);
            const struct CopyPatch *p = &copy_patches[j];
            SetAPen(rp, 0);
            RectFill(rp, p->x - 1, p->y - 1, p->x + COPY_WIDTH, p->y + COPY_HEIGHT);
            if (j >= 2) continue;
            for (y = 0; y < COPY_HEIGHT; ++y)
                for (x = 0; x < COPY_WIDTH; ++x)
                {
                    SetAPen(rp, ((x / 2 + y + j) & 1) ? 1 : 2);
                    WritePixel(rp, p->x + x, p->y + y);
                }
        }
    }
    if ((windows[0]->LeftEdge + windows[0]->Width) * factor > windows[1]->LeftEdge * factor)
        goto cleanup;
    Delay(10);
    for (i = 0; i < 2; ++i)
        for (j = 0; j < COPY_PATCHES; ++j)
        {
            /* Baseline comes from logical clients; compare physical output
               separately before worker creation to reject a blank oracle. */
            const struct CopyPatch *p = &copy_patches[j];
            if (ReadPixelArray(reference[i][j], 0, 0, (COPY_WIDTH + 2) * 3,
                copy_rp(windows[i], j), p->x - 1, p->y - 1,
                COPY_WIDTH + 2, COPY_HEIGHT + 2, RECTFMT_RGB) !=
                (COPY_WIDTH + 2) * (COPY_HEIGHT + 2) ||
                !copy_read(screen, windows[i], j, factor, pixels)) goto cleanup;
            copy_compare(pixels, reference[i][j], reference[i][j], factor,
                &pair_bad, &guard_bad);
        }
    if (pair_bad || guard_bad) goto cleanup;
    /* Sources must contain distinct, nonuniform bytes. */
    for (i = 0; i < 2; ++i)
    {
        BOOL nonuniform = FALSE;
        for (j = 1; j < COPY_WIDTH; ++j)
            if (memcmp(reference[i][0] + ((COPY_WIDTH + 2) + 1) * 3,
                       reference[i][0] + ((COPY_WIDTH + 2) + 1 + j) * 3, 3))
                nonuniform = TRUE;
        if (!nonuniform || !memcmp(reference[i][0], reference[i][1], COPY_BYTES)) goto cleanup;
    }
    if (!copy_clock_open(&clock)) goto cleanup;
    report("[SCALE-LEGACY-COPY-START] scale=%u factor=%u workers=2 rounds=%u gzz=2\n",
        factor * 100, factor, COPY_ROUNDS);
    for (i = 0; i < 2; ++i)
    {
        workers[i].group = &group;
        workers[i].source = windows[i];
        workers[i].destination = windows[1 - i];
        /* The asynchronous return value is a success indicator only. The
           child publishes its own stable ID before it can return. */
        if (!CreateNewProcTags(NP_Entry, (IPTR)copy_worker,
            NP_UserData, (IPTR)&workers[i], NP_StackSize, 65536,
            NP_Name, (IPTR)"ScaleLegacy opposed ClipBlit", NP_Cli, TRUE,
            NP_NotifyOnDeath, TRUE, TAG_DONE)) break;
        ++created;
    }
    for (;;)
    {
        BOOL ready = TRUE;
        for (i = 0; i < created; ++i)
            if (!__atomic_load_n(&workers[i].ready, __ATOMIC_ACQUIRE)) ready = FALSE;
        if (ready) break;
        copy_deadline(&clock, workers, created, factor, "ready");
        Delay(1);
    }
    if (created != 2) __atomic_store_n(&group.stop, 1, __ATOMIC_RELEASE);
    for (i = 0; i < created; ++i)
        if (!__atomic_load_n(&workers[i].tid, __ATOMIC_ACQUIRE))
            copy_retain(workers, created, factor, "child-id");
        else if (__atomic_load_n(&workers[i].error, __ATOMIC_ACQUIRE))
            __atomic_store_n(&group.stop, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&group.go, 1, __ATOMIC_RELEASE);
    for (;;)
    {
        ULONG rounds[2] = {0, 0};
        BOOL done = TRUE;
        for (i = 0; i < created; ++i)
        {
            rounds[i] = __atomic_load_n(&workers[i].rounds, __ATOMIC_ACQUIRE);
            if (!__atomic_load_n(&workers[i].done, __ATOMIC_ACQUIRE)) done = FALSE;
        }
        if (done) break;
        ++samples;
        if (created == 2 && rounds[0] && rounds[1] &&
            rounds[0] < COPY_ROUNDS && rounds[1] < COPY_ROUNDS)
        {
            ++active_samples;
            for (i = 0; i < 2; ++i)
                if (rounds[i] > last_rounds[i])
                {
                    ++workers[i].progress_samples;
                    last_rounds[i] = rounds[i];
                }
            /* Test stable immutable physical source while both workers are
               incomplete. Reading a changing destination would race copies. */
            for (i = 0; i < 2; ++i)
            {
                ULONG bad = 0, guards = 0;
                if (!copy_read(screen, windows[i], 0, factor, pixels)) ++source_bad;
                else copy_compare(pixels, reference[i][0], reference[i][0], factor,
                    &bad, &guards);
                source_bad += bad; guard_bad += guards;
            }
            ++concurrent_checks;
        }
        copy_deadline(&clock, workers, created, factor, "copies");
        Delay(1);
    }
    for (i = 0; i < created; ++i)
    {
        struct ETask *child;
        ULONG tid = __atomic_load_n(&workers[i].tid, __ATOMIC_ACQUIRE);
        while (ChildStatus(tid) == CHILD_ACTIVE)
        {
            copy_deadline(&clock, workers, created, factor, "child-exit");
            Delay(1);
        }
        if (ChildStatus(tid) != CHILD_EXITED)
            copy_retain(workers, created, factor, "child-status");
        child = (struct ETask *)ChildWait(tid);
        if ((IPTR)child <= CHILD_ACTIVE || child->et_UniqueID != tid)
            copy_retain(workers, created, factor, "child-reap");
        if (child->et_Result1 != RETURN_OK) workers[i].error = 3;
        ChildFree(tid);
        workers[i].reaped = 1;
        report("[SCALE-LEGACY-COPY-WORKER] factor=%u worker=%u tid=%u ready=%u base-ok=%u rounds=%u ii=%u bb=%u ib=%u bi=%u error=%u done=%u reaped=%u progress-samples=%u\n",
            factor, i, tid, workers[i].ready, workers[i].base_ok, workers[i].rounds,
            workers[i].pairs[0], workers[i].pairs[1], workers[i].pairs[2], workers[i].pairs[3],
            workers[i].error, workers[i].done, workers[i].reaped, workers[i].progress_samples);
    }
    Delay(10);
    for (i = 0; i < 2; ++i)
        for (j = 0; j < COPY_PATCHES; ++j)
        {
            ULONG source = j;
            ULONG pair;
            ULONG bad = 0, guards = 0;
            for (pair = 0; pair < 4; ++pair)
                if (j == copy_destination[pair]) source = copy_source[pair];
            if (!copy_read(screen, windows[i], j, factor, pixels)) ++pair_bad;
            else copy_compare(pixels, reference[i][j], reference[1 - i][source], factor,
                &bad, &guards);
            if (j < 2) source_bad += bad;
            else pair_bad += bad;
            guard_bad += guards;
        }
    good = copy_validate(created, workers, samples, active_samples, concurrent_checks,
        pair_bad, source_bad, guard_bad);
cleanup:
    copy_clock_close(&clock);
    if (oracle) FreeVec(oracle);
    for (i = 0; i < 2; ++i) if (windows[i]) CloseWindow(windows[i]);
    report("[SCALE-LEGACY-COPY-CHECK] factor=%u workers=%u rounds=%u gzz=%u samples=%u active-samples=%u concurrent-checks=%u pair-bad=%u source-bad=%u guard-bad=%u reaped=%u timeout=0 result=%s\n",
        factor, created, COPY_ROUNDS, windows[0] && windows[1] ? 2 : 0,
        samples, active_samples, concurrent_checks, pair_bad, source_bad, guard_bad,
        workers[0].reaped + workers[1].reaped, good ? "PASS" : "FAIL");
    return good;
}
#endif
