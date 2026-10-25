/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: SystemUpdate APPLY|READY - install a delivered system package onto
    AFSSYS: before SYS: moves there.

    usb-link and PkgRecv leave WORK:PkgDepot/<snapshot>/, a pkg channel
    (index, objects/) whose index lists the aros-system package, with READY
    last. For a snapshot with READY that AFSSYS:S/SystemSnapshot does not
    already name, this command writes and flushes S/SystemUpdate.pending
    with the snapshot, then runs C:PkgInstall on that channel: INSTALL
    TAKEOVER the first time (the volume was set up outside pkg: its files
    become the package's), UPGRADE from then on. libpkg checks the
    signature and every file's digest before anything is written, places
    the files, removes the ones the previous version had and this one
    drops, keeps a person's edits of the package's configuration files,
    records the package and flushes the volume; its typed progress goes to
    the kernel log through PkgInstall, so the panel shows the update moving
    before the desktop exists. The publisher's key is the first line of
    the RAM image's S/EnableDevelopmentUpdates: only a package signed by
    the key the boot image names is installed.

    PkgInstall's receipt (AFSSYS:S/PkgInstall.receipt) must say PASS. Only
    then is the snapshot recorded (the previous one kept in
    SystemSnapshot.old), the pending marker removed and the snapshot
    directory deleted. A pending marker blocks installed-SYS startup until
    the same snapshot has been applied: an interrupted change is finished
    by running the same operation again, which libpkg does. A receipt
    with "root-written: no" (refused by the dry run or the checks before
    it: a conflict, a foreign signer) means SYS is the system it was: the
    pending marker goes, the snapshot moves to WORK:PkgRefused, the window
    shows why for a few seconds and the installed system starts. Any other
    failure leaves the marker and a window that stays until it is closed,
    and the recovery desktop starts. Every step is
    reported on the kernel log as "[update] ...". RC 0 when nothing was
    due or the update was applied, 10 when it was refused, 20 on a
    failure. READY only answers whether a snapshot with READY is waiting:
    RC 0 if one is, 5 if not.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <proto/intuition.h>
#include <intuition/screens.h>

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "system_update_index.h"

#define DEPOT       "WORK:PkgDepot"
#define TARGET      "AFSSYS:"
#define PENDING     TARGET "S/SystemUpdate.pending"
#define RECEIPT     TARGET "S/PkgInstall.receipt"
#define INSTALLED   TARGET ".pkg/db/" SYSTEM_UPDATE_PACKAGE
#define KEYFILE     "SYS0:S/EnableDevelopmentUpdates"
#define INSTALLER   "C:PkgInstall"

APTR KernelBase;
static BOOL verbose;
static struct DateStamp started;
/* The window a person watches while an update is applied. Before the
 * desktop the display already belongs to the graphics driver, so the
 * kernel log no longer reaches the panel; this console does. */
static BPTR progress_window;
static struct Screen *update_screen;
struct IntuitionBase *IntuitionBase;
static void open_update_screen(void);
/* The last APPLY was refused before any write to SYS. */
static BOOL refused;

/* The update runs before IPrefs and the decorator: a window on Workbench
 * would open that screen early, with the built-in font, frames and no
 * backdrop, and keep it so. Its own public screen closes before the desktop. */
#define UPDATE_SCREEN   "AROSUpdate"
#define PROGRESS_WINDOW "CON:40/60/1100/420/AROS system update/AUTO/SCREEN" UPDATE_SCREEN
/* A failure that leaves SYS in doubt stays on screen until it is closed. */
#define FAILURE_WINDOW  "CON:40/60/1100/420/AROS system update failed/AUTO/WAIT/CLOSE/SCREEN" UPDATE_SCREEN
#define REFUSED_DIR     "WORK:PkgRefused"
/* How long a refusal stays readable before the system starts. */
#define REFUSED_SECONDS 8

/* Seconds since the command started, for the progress lines. */
static ULONG elapsed(void)
{
    struct DateStamp now;
    DateStamp(&now);
    return (now.ds_Days - started.ds_Days) * 86400UL
         + (now.ds_Minute - started.ds_Minute) * 60UL
         + (now.ds_Tick - started.ds_Tick) / TICKS_PER_SECOND;
}

/* Every message goes to the kernel log (the screen log before the desktop,
 * the DockChannel route) and to the command's own output, so a Shell that
 * runs the command by hand sees it too. */
static void report(const char *format, ...)
{
    va_list args;
    BPTR out = Output();

    va_start(args, format);
    if (KernelBase) {
        Forbid();
        KrnBug(format, args);
        Permit();
    }
    va_end(args);
    if (out || progress_window) {
        char line[256];
        va_start(args, format);
        vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        if (out) {
            FPuts(out, line);
            Flush(out);
        }
        if (progress_window) {
            FPuts(progress_window, line);
            Flush(progress_window);
        }
    }
}

static BOOL exists(CONST_STRPTR path)
{
    BPTR lock = Lock(path, SHARED_LOCK);

    if (!lock)
        return FALSE;
    UnLock(lock);
    return TRUE;
}

static BOOL read_line(CONST_STRPTR path, char *out, LONG size)
{
    BPTR fh = Open(path, MODE_OLDFILE);
    LONG n;

    out[0] = 0;
    if (!fh)
        return FALSE;
    n = Read(fh, out, size - 1);
    Close(fh);
    if (n < 0)
        return FALSE;
    out[n] = 0;
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
        out[--n] = 0;
    return TRUE;
}

static BOOL write_text(CONST_STRPTR path, CONST_STRPTR text)
{
    BPTR fh = Open(path, MODE_NEWFILE);
    LONG n = strlen(text);
    BOOL ok;

    if (!fh)
        return FALSE;
    ok = Write(fh, (APTR)text, n) == n && Write(fh, "\n", 1) == 1;
    return Close(fh) && ok;
}

static BOOL flush_system(void)
{
    struct MsgPort *port = DeviceProc(TARGET);
    return port && DoPkt(port, ACTION_FLUSH, 0, 0, 0, 0, 0);
}

static BOOL finish_pending(void)
{
    if (exists(PENDING) && !DeleteFile(PENDING))
        return FALSE;
    return flush_system();
}

/* READY belongs to the transport, not to SystemUpdate: boot generations and
 * other packages share this depot. Classify the index before anything else.
 * Refuse malformed indexes or ambiguous candidates without a write. */
static int find_snapshot(char *id, LONG size)
{
    BPTR lock = Lock(DEPOT, SHARED_LOCK);
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    char path[160], index[SYSTEM_UPDATE_INDEX_MAX];
    int found = 0;

    id[0] = 0;
    if (lock && fib && Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType <= 0 || strlen(fib->fib_FileName) >= (size_t)size)
                continue;
            snprintf(path, sizeof(path), DEPOT "/%s/READY", fib->fib_FileName);
            if (exists(path)) {
                BPTR index_file;
                LONG index_size;
                int kind;
                snprintf(path, sizeof(path), DEPOT "/%s/index", fib->fib_FileName);
                index_file = Open(path, MODE_OLDFILE);
                if (!index_file)
                    continue;
                index_size = Read(index_file, index, sizeof(index));
                Close(index_file);
                kind = index_size < 0 ? -1 :
                    SystemUpdateIndexKind(index, (size_t)index_size);
                if (!kind)
                    continue;
                if (kind < 0 || found) {
                    report("[update] invalid or ambiguous system package candidates; refused\n");
                    found = -1;
                    break;
                }
                strcpy(id, fib->fib_FileName);
                found = 1;
            }
        }
    }
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    if (lock)
        UnLock(lock);
    return found;
}

