/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: LockSnoop [TASK name] - who holds the layer locks and public
          semaphores, and what every waiting task waits for.

    For a desktop that stops redrawing while tasks still run: a program
    blocked in OpenWindow or a drawing call waits for a layer lock, and the
    lock names its owner. Everything is read under Forbid() into a buffer,
    without taking any lock (a lock could be the one held), then printed:

      screen "<title>": layerinfo owner "<task>" nest n queue n
        layer <address> <x0>,<y0>-<x1>,<y1> window "<title>": owner "<task>" nest n queue n
      semaphore "<name>": owner "<task>" nest n queue n
      task "<name>" state n pri n sigwait 0x.. sigrecvd 0x..
        waits on semaphore <address> "<name>": owner "<task>" (state n) nest n queue n

    A task waiting for a semaphore (SIGF_SINGLE) has its SemaphoreRequest on
    its stack, linked into the semaphore's wait queue: the live part of the
    stack (from the saved stack pointer up) is searched for it, and the
    queue's header gives the semaphore, private ones too.
    A task waiting otherwise on SIGF_SINGLE (Delay() and other one-shot
    waits use it), or every task whose name contains TASK, also gets its
    saved pc, lr and frame-pointer chain, decoded after Permit() by
    debug.library (module, symbol and offset):

        frame <n> <address> <module> <symbol>+0x..
        frame <n> <address> resident "<name>"+0x..   (a ROM module: the
                                offset from its resident tag)
        frame <n> <address> near "<library>" LVO n +0x..   (otherwise: the
                                library or device vector nearest below)

    A traced task's timer requests found on its stack print with their
    message type (5 NT_MESSAGE: timer.device holds it, 7 NT_REPLYMSG:
    replied) and reply port.

    The input handlers follow, in input.device's order, each with the module
    its code is in (debug.library, else the nearest library vector):

        input handler "<name>" pri n code <address> <module> <symbol>

    A handler whose code is in no module points into freed memory.

    Every process also shows its output and error handles, with dos's
    flags (0x20: a flush in progress):

        output <address> flags 0x.. [FLUSHING] pos n end n

    Only the held locks print ("free" ones are counted). The window titles
    and task names are copied as they are at that moment.
*/

#include <exec/execbase.h>
#include <exec/resident.h>
#include <devices/input.h>
#include <devices/timer.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <aros/aarch64/cpucontext.h>
#include <libraries/debug.h>
#include <proto/debug.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define OUT_SIZE 32768

static char out[OUT_SIZE];
static ULONG used;

#define MAX_TRACED 8
#define MAX_FRAMES 24

/* A task's stack as found under Forbid(), decoded after it. */
struct traced {
    char name[64];
    ULONG frames;
    IPTR address[MAX_FRAMES];
};

static struct traced traced[MAX_TRACED];

/* input.device's base as rom/devs/input keeps it, up to the handler list. */
struct input_view {
    struct InputDevice pub;
    struct Task *InputTask;
    struct MsgPort *CommandPort;
    struct MinList HandlerList;
};

#define MAX_HANDLERS 32
static struct {
    char name[48];
    BYTE pri;
    APTR code;
} handlers[MAX_HANDLERS];
static ULONG handler_count;
static ULONG traced_count;
static const char *task_filter;
struct Library *DebugBase;

