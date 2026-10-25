/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Disposable VM fixture. AFSPROBE: must be a writable, authorized AFS+
    image. This probe remaps SYS/ENV/ENVARC temporarily and never qualifies
    GUI import or live application of five preferences across three editors.
*/
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <filesystems/afsplus_ext_packet.h>
#include <filesystems/afsplus_aros.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "afsprobe_device.h"
#include "afsprobe_report.h"
#include "../../../workbench/prefs/storage/report.h"

static ULONG failures;
static BPTR command_segment;
static BOOL rebound[3];
static const char *assign_names[] = {"SYS", "ENV", "ENVARC"};

static void probe_report(const char *format, ...)
{
    char line[512];
    va_list args;
    int length;
    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof(line)) return;
    afsprobe_debug_record(line, (size_t)length);
    PutStr((CONST_STRPTR)line);
}

static void check(BOOL ok, const char *step)
{
    probe_report("[AFSSETTINGS] step=%s observed=%s\n", step,
        ok ? "accepted" : "FAIL");
    if (!ok) failures++;
}

static BOOL make_directory(CONST_STRPTR path)
{
    BPTR lock = Lock(path, SHARED_LOCK);
    if (!lock)
    {
        LONG lookup_code = IoErr();
        lock = CreateDir(path);
        if (!lock)
        {
            LONG code = IoErr();
            probe_report("[AFSSETTINGS] detail=mkdir path=%s lookup-code=%ld dos-code=%ld\n",
                path, (long)lookup_code, (long)code);
            SetIoErr(code); return FALSE;
        }
    }
    UnLock(lock);
    return TRUE;
}

static BOOL assign_directory(CONST_STRPTR name, CONST_STRPTR path)
{
    BPTR lock = Lock(path, SHARED_LOCK);
    struct DosList *list, *entry;
    LONG kind = -1, code;
    ULONG i;
    if (!lock)
    {
        code = IoErr();
        probe_report("[AFSSETTINGS] detail=assign-lock name=%s path=%s dos-code=%ld\n", name, path, (long)code);
        SetIoErr(code); return FALSE;
    }
    list = LockDosList(LDF_ALL | LDF_READ);
    if (list)
    {
        entry = FindDosEntry(list, name, LDF_ALL);
        if (entry) kind = entry->dol_Type;
        UnLockDosList(LDF_ALL | LDF_READ);
    }
    if (AssignLock(name, lock))
    {
        for (i = 0; i < 3; i++) if (!strcmp((const char *)name, assign_names[i])) rebound[i] = TRUE;
        probe_report("[AFSSETTINGS] detail=assign name=%s path=%s previous-kind=%ld dos-code=0\n", name, path, (long)kind);
        return TRUE;
    }
    code = IoErr();
    probe_report("[AFSSETTINGS] detail=assign name=%s path=%s previous-kind=%ld dos-code=%ld\n", name, path, (long)kind, (long)code);
    UnLock(lock);
    SetIoErr(code);
    return FALSE;
}

static BOOL image_file(CONST_STRPTR path, ULONG size, UBYTE seed)
{
    UBYTE data[4096];
    ULONG offset = 0, i, count;
    BPTR file = Open(path, MODE_NEWFILE);
    BOOL ok = file != BNULL;
    LONG first_code = 0;
    if (!file)
    {
        LONG code = IoErr();
        probe_report("[AFSSETTINGS] detail=open path=%s size=%lu dos-code=%ld\n", path, (unsigned long)size, (long)code);
        SetIoErr(code); return FALSE;
    }
    while (ok && offset < size)
    {
        LONG written;
        count = size - offset;
        if (count > sizeof(data)) count = sizeof(data);
        for (i = 0; i < count; i++) data[i] = (UBYTE)((offset + i + seed) % 251);
        written = Write(file, data, count);
        ok = written == (LONG)count;
        if (!ok)
        {
            first_code = IoErr();
            probe_report("[AFSSETTINGS] detail=write path=%s offset=%lu requested=%lu written=%ld dos-code=%ld\n",
                path, (unsigned long)offset, (unsigned long)count, (long)written, (long)first_code);
        }
        else offset += count;
    }
    if (!Flush(file))
    {
        LONG code = IoErr();
        probe_report("[AFSSETTINGS] detail=flush path=%s offset=%lu dos-code=%ld\n", path, (unsigned long)offset, (long)code);
        if (ok) first_code = code;
        ok = FALSE;
    }
    if (!Close(file))
    {
        LONG code = IoErr();
        probe_report("[AFSSETTINGS] detail=close path=%s offset=%lu dos-code=%ld\n", path, (unsigned long)offset, (long)code);
        if (ok) first_code = code;
        ok = FALSE;
    }
    if (ok) probe_report("[AFSSETTINGS] detail=file-complete path=%s bytes=%lu dos-code=0\n", path, (unsigned long)offset);
    SetIoErr(first_code);
    return ok;
}

