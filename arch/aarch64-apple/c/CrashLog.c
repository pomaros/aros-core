/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: CrashLog [LAST n] [REGS] [WATCH] - the kernel's record of the last
          task crashes (aros/apple/crashlog.h).

    Without WATCH it prints every recorded crash, newest last: the task, its
    CPU, whether the kernel suspended it or halted, the decoded fault, ELR
    and LR as module+offset+function, the backtrace, the registers that
    point into code and a stack excerpt; REGS adds x0-x30. LAST n prints
    only the n newest. The last line is "[crashlog] count=<n>
    suspended=<n>", so a remote reader can tell whether anything new
    happened since its last look.

    WATCH stays running (CTRL-C ends it) and, for every crash recorded
    after it started, posts a "Program suspended" requester with an OK
    button from this process, never from the crashed task: nothing the
    crashed task holds can block it, and answering it changes nothing.
    It also prints the entry. A requester needs Intuition's input handler;
    when the crashed task is input.device itself the requester cannot
    appear and the printed entry is what remains.
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <aros/apple/crashlog.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/kernel.h>

#include <string.h>

#include "../kernel/task_crash_policy.h"

APTR KernelBase;
struct IntuitionBase *IntuitionBase;

static void put_place(const char *label, const struct AROSAppleCrashPlace *p)
{
    char line[200];
    struct AROSAppleCrashText t;

    crash_text_init(&t, line, sizeof(line));
    crash_text_str(&t, "  ");
    crash_text_str(&t, label);
    crash_text_char(&t, ' ');
    crash_text_hex(&t, p->address);
    crash_text_char(&t, ' ');
    AROSAppleCrashFormatPlace(&t, p);
    crash_text_char(&t, '\n');
    PutStr(line);
}

/* A consistent copy of entry n, or FALSE when it was overwritten. */
static BOOL copy_entry(const struct AROSAppleCrashLog *log, uint64_t n,
    struct AROSAppleCrashEntry *out)
{
    const struct AROSAppleCrashEntry *e =
        &log->entries[(n - 1) % log->capacity];

    if (e->sequence != n)
        return FALSE;
    CopyMem((APTR)e, out, sizeof(*out));
    return e->sequence == n && out->sequence == n;
}

static void print_entry(const struct AROSAppleCrashEntry *e, BOOL regs)
{
    char line[220];
    struct AROSAppleCrashText t;
    ULONG i;

    crash_text_init(&t, line, sizeof(line));
    crash_text_str(&t, "crash ");
    crash_text_dec(&t, e->sequence);
    crash_text_str(&t, ": \"");
    crash_text_str(&t, e->task_name);
    crash_text_str(&t, "\" ");
    crash_text_str(&t, e->task_type == 13 ? "process " : e->task_type == 1 ? "task " : "");
    crash_text_hex(&t, e->task);
    crash_text_str(&t, " cpu ");
    crash_text_dec(&t, e->cpu);
    crash_text_str(&t, " ");
    crash_text_str(&t, e->verdict == AROS_APPLE_CRASH_SUSPEND ? "suspended" : "HALTED: ");
    if (e->verdict != AROS_APPLE_CRASH_SUSPEND)
        crash_text_str(&t, AROSAppleCrashVerdictName(e->verdict));
    if (e->flags & AROS_APPLE_CRASH_IN_FORBID)
        crash_text_str(&t, " in-Forbid");
    if (e->flags & AROS_APPLE_CRASH_IN_DISABLE)
        crash_text_str(&t, " in-Disable");
    if (e->flags & AROS_APPLE_CRASH_STACK_OUTSIDE)
        crash_text_str(&t, " SP-outside-stack");
    crash_text_char(&t, '\n');
    PutStr(line);
    PutStr("  fault ");
    PutStr(e->fault);
    PutStr("\n");
    put_place("elr", &e->elr_place);
    put_place("lr ", &e->lr_place);
    for (i = 0; i < e->frame_count && i < AROS_APPLE_CRASH_FRAMES; ++i)
        put_place("frame", &e->frames[i]);
    for (i = 0; i < e->reg_place_count && i < AROS_APPLE_CRASH_REG_PLACES; ++i) {
        char label[8];
        struct AROSAppleCrashText l;

        crash_text_init(&l, label, sizeof(label));
        crash_text_char(&l, 'x');
        crash_text_dec(&l, e->reg_places[i].reg);
        put_place(label, &e->reg_places[i].place);
    }
    crash_text_init(&t, line, sizeof(line));
    crash_text_str(&t, "  esr ");
    crash_text_hex(&t, e->esr);
    crash_text_str(&t, " far ");
    crash_text_hex(&t, e->far);
    crash_text_str(&t, " sp ");
    crash_text_hex(&t, e->sp);
    crash_text_str(&t, " stack ");
    crash_text_hex(&t, e->stack_lower);
    crash_text_str(&t, "..");
    crash_text_hex(&t, e->stack_upper);
    crash_text_char(&t, '\n');
    PutStr(line);
    if (e->stack_count) {
        crash_text_init(&t, line, sizeof(line));
        crash_text_str(&t, "  sp:");
        for (i = 0; i < e->stack_count && i < AROS_APPLE_CRASH_STACK_WORDS; ++i) {
            crash_text_char(&t, ' ');
            crash_text_hex(&t, e->stack[i]);
        }
        crash_text_char(&t, '\n');
        PutStr(line);
    }
    if (regs)
        for (i = 0; i < 31; i += 4) {
            ULONG k;

            crash_text_init(&t, line, sizeof(line));
            crash_text_str(&t, " ");
            for (k = i; k < i + 4 && k < 31; ++k) {
                crash_text_str(&t, " x");
                crash_text_dec(&t, k);
                crash_text_char(&t, '=');
                crash_text_hex(&t, e->x[k]);
            }
            crash_text_char(&t, '\n');
            PutStr(line);
        }
}