static void delete_tree(CONST_STRPTR path)
{
    char command[200];

    snprintf(command, sizeof(command), "Delete \"%s\" ALL QUIET FORCE", path);
    SystemTags(command, SYS_Input, Open("NIL:", MODE_OLDFILE),
        SYS_Output, Open("NIL:", MODE_NEWFILE), TAG_DONE);
}

/* The receipt's verdict line, exactly "verdict: PASS". The file is small
 * (key: value lines); a receipt that cannot be read is no pass. */
static BOOL receipt_passed(void)
{
    BPTR fh = Open(RECEIPT, MODE_OLDFILE);
    char text[4096];
    LONG n;

    if (!fh)
        return FALSE;
    n = Read(fh, text, sizeof(text) - 1);
    Close(fh);
    if (n <= 0)
        return FALSE;
    text[n] = 0;
    return strncmp(text, "verdict: PASS\n", 14) == 0 || strstr(text, "\nverdict: PASS\n") != NULL;
}

/* One "key: value" field of the receipt; FALSE when it is absent. */
static BOOL receipt_field(const char *key, char *out, LONG size)
{
    BPTR fh = Open(RECEIPT, MODE_OLDFILE);
    char text[4096], want[48];
    const char *at;
    LONG n, i = 0;

    out[0] = 0;
    if (!fh)
        return FALSE;
    n = Read(fh, text, sizeof(text) - 1);
    Close(fh);
    if (n <= 0)
        return FALSE;
    text[n] = 0;
    snprintf(want, sizeof(want), "\n%s: ", key);
    at = strstr(text, want);
    if (!at)
        return FALSE;
    at += strlen(want);
    while (at[i] && at[i] != '\n' && i + 1 < size) {
        out[i] = at[i];
        ++i;
    }
    out[i] = 0;
    return TRUE;
}