static BOOL verify_file(CONST_STRPTR path, ULONG size, UBYTE seed)
{
    UBYTE data[4096];
    ULONG offset = 0, i, count;
    LONG read;
    BOOL ok = TRUE;
    BPTR file = Open(path, MODE_OLDFILE);
    if (!file) return FALSE;
    while (ok && offset < size)
    {
        count = size - offset;
        if (count > sizeof(data)) count = sizeof(data);
        read = Read(file, data, count);
        if (read != (LONG)count) { ok = FALSE; break; }
        for (i = 0; i < count; i++)
            if (data[i] != (UBYTE)((offset + i + seed) % 251)) { ok = FALSE; break; }
        offset += count;
    }
    if (Read(file, data, 1) != 0) ok = FALSE;
    if (!Close(file)) ok = FALSE;
    return ok;
}

static LONG settings_command(const char *operation)
{
    char args[128];
    BPTR sink = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE), original;
    LONG result;
    snprintf(args, sizeof(args), "%s QUIET\n", operation);
    if (!sink) return -1;
    original = SelectOutput(sink);
    result = RunCommand(command_segment, 65536, (STRPTR)args, strlen(args));
    SelectOutput(original);
    Close(sink);
    return result;
}

static BOOL capture_path(const char *expected)
{
    char path[1024];
    BPTR state = Open((CONST_STRPTR)"RAM:Settings-Boot-State", MODE_OLDFILE);
    LONG count;
    if (!state) return FALSE;
    count = Read(state, path, sizeof(path) - 1);
    Close(state);
    if (count < 0) return FALSE;
    path[count] = 0;
    return strcmp(path, expected) == 0;
}

static BOOL fixture(void)
{
    static const char *names[] = {"Sys/font", "Sys/screenmode", "Zune/global",
        "Network/net", "empty", "large", "collision", "collision.boot-new"};
    static const ULONG sizes[] = {137, 129, 513, 67, 0, 300000, 33, 51};
    char path[1024];
    ULONG i;
    if (!make_directory((CONST_STRPTR)"ENVARC:Sys") ||
        !make_directory((CONST_STRPTR)"ENVARC:Zune") ||
        !make_directory((CONST_STRPTR)"ENVARC:Network") ||
        !make_directory((CONST_STRPTR)"ENVARC:~#0")) return FALSE;
    for (i = 0; i < 8; i++)
    {
        snprintf(path, sizeof(path), "ENVARC:%s", names[i]);
        if (!image_file((CONST_STRPTR)path, sizes[i], i)) return FALSE;
    }
    return TRUE;
}

static BOOL restored(void)
{
    static const char *names[] = {"Sys/font", "Sys/screenmode", "Zune/global",
        "Network/net", "empty", "large", "collision", "collision.boot-new"};
    static const ULONG sizes[] = {137, 129, 513, 67, 0, 300000, 33, 51};
    char path[1024];
    ULONG i;
    for (i = 0; i < 8; i++)
    {
        snprintf(path, sizeof(path), "ENV:%s", names[i]);
        if (!verify_file((CONST_STRPTR)path, sizes[i], i)) return FALSE;
    }
    return TRUE;
}

static int32_t send(void *context, struct AfsplusExtRequest *request)
{
    if (DoPkt(context, ACTION_AFSPLUS_EXT, (SIPTR)request, 0, 0, 0, 0)) return 0;
    return IoErr() ? IoErr() : ERROR_UNKNOWN;
}