static void put(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void put(const char *fmt, ...)
{
    va_list ap;
    int n;

    if (used >= OUT_SIZE - 1)
        return;
    va_start(ap, fmt);
    n = vsnprintf(out + used, OUT_SIZE - used, fmt, ap);
    va_end(ap);
    if (n > 0)
        used += (ULONG)n < OUT_SIZE - used ? (ULONG)n : OUT_SIZE - 1 - used;
}

static const char *task_name(struct Task *t)
{
    if (t == NULL)
        return "-";
    return t->tc_Node.ln_Name != NULL ? t->tc_Node.ln_Name : "?";
}

static const char *safe(const char *s)
{
    return s != NULL ? s : "";
}

static void lock_line(const char *indent, const char *what, struct SignalSemaphore *ss)
{
    put("%s%s: owner \"%s\" nest %d queue %d\n", indent, what, task_name(ss->ss_Owner),
        (int)ss->ss_NestCount, (int)ss->ss_QueueCount);
}

/* An address the search may read: aligned, in memory exec manages. */
static BOOL readable(const void *p)
{
    return p != NULL && ((IPTR)p & (sizeof(IPTR) - 1)) == 0 && TypeOfMem((APTR)p) != 0;
}

/* The semaphore whose wait queue holds t's request, found on t's stack. */
static void waited_semaphore(struct Task *t)
{
    IPTR *p, *lower = (IPTR *)t->tc_SPReg, *upper = (IPTR *)t->tc_SPUpper;

    /* Only the live part of the stack, from the saved stack pointer up:
     * below it, and outside the bounds of a swapped stack, nothing is
     * known to be mapped. */
    if (t == FindTask(NULL) || lower == NULL || upper == NULL || upper <= lower ||
        (APTR)lower < t->tc_SPLower || (UBYTE *)upper - (UBYTE *)lower > 1024 * 1024) {
        put("  stack not searched (sp %p, %p-%p)\n", t->tc_SPReg, t->tc_SPLower, t->tc_SPUpper);
        return;
    }
    for (p = lower; p < upper; ++p) {
        struct SemaphoreRequest *sr;
        struct MinNode *node, *walk;
        struct SignalSemaphore *ss;
        int steps;

        if (*p != (IPTR)t)
            continue;
        sr = (struct SemaphoreRequest *)((UBYTE *)p - offsetof(struct SemaphoreRequest, sr_Waiter));
        node = &sr->sr_Link;
        if (!readable(node->mln_Succ) || !readable(node->mln_Pred) ||
            node->mln_Pred->mln_Succ != node || node->mln_Succ->mln_Pred != node)
            continue;
        /* Back to the header: the list's lh_Head is the node whose
         * predecessor is NULL. */
        for (walk = node, steps = 0; steps < 256; ++steps) {
            if (walk->mln_Pred == NULL || !readable(walk->mln_Pred))
                break;
            walk = walk->mln_Pred;
        }
        if (walk->mln_Pred != NULL)
            continue;
        ss = (struct SignalSemaphore *)((UBYTE *)walk - offsetof(struct SignalSemaphore, ss_WaitQueue));
        if (!readable(ss->ss_Owner) && ss->ss_Owner != NULL)
            continue;
        put("  waits on semaphore %p \"%s\": owner \"%s\" (state %d) nest %d queue %d\n",
            (void *)ss, ss->ss_Link.ln_Name != NULL && TypeOfMem(ss->ss_Link.ln_Name) ? ss->ss_Link.ln_Name : "",
            task_name(ss->ss_Owner),
            ss->ss_Owner != NULL ? (int)ss->ss_Owner->tc_State : -1,
            (int)ss->ss_NestCount, (int)ss->ss_QueueCount);
        return;
    }
    put("  no semaphore request on the stack (a one-shot wait)\n");
}

/* pc, lr and the frame-pointer chain ([fp] = previous fp, [fp + 8] = return
 * address) inside the task's stack, from its saved context. */
static void trace(struct Task *t)
{
    struct ETask *et = t->tc_UnionETask.tc_ETask;
    struct ExceptionContext *ctx;
    struct traced *tr;
    IPTR fp;

    if (traced_count >= MAX_TRACED || t == FindTask(NULL) || !(t->tc_Flags & TF_ETASK) ||
        !readable(et) || !readable(et->et_RegFrame))
        return;
    ctx = et->et_RegFrame;
    /* A file handle with a flush in progress among the saved registers and
     * the top of the stack: the one a flush waits for. */
    {
        IPTR *w = (IPTR *)ctx->sp, *end = (IPTR *)t->tc_SPUpper;
        int r;

        for (r = 0; r < 29; ++r) {
            struct FileHandle *fh = (struct FileHandle *)ctx->x[r];

            if (readable(fh) && readable(&fh->fh_End) && (fh->fh_Flags & 0x20) &&
                readable(BADDR(fh->fh_Buf)))
                put("  x%d %p: file handle flags 0x%08lx pos %ld end %ld port %p\n", r,
                    (void *)fh, (unsigned long)fh->fh_Flags, (long)fh->fh_Pos,
                    (long)fh->fh_End, (void *)fh->fh_Type);
        }
        if ((APTR)w >= t->tc_SPLower && (APTR)w < t->tc_SPUpper) {
            for (; w < end; ++w) {
                struct FileHandle *fh = (struct FileHandle *)*w;

                if (readable(fh) && readable(&fh->fh_End) && (fh->fh_Flags & 0x20) &&
                    readable(BADDR(fh->fh_Buf)))
                    put("  stack +0x%lx %p: file handle flags 0x%08lx pos %ld end %ld port %p\n",
                        (unsigned long)((IPTR)w - ctx->sp), (void *)fh,
                        (unsigned long)fh->fh_Flags, (long)fh->fh_Pos, (long)fh->fh_End,
                        (void *)fh->fh_Type);
            }
        }
    }
    /* A timer request on the stack (Delay's, DoIO's): whether timer.device
     * still holds it (NT_MESSAGE) or replied (NT_REPLYMSG), and its port. */
    {
        struct Device *timer = (struct Device *)FindName(&SysBase->DeviceList, "timer.device");
        IPTR *w = (IPTR *)ctx->sp;

        if (timer != NULL && (APTR)w >= t->tc_SPLower && (APTR)w < t->tc_SPUpper) {
            for (; w < (IPTR *)t->tc_SPUpper; ++w) {
                struct timerequest *tr2;
                struct MsgPort *mp;

                if (*w != (IPTR)timer)
                    continue;
                tr2 = (struct timerequest *)((UBYTE *)w - offsetof(struct IORequest, io_Device));
                mp = tr2->tr_node.io_Message.mn_ReplyPort;
                put("  timer request %p: type %d command %d flags 0x%x error %d time %lu.%06lu unit %p\n",
                    (void *)tr2, (int)tr2->tr_node.io_Message.mn_Node.ln_Type,
                    (int)tr2->tr_node.io_Command, (unsigned)tr2->tr_node.io_Flags,
                    (int)tr2->tr_node.io_Error, (unsigned long)tr2->tr_time.tv_secs,
                    (unsigned long)tr2->tr_time.tv_micro, (void *)tr2->tr_node.io_Unit);
                if (readable(mp)) {
                    struct Node *m;
                    int queued = 0;

                    for (m = mp->mp_MsgList.lh_Head; readable(m) && m->ln_Succ != NULL; m = m->ln_Succ)
                        if (m == (struct Node *)tr2)
                            queued = 1;
                    put("    reply port %p: sigbit %d task \"%s\" flags %d, the request %s its list\n",
                        (void *)mp, (int)mp->mp_SigBit,
                        readable(mp->mp_SigTask) ? task_name(mp->mp_SigTask) : "?",
                        (int)mp->mp_Flags, queued ? "in" : "not in");
                }
            }
        }
    }
    tr = &traced[traced_count++];
    strncpy(tr->name, task_name(t), sizeof(tr->name) - 1);
    tr->address[tr->frames++] = ctx->pc;
    tr->address[tr->frames++] = ctx->lr;
    for (fp = ctx->fp; tr->frames < MAX_FRAMES; fp = ((IPTR *)fp)[0]) {
        IPTR *f = (IPTR *)fp;

        if (!readable((void *)fp) || (APTR)fp < t->tc_SPLower || (APTR)(fp + 48) > t->tc_SPUpper)
            break;
        /* What the frame saved above its fp/lr pair (x19..x22 in the usual
         * prologue), and the first word each points to. */
        put("  fp %p lr %p: +0x10 %p +0x18 %p +0x20 %p +0x28 %p", (void *)fp, (void *)f[1],
            (void *)f[2], (void *)f[3], (void *)f[4], (void *)f[5]);
        if (readable((void *)f[5]))
            put(" [+0x28] -> 0x%08lx", (unsigned long)*(ULONG *)f[5]);
        if (readable((void *)f[4]))
            put(" [+0x20] -> 0x%08lx", (unsigned long)*(ULONG *)f[4]);
        put("\n");
        tr->address[tr->frames++] = ((IPTR *)fp)[1];
        if (((IPTR *)fp)[0] <= fp)
            break;
    }
}

/* The ROM module whose resident tag and end bracket an address, for the
 * modules debug.library has no symbols for. */
static const char *resident_of(IPTR address, IPTR *offset)
{
    struct Resident **list = (struct Resident **)SysBase->ResModules;
    const char *best = NULL;
    IPTR best_start = 0;

    if (list == NULL)
        return NULL;
    while (*list != NULL) {
        struct Resident *r;

        if ((IPTR)*list & 0x1) {
            list = (struct Resident **)((IPTR)*list & ~(IPTR)1);
            continue;
        }
        r = *list++;
        if ((IPTR)r <= address && address < (IPTR)r->rt_EndSkip && (IPTR)r >= best_start) {
            best = r->rt_Name != NULL ? (const char *)r->rt_Name : "?";
            best_start = (IPTR)r;
        }
    }
    *offset = address - best_start;
    return best;
}

/* The library vector nearest below an address, among every library and
 * device: the public function it is in, or one before it. */
static const char *vector_of(IPTR address, ULONG *lvo, IPTR *offset)
{
    struct List *lists[2] = { &SysBase->LibList, &SysBase->DeviceList };
    const char *best = NULL;
    IPTR best_addr = 0;
    ULONG i;

    Forbid();
    for (i = 0; i < 2; ++i) {
        struct Node *n;

        for (n = lists[i]->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
            struct Library *lib = (struct Library *)n;
            ULONG count = lib->lib_NegSize / LIB_VECTSIZE, v;

            if (count > 4096)
                continue;
            for (v = 1; v <= count; ++v) {
                IPTR f;

                if (!readable((UBYTE *)lib - v * LIB_VECTSIZE))
                    break;
                f = (IPTR)__AROS_GETVECADDR(lib, v);

                if (f <= address && f > best_addr && address - f < 0x100000) {
                    best_addr = f;
                    best = n->ln_Name;
                    *lvo = v;
                }
            }
        }
    }
    Permit();
    *offset = address - best_addr;
    return best;
}

static void collect_handlers(void)
{
    struct input_view *in = (struct input_view *)FindName(&SysBase->DeviceList, "input.device");
    struct MinNode *n;

    if (!readable(in))
        return;
    for (n = in->HandlerList.mlh_Head; readable(n) && n->mln_Succ != NULL && handler_count < MAX_HANDLERS;
         n = n->mln_Succ) {
        struct Interrupt *is = (struct Interrupt *)n;

        strncpy(handlers[handler_count].name,
            is->is_Node.ln_Name != NULL && TypeOfMem(is->is_Node.ln_Name) ? is->is_Node.ln_Name : "?",
            sizeof(handlers[0].name) - 1);
        handlers[handler_count].pri = is->is_Node.ln_Pri;
        handlers[handler_count].code = (APTR)is->is_Code;
        ++handler_count;
    }
}

static void print_handlers(void)
{
    ULONG i;

    for (i = 0; i < handler_count; ++i) {
        char *module = NULL, *symbol = NULL;
        ULONG lvo = 0;
        IPTR offset = 0;
        const char *lib = NULL;

        if (DebugBase != NULL)
            DecodeLocation(handlers[i].code, DL_ModuleName, (IPTR)&module, DL_SymbolName, (IPTR)&symbol,
                TAG_DONE);
        if (module == NULL)
            lib = vector_of((IPTR)handlers[i].code, &lvo, &offset);
        if (module != NULL)
            Printf("input handler \"%s\" pri %ld code %p %s %s\n", (IPTR)handlers[i].name,
                (LONG)handlers[i].pri, handlers[i].code, (IPTR)module, (IPTR)(symbol ? symbol : "?"));
        else if (lib != NULL && offset < 0x40000)
            Printf("input handler \"%s\" pri %ld code %p near \"%s\" LVO %lu +0x%lx\n",
                (IPTR)handlers[i].name, (LONG)handlers[i].pri, handlers[i].code, (IPTR)lib,
                (ULONG)lvo, (ULONG)offset);
        else
            Printf("input handler \"%s\" pri %ld code %p in no module (freed?)\n",
                (IPTR)handlers[i].name, (LONG)handlers[i].pri, handlers[i].code);
    }
}

static void print_traces(void)
{
    ULONG i, j;

    for (i = 0; i < traced_count; ++i) {
        Printf("trace \"%s\"\n", (IPTR)traced[i].name);
        for (j = 0; j < traced[i].frames; ++j) {
            char *module = NULL, *symbol = NULL;
            void *start = NULL;

            if (DebugBase != NULL)
                DecodeLocation((APTR)traced[i].address[j], DL_ModuleName, (IPTR)&module,
                    DL_SymbolName, (IPTR)&symbol, DL_SymbolStart, (IPTR)&start, TAG_DONE);
            if (module == NULL) {
                IPTR offset = 0;
                const char *rom = resident_of(traced[i].address[j], &offset);

                ULONG lvo = 0;

                if (rom != NULL) {
                    Printf("  frame %lu %p resident \"%s\"+0x%lx\n", (ULONG)j,
                        (APTR)traced[i].address[j], (IPTR)rom, (ULONG)offset);
                    continue;
                }
                rom = vector_of(traced[i].address[j], &lvo, &offset);
                if (rom != NULL) {
                    Printf("  frame %lu %p near \"%s\" LVO %lu +0x%lx\n", (ULONG)j,
                        (APTR)traced[i].address[j], (IPTR)rom, (ULONG)lvo, (ULONG)offset);
                    continue;
                }
            }
            Printf("  frame %lu %p %s %s+0x%lx\n", (ULONG)j, (APTR)traced[i].address[j],
                (IPTR)(module != NULL ? module : "?"), (IPTR)(symbol != NULL ? symbol : "?"),
                (ULONG)(start != NULL ? traced[i].address[j] - (IPTR)start : 0));
        }
    }
}

/* A process's output and error handles: dos keeps a flush in progress in
 * fh_Flags bit 0x20 (FHF_FLUSHING), and a flush waits while it is set. */
static void handle_line(const char *what, BPTR b)
{
    struct FileHandle *fh = BADDR(b);

    if (b == BNULL || !readable(fh))
        return;
    put("  %s %p flags 0x%08lx%s pos %ld end %ld\n", what, (void *)fh,
        (unsigned long)fh->fh_Flags, (fh->fh_Flags & 0x20) ? " FLUSHING" : "",
        (long)fh->fh_Pos, (long)fh->fh_End);
}

static void tasks(struct List *list, const char *which)
{
    struct Node *n;

    for (n = list->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        struct Task *t = (struct Task *)n;

        put("task \"%s\" (%s) state %d pri %d sigwait 0x%08lx sigrecvd 0x%08lx\n",
            task_name(t), which, (int)t->tc_State, (int)t->tc_Node.ln_Pri,
            (unsigned long)t->tc_SigWait, (unsigned long)t->tc_SigRecvd);
        if (t->tc_Node.ln_Type == NT_PROCESS) {
            struct Process *pr = (struct Process *)t;

            handle_line("output", pr->pr_COS);
            handle_line("error", pr->pr_CES);
        }
        if (t->tc_SigWait & SIGF_SINGLE)
            waited_semaphore(t);
        if ((t->tc_SigWait & SIGF_SINGLE) ||
            (task_filter != NULL && strstr(task_name(t), task_filter) != NULL))
            trace(t);
    }
}

int main(void)
{
    IPTR args[1] = { 0 };
    struct RDArgs *rda = ReadArgs("TASK/K", args, NULL);
    struct IntuitionBase *ib = (struct IntuitionBase *)OpenLibrary("intuition.library", 0);
    struct Screen *s;
    struct Node *n;
    ULONG free_locks = 0;

    if (ib == NULL)
        return RETURN_FAIL;
    if (rda != NULL && args[0])
        task_filter = (const char *)args[0];
    DebugBase = OpenLibrary("debug.library", 0);
    Forbid();
    for (s = ib->FirstScreen; s != NULL; s = s->NextScreen) {
        struct Layer_Info *li = &s->LayerInfo;
        struct Layer *L;
        char what[160];

        snprintf(what, sizeof(what), "screen \"%s\": layerinfo", safe((const char *)s->Title));
        if (li->Lock.ss_Owner != NULL || li->Lock.ss_QueueCount >= 0)
            lock_line("", what, &li->Lock);
        else
            put("screen \"%s\": layerinfo free\n", safe((const char *)s->Title));
        for (L = li->top_layer; L != NULL; L = L->back) {
            struct Window *w = L->Window;

            if (L->Lock.ss_Owner == NULL && L->Lock.ss_QueueCount < 0) {
                ++free_locks;
                continue;
            }
            snprintf(what, sizeof(what), "layer %p %d,%d-%d,%d window \"%s\"", (void *)L,
                (int)L->bounds.MinX, (int)L->bounds.MinY, (int)L->bounds.MaxX,
                (int)L->bounds.MaxY, w != NULL ? safe((const char *)w->Title) : "-");
            lock_line("  ", what, &L->Lock);
        }
    }
    for (n = SysBase->SemaphoreList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        struct SignalSemaphore *ss = (struct SignalSemaphore *)n;
        char what[96];

        if (ss->ss_Owner == NULL && ss->ss_QueueCount < 0) {
            ++free_locks;
            continue;
        }
        snprintf(what, sizeof(what), "semaphore \"%s\"", safe(n->ln_Name));
        lock_line("", what, ss);
    }
    put("free locks %lu\n", (unsigned long)free_locks);
    tasks(&SysBase->TaskWait, "waiting");
    tasks(&SysBase->TaskReady, "ready");
    collect_handlers();
    Permit();

    PutStr(out);
    print_handlers();
    print_traces();
    if (DebugBase != NULL)
        CloseLibrary(DebugBase);
    if (rda != NULL)
        FreeArgs(rda);
    CloseLibrary((struct Library *)ib);
    return RETURN_OK;
}
