/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 * PX3 exercises the real DOS handler. Install the candidate as SYS:PIPEHDLR.
 * The private PX3PIPE mount stays resident until shutdown (handler has no DIE).
 */

#include <dos/dosextens.h>
#include <dos/pipe.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

/* Headless RunCommand fixture: initialize library bases without console I/O. */
int __nostdiowin = 1;
int __nocommandline = 1;
int __nowbsupport = 1;
struct WBStartup *WBenchMsg;

static void logline(const char *s)
{
    while (*s) RawPutChar(*s++);
}

static IPTR packet(BPTR stream, LONG action, IPTR a2, IPTR a3)
{
    struct FileHandle *fh = BADDR(stream);
    return DoPkt(fh->fh_Type, action, fh->fh_Arg1, a2, a3, 0, 0);
}

/* Bounded observation; the outer VM watchdog also catches a blocked Read. */
static BOOL notified(ULONG mask)
{
    unsigned int tick;
    for (tick = 0; tick < 100; ++tick)
    {
        if (SetSignal(0, mask) & mask) return TRUE;
        Delay(1);
    }
    return FALSE;
}

int main(void)
{
    struct DosList *mount = NULL;
    UBYTE *handler = NULL;
    BPTR reader = BNULL, writer = BNULL;
    LONG signal = -1;
    ULONG mask = 0;
    LONG result = RETURN_FAIL;
    TEXT name[128], data[64];
    const char path[] = "SYS:PIPEHDLR";
    unsigned int i;
    BOOL mounted = FALSE;
#define CHECK(expr, reason) do { if (!(expr)) { logline("[PX3] FAIL " reason "\n"); goto out; } } while (0)
    logline("[PX3] begin real DOS pipe readiness\n");
    mount = MakeDosEntry("PX3PIPE", DLT_DEVICE);
    handler = AllocVec(AROS_BSTR_MEMSIZE4LEN(sizeof(path)), MEMF_PUBLIC | MEMF_CLEAR);
    CHECK(mount && handler, "mount allocation");
    mount->dol_misc.dol_handler.dol_Handler = MKBADDR(handler);
    AROS_BSTR_setstrlen(mount->dol_misc.dol_handler.dol_Handler, sizeof(path)-1);
    for (i = 0; i < sizeof(path); ++i)
        AROS_BSTR_putchar(mount->dol_misc.dol_handler.dol_Handler, i, path[i]);
    mount->dol_misc.dol_handler.dol_StackSize = 32768;
    CHECK(AddDosEntry(mount), "private mount");
    mounted = TRUE;
    writer = Open("PX3PIPE:*", MODE_NEWFILE);
    CHECK(writer, "open writer");
    CHECK(NameFromFH(writer, name, sizeof(name)), "pipe name");
    reader = Open(name, MODE_OLDFILE);
    CHECK(reader, "open reader");
    CHECK(IsInteractive(reader) && IsInteractive(writer), "interactive handles");
    CHECK(packet(reader, ACTION_PIPE_SET_NONBLOCK, 1, 0), "set nonblocking");
    CHECK(Read(reader, data, sizeof(data)) == -1 && IoErr() == ERROR_PIPE_WOULD_BLOCK,
          "nonblocking empty read");
    CHECK(Read(reader, data, 0) == 0, "zero length read");
    signal = AllocSignal(-1);
    CHECK(signal >= 0, "signal allocation");
    mask = 1UL << signal;
    SetSignal(0, mask);
    CHECK(packet(reader, ACTION_PIPE_READ_NOTIFY, mask, (IPTR)FindTask(NULL)), "register");
    CHECK(!packet(writer, ACTION_PIPE_READ_NOTIFY, mask, (IPTR)FindTask(NULL)), "reject writer registration");
    CHECK(!packet(reader, ACTION_PIPE_READ_NOTIFY, mask, 0), "reject missing recipient");
    CHECK(Write(writer, "A", 1) == 1 && notified(mask), "notify on data");
    CHECK(Read(reader, data, sizeof(data)) == 1 && data[0] == 'A', "nonblocking partial read");
    CHECK(packet(reader, ACTION_PIPE_READ_NOTIFY, 0, 0), "unregister");
    SetSignal(0, mask);
    CHECK(Write(writer, "B", 1) == 1, "control write");
    Delay(2);
    CHECK(!(SetSignal(0, mask) & mask), "unregistered signal");
    logline("[PX3] (control) unregistered: signals=0\n");
    CHECK(packet(reader, ACTION_PIPE_READ_NOTIFY, mask, (IPTR)FindTask(NULL)) && notified(mask),
          "notify if already readable");
    CHECK(Read(reader, data, sizeof(data)) == 1 && data[0] == 'B', "read existing data");
    CHECK(packet(reader, ACTION_PIPE_SET_NONBLOCK, 0, 0), "clear nonblocking");
    CHECK(Write(writer, "C", 1) == 1, "blocking-mode write");
    CHECK(Read(reader, data, sizeof(data)) == 1 && data[0] == 'C', "blocking partial read");
    CHECK(packet(reader, ACTION_PIPE_SET_NONBLOCK, 1, 0), "restore nonblocking");
    SetSignal(0, mask);
    Close(writer); writer = BNULL;
    CHECK(notified(mask) && Read(reader, data, sizeof(data)) == 0, "EOF readiness");
    CHECK(packet(reader, ACTION_PIPE_READ_NOTIFY, 0, 0), "final unregister");
    result = RETURN_OK;
out:
    if (reader) { packet(reader, ACTION_PIPE_READ_NOTIFY, 0, 0); Close(reader); }
    if (writer) Close(writer);
    if (signal >= 0) { SetSignal(0, mask); FreeSignal(signal); }
    /* A live handler owns its mount; removing it would leave a dangling node. */
    if (!mounted) { if (mount) FreeDosEntry(mount); if (handler) FreeVec(handler); }
    if (result == RETURN_OK)
        logline("[PX3] PASS pipe: nonblock-empty=would-block notify-on-data=1 notify-if-already-readable=1 partial-read-returns=1 interactive=1\n");
    return result;
#undef CHECK
}