static void begin(struct AfsplusExtRequest *request, uint32_t operation)
{
    memset(request, 0, sizeof(*request));
    request->magic = AFSPLUS_EXT_MAGIC;
    request->version = AFSPLUS_EXT_VERSION;
    request->header_size = sizeof(*request);
    request->operation = operation;
}

static void snapshot(void *context, uint64_t id, uint64_t generation, uint64_t tx)
{
    uint64_t *wanted = context;
    (void)generation; (void)tx;
    if (id == wanted[0]) wanted[1] = 1;
}

int main(void)
{
    BPTR volume = BNULL, previous[3] = {BNULL, BNULL, BNULL};
    static const char *roots[] = {"SYS:", "ENV:", "ENVARC:"};
    struct AfsplusExtRequest request;
    struct MsgPort *port;
    struct DevProc *device;
    uint64_t wanted[2] = {0,0}, reader = 0;
    uint32_t total;
    int incomplete;
    ULONG i;
    char report[768];
    BOOL success;
    struct AfsplusArosCapabilities caps;
    command_segment = LoadSeg((CONST_STRPTR)"C:SettingsBoot");
    volume = Lock((CONST_STRPTR)"AFSPROBE:", SHARED_LOCK);
    if (!command_segment || !volume)
    { check(FALSE, "fixture-prerequisites"); goto done; }
    port = ((struct FileLock *)BADDR(volume))->fl_Task;
    device = GetDeviceProc((CONST_STRPTR)"AFSPROBE:", NULL);
    if (!afsprobe_bound_device(device, 91, (CONST_STRPTR)"AFSPROBE") ||
        device->dvp_Port != port)
    {
        if (device) FreeDeviceProc(device);
        check(FALSE, "fdsk-unit91-identity-required"); goto done;
    }
    FreeDeviceProc(device);
    check(storage_report(port, send, report, sizeof(report)), "storage-reports");
    begin(&request, AFSPLUS_EXT_CAPABILITIES);
    memset(&caps, 0, sizeof(caps)); caps.struct_size = sizeof(caps);
    request.buffer = &caps; request.buffer_size = sizeof(caps);
    if (send(port, &request) == 0)
        probe_report("[AFSSETTINGS] detail=capacity total-blocks=%llu free-blocks=%llu available-blocks=%llu block-size=%lu\n",
            (unsigned long long)caps.total_blocks, (unsigned long long)caps.free_blocks,
            (unsigned long long)caps.available_blocks, (unsigned long)caps.block_size);
    for (i = 0; i < 3; i++) previous[i] = Lock((CONST_STRPTR)roots[i], SHARED_LOCK);
    if (!previous[0] || !previous[1] || !previous[2])
    { check(FALSE, "original-assigns"); goto done; }
    success = make_directory((CONST_STRPTR)"AFSPROBE:SettingsFixture") &&
        make_directory((CONST_STRPTR)"AFSPROBE:SettingsFixture/Prefs") &&
        make_directory((CONST_STRPTR)"AFSPROBE:SettingsFixture/Archive") &&
        make_directory((CONST_STRPTR)"RAM:AFSSettings") &&
        make_directory((CONST_STRPTR)"RAM:AFSSettings/ENV") &&
        make_directory((CONST_STRPTR)"RAM:AFSSettings/Archive");
    if (!success) { check(FALSE, "fixture-directories"); goto done; }
    success = assign_directory((CONST_STRPTR)"SYS", (CONST_STRPTR)"AFSPROBE:SettingsFixture") &&
        assign_directory((CONST_STRPTR)"ENVARC", (CONST_STRPTR)"AFSPROBE:SettingsFixture/Archive") &&
        assign_directory((CONST_STRPTR)"ENV", (CONST_STRPTR)"RAM:AFSSettings/ENV") && fixture();
    if (!success) { check(FALSE, "afs-fixture"); goto done; }
    DeleteFile((CONST_STRPTR)"RAM:Settings-Boot-State");
    check(settings_command("RESTORE") != RETURN_OK, "absent-capture-refused");
    {
        BPTR busy = CreateDir((CONST_STRPTR)"RAM:Settings-Boot-Busy");
        check(busy && settings_command("CAPTURE") != RETURN_OK, "concurrent-capture-refused");
        if (busy) UnLock(busy);
        DeleteFile((CONST_STRPTR)"RAM:Settings-Boot-Busy");
    }
    check(settings_command("CAPTURE") == RETURN_OK && capture_path("SYS:Prefs/Settings-At-Boot"), "clone-capture");
    check(settings_command("CAPTURE") != RETURN_OK, "repeat-capture-refused");
    check(image_file((CONST_STRPTR)"ENV:~#1", 117, 77), "stage-neighbor-fixture");
    check(settings_command("RESTORE") == RETURN_OK && restored() &&
        verify_file((CONST_STRPTR)"ENV:~#1", 117, 77), "clone-restore-eight-files");
    check(image_file((CONST_STRPTR)"ENV:Sys/font", 1, 91) &&
        settings_command("RESTORE") == RETURN_OK && restored(), "atomic-existing-target");
    begin(&request, AFSPLUS_EXT_SNAPSHOT_CREATE);
    if (send(port, &request)) { check(FALSE, "snapshot-create"); goto done; }
    wanted[0] = request.output_value;
    check(storage_snapshots(port, send, snapshot, wanted, &total, &incomplete) == 0 &&
        wanted[1] && !incomplete, "storage-snapshot-list");
    begin(&request, AFSPLUS_EXT_SNAPSHOT_OPEN);
    request.object[0] = wanted[0];
    if (send(port, &request)) { check(FALSE, "snapshot-open"); goto done; }
    reader = request.output_value;
    check(storage_snapshot_delete(port, send, wanted[0]) == ERROR_OBJECT_IN_USE, "open-reader-delete-refused");
    begin(&request, AFSPLUS_EXT_SNAPSHOT_CLOSE);
    request.offset[0] = reader;
    check(send(port, &request) == 0, "snapshot-close");
    reader = 0;
    check(storage_snapshot_delete(port, send, wanted[0]) == 0, "storage-snapshot-delete");
    wanted[0] = 0;
    DeleteFile((CONST_STRPTR)"RAM:Settings-Boot-State");
    success = assign_directory((CONST_STRPTR)"ENVARC", (CONST_STRPTR)"RAM:AFSSettings/Archive") && fixture();
    check(success && settings_command("CAPTURE") == RETURN_OK && capture_path("RAM:Settings-At-Boot"), "copy-capture");
    check(image_file((CONST_STRPTR)"ENV:large", 3, 84) &&
        settings_command("RESTORE") == RETURN_OK && restored(), "copy-restore-eight-files");
done:
    DeleteFile((CONST_STRPTR)"RAM:Settings-Boot-State");
    if (volume && reader)
    {
        begin(&request, AFSPLUS_EXT_SNAPSHOT_CLOSE); request.offset[0] = reader;
        send(((struct FileLock *)BADDR(volume))->fl_Task, &request);
    }
    if (volume && wanted[0])
        storage_snapshot_delete(((struct FileLock *)BADDR(volume))->fl_Task, send, wanted[0]);
    for (i = 0; i < 3; i++)
        if (previous[i])
        {
            if (!rebound[i]) UnLock(previous[i]);
            else if (!AssignLock((CONST_STRPTR)assign_names[i], previous[i]))
            {
                LONG code = IoErr();
                probe_report("[AFSSETTINGS] detail=restore-assign name=%s dos-code=%ld\n", assign_names[i], (long)code);
                UnLock(previous[i]); check(FALSE, "restore-original-assign");
            }
        }
    if (command_segment) UnLoadSeg(command_segment);
    if (volume) UnLock(volume);
    probe_report("[AFSSETTINGS] result=%s fixture-files=8 large-bytes=300000 staging-alias=1 clone-and-copy=1 gui-live=UNVERIFIED failures=%lu\n",
        failures ? "FAIL" : "PASS", (unsigned long)failures);
    return failures ? RETURN_ERROR : RETURN_OK;
}