static void notify(const struct AROSAppleCrashEntry *e)
{
    char body[320];
    struct AROSAppleCrashText t;
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (CONST_STRPTR)"Program suspended",
        (CONST_STRPTR)body, (CONST_STRPTR)"OK" };

    if (IntuitionBase == NULL)
        return;
    crash_text_init(&t, body, sizeof(body));
    crash_text_str(&t, "\"");
    crash_text_str(&t, e->task_name);
    crash_text_str(&t, "\" crashed and was suspended.\nThe rest of the system keeps running.\n\n");
    crash_text_str(&t, e->fault);
    crash_text_str(&t, "\nat ");
    AROSAppleCrashFormatPlace(&t, &e->elr_place);
    crash_text_str(&t, "\n\nC:CrashLog shows the full record.");
    /* EasyRequest formats: no % may survive from the record. */
    for (ULONG i = 0; body[i]; ++i)
        if (body[i] == '%')
            body[i] = ' ';
    EasyRequestArgs(NULL, &es, NULL, NULL);
}

int main(void)
{
    IPTR args[3] = { 0, 0, 0 };
    struct RDArgs *rda = ReadArgs("LAST/K/N,REGS/S,WATCH/S", args, NULL);
    const struct AROSAppleCrashLog *log;
    IPTR attr;
    uint64_t count, first, n;
    struct AROSAppleCrashEntry entry;

    if (!rda) {
        PrintFault(IoErr(), "CrashLog");
        return RETURN_ERROR;
    }
    KernelBase = OpenResource("kernel.resource");
    attr = KernelBase ? KrnGetSystemAttr(AROS_APPLE_KATTR_CRASH_LOG) : (IPTR)-1;
    log = (attr != (IPTR)-1 && attr != 0) ? (const struct AROSAppleCrashLog *)attr : NULL;
    if (!log || log->version != AROS_APPLE_CRASH_LOG_VERSION ||
            log->size < sizeof(*log) || log->entry_size != sizeof(entry) ||
            log->capacity != AROS_APPLE_CRASH_LOG_CAPACITY) {
        PutStr("CrashLog: this kernel keeps no crash log\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }

    if (args[2]) {
        uint64_t seen = log->count;

        IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
        PutStr("CrashLog: watching, CTRL-C ends\n");
        while (!(SetSignal(0, 0) & SIGBREAKF_CTRL_C)) {
            count = log->count;
            for (n = seen + 1; n <= count; ++n)
                if (copy_entry(log, n, &entry)) {
                    print_entry(&entry, FALSE);
                    Flush(Output());
                    notify(&entry);
                }
            seen = count;
            Delay(TICKS_PER_SECOND);
        }
        if (IntuitionBase)
            CloseLibrary((struct Library *)IntuitionBase);
        FreeArgs(rda);
        return RETURN_OK;
    }

    count = log->count;
    first = count > log->capacity ? count - log->capacity + 1 : 1;
    if (args[0] && *(LONG *)args[0] > 0 && (uint64_t)*(LONG *)args[0] < count - first + 1)
        first = count - (uint64_t)*(LONG *)args[0] + 1;
    for (n = first; n <= count; ++n) {
        if (copy_entry(log, n, &entry))
            print_entry(&entry, args[1] != 0);
        else
            Printf("crash %lu: overwritten while reading\n", (unsigned long)n);
    }
    Printf("[crashlog] count=%lu suspended=%lu\n", (unsigned long)log->count,
        (unsigned long)log->suspended);
    FreeArgs(rda);
    return RETURN_OK;
}