/* PkgInstall on the delivered channel, with the command's own output (the
 * boot script's RAM:update.log, or the Shell) as its output. */
static LONG run_installer(const char *id, BOOL first)
{
    char command[400];
    LONG rc;

    snprintf(command, sizeof(command),
        INSTALLER " DEPOT \"" DEPOT "/%s\" ROOT " TARGET " PACKAGE " SYSTEM_UPDATE_PACKAGE
        " OP %s KEYFILE " KEYFILE " RECEIPT " RECEIPT "%s%s",
        id, first ? "INSTALL" : "UPGRADE", first ? " TAKEOVER" : "", verbose ? "" : "");
    report("[update] %s\n", command);
    /* PkgInstall prints every progress event: into the window when there
     * is one, the command's own output otherwise. The receipt keeps the
     * result either way. */
    if (progress_window) {
        /* OUT: duplicates the current output: a second handle on the same
         * window, which SystemTags closes when the command ends. */
        BPTR log = SelectOutput(progress_window);
        BPTR window = Open("OUT:", MODE_NEWFILE);

        SelectOutput(log);
        rc = SystemTags(command, SYS_Input, Open("NIL:", MODE_OLDFILE),
            SYS_Output, window ? window : Open("NIL:", MODE_NEWFILE), TAG_DONE);
    } else
        rc = SystemTags(command, SYS_Input, Open("NIL:", MODE_OLDFILE), TAG_DONE);
    return rc;
}

static int apply(void)
{
    char id[80], applied[80], path[200], pending[80];
    BOOL interrupted = exists(PENDING);
    BOOL first;
    LONG rc;
    int candidate;

    if (!exists(TARGET) || !exists("WORK:") || !exists(DEPOT))
        return interrupted ? RETURN_FAIL : RETURN_OK;
    candidate = find_snapshot(id, sizeof(id));
    if (candidate < 0)
        return RETURN_ERROR;
    if (!candidate)
        return interrupted ? RETURN_FAIL : RETURN_OK;
    if (interrupted && (!read_line(PENDING, pending, sizeof(pending)) || strcmp(pending, id))) {
        report("[update] pending SYS operation differs or is unreadable; recovery required\n");
        return RETURN_FAIL;
    }
    report("[update] found %s\n", id);
    if (read_line(TARGET "S/SystemSnapshot", applied, sizeof(applied)) && strcmp(applied, id) == 0) {
        if (interrupted && !finish_pending())
            return RETURN_FAIL;
        report("[update] %s already applied\n", id);
        snprintf(path, sizeof(path), DEPOT "/%s", id);
        delete_tree(path);
        return RETURN_OK;
    }
    if (!exists(INSTALLER)) {
        report("[update] " INSTALLER " is not in the boot image; refused\n");
        return RETURN_ERROR;
    }
    if (!exists(KEYFILE)) {
        report("[update] " KEYFILE " names no publisher key; refused\n");
        return RETURN_ERROR;
    }
    if (!write_text(PENDING, id) || !flush_system()) {
        report("[update] cannot persist SYS update intent; refused\n");
        return RETURN_FAIL;
    }
    open_update_screen();
    progress_window = update_screen ? Open(PROGRESS_WINDOW, MODE_NEWFILE) : BNULL;
    report("Installing the system update %s.\nThis takes a few minutes; the desktop follows.\n\n", id);

    /* The first install takes the volume over; from then on each update is
     * an upgrade, and an interrupted one is finished by the same call. */
    first = !exists(INSTALLED);
    report("[update] %s " SYSTEM_UPDATE_PACKAGE " from %s\n", first ? "installing" : "upgrading", id);
    rc = run_installer(id, first);
    if (rc != 0 && !receipt_passed()) {
        char written[8], failure[160], aside[200];

        /* Refused before its first write: SYS is the system it was, so it
         * starts as before and the snapshot goes aside, not retried. */
        if (receipt_field("root-written", written, sizeof(written)) && !strcmp(written, "no")) {
            if (!receipt_field("failure", failure, sizeof(failure)))
                strcpy(failure, "-");
            snprintf(path, sizeof(path), DEPOT "/%s", id);
            snprintf(aside, sizeof(aside), REFUSED_DIR "/%s", id);
            if (!exists(REFUSED_DIR)) {
                BPTR dir = CreateDir(REFUSED_DIR);
                if (dir)
                    UnLock(dir);
            }
            if (!Rename(path, aside))
                delete_tree(path);
            if (!finish_pending()) {
                report("[update] %s refused, but the pending marker could not be removed\n", id);
                return RETURN_FAIL;
            }
            report("[update] %s refused, SYS unchanged: %s\n", id, failure);
            report("\nThe update was refused and nothing was changed; the system starts as\n"
                   "before. The snapshot is kept in " REFUSED_DIR " and "
                   RECEIPT " says why.\n");
            refused = TRUE;
            return RETURN_OK;
        }
    }
    if (rc != 0 || !receipt_passed()) {
        report("[update] %s not recorded: PkgInstall rc %ld, receipt %s; see " RECEIPT "\n",
               id, (long)rc, receipt_passed() ? "PASS" : "not PASS");
        return RETURN_FAIL;
    }

    if (!flush_system() || (applied[0] && !write_text(TARGET "S/SystemSnapshot.old", applied))) {
        report("[update] cannot synchronize SYS or retain previous snapshot\n");
        return RETURN_FAIL;
    }
    if (!write_text(TARGET "S/SystemSnapshot", id) || !flush_system()) {
        report("[update] cannot record %s\n", id);
        return RETURN_FAIL;
    }
    if (!finish_pending()) {
        report("[update] committed SYS but pending marker cleanup failed\n");
        return RETURN_FAIL;
    }
    snprintf(path, sizeof(path), DEPOT "/%s", id);
    delete_tree(path);
    report("[update] applied %s: %s, %lu s\n", id, first ? "installed" : "upgraded",
           (unsigned long)elapsed());
    return RETURN_OK;
}

/* A private public screen for the progress console; without one the update
 * still runs and reports to the kernel log and the command's output. */
static void open_update_screen(void)
{
    if (!IntuitionBase)
        IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!IntuitionBase)
        return;
    update_screen = OpenScreenTags(NULL,
        SA_PubName, (IPTR)UPDATE_SCREEN,
        SA_Title, (IPTR)"AROS system update",
        SA_Depth, 8,
        SA_ShowTitle, TRUE,
        SA_Type, PUBLICSCREEN,
        TAG_DONE);
    if (update_screen)
        PubScreenStatus(update_screen, 0);
}

static void close_update_screen(void)
{
    if (update_screen) {
        PubScreenStatus(update_screen, PSNF_PRIVATE);
        CloseScreen(update_screen);
        update_screen = NULL;
    }
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

static int apply_and_close(void)
{
    int rc = apply();

    if (progress_window) {
        if (refused)
            Delay(REFUSED_SECONDS * TICKS_PER_SECOND);
        Close(progress_window);
        progress_window = BNULL;
        if (rc != RETURN_OK) {
            /* SYS may be half changed: this one stays until it is closed. */
            BPTR failure = Open(FAILURE_WINDOW, MODE_NEWFILE);

            if (failure) {
                FPuts(failure, "The system update was not applied and SYS may have changed;\n"
                               "the recovery desktop follows. " RECEIPT " says why.\n"
                               "Close this window.\n");
                Close(failure);
            }
        }
    }
    close_update_screen();
    return rc;
}

int main(void)
{
    IPTR args[3] = { 0, 0, 0 };
    struct RDArgs *rda;

    KernelBase = OpenResource("kernel.resource");
    DateStamp(&started);
    /* No requester: before the desktop nobody can answer one, and a missing
     * volume must be an error, not a wait. */
    ((struct Process *)FindTask(NULL))->pr_WindowPtr = (APTR)-1;
    rda = ReadArgs("APPLY/S,READY/S,VERBOSE/S", args, NULL);
    if (!rda || !args[0] == !args[1]) {
        if (rda)
            FreeArgs(rda);
        PutStr("usage: SystemUpdate APPLY|READY\n");
        return RETURN_ERROR;
    }
    verbose = args[2] != 0;
    FreeArgs(rda);
    if (args[1]) {
        char id[80];

        return exists(DEPOT) && find_snapshot(id, sizeof(id)) == 1 ? RETURN_OK : RETURN_WARN;
    }
    return apply_and_close();
}
