/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Preference contents captured before writers start and reapplied to ENV:.
    AFS+ clones preserve the source metadata in the retained capture. The
    copy route and ENV restoration implement the preferences contents contract;
    arbitrary filesystem backup uses its separate metadata/security contract.
*/
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <filesystems/afsplus_ext_packet.h>
#include <filesystems/afsplus_aros.h>
#include <filesystems/filesystem_v2.h>
#include <stdio.h>
#include <string.h>

#define STATE "RAM:Settings-Boot-State"
#define BUSY "RAM:Settings-Boot-Busy"
#define DISK_COPY "SYS:Prefs/Settings-At-Boot"
#define RAM_COPY "RAM:Settings-At-Boot"
#define PATH_SIZE 1024

static ULONG copied;
static LONG failure;
static char staging_directory[PATH_SIZE];

static BOOL path_join(char *output, CONST_STRPTR directory, CONST_STRPTR name)
{
    int n = snprintf(output, PATH_SIZE, "%s%s%s", directory,
        directory[strlen(directory) - 1] == ':' ? "" : "/", name);
    if (n < 0 || n >= PATH_SIZE) { failure = ERROR_LINE_TOO_LONG; return FALSE; }
    return TRUE;
}

/* Reserve a directory in the destination filesystem, with a component absent
   from both the captured inventory and the live destination. Digits and
   punctuation have the same comparison identity under the source and target
   name policies. Each copy uses its private child, so a preference named
   "name.boot-new" cannot be overwritten as another preference's staging file. */
static BPTR prepare_stage(CONST_STRPTR source, CONST_STRPTR target)
{
    char source_path[PATH_SIZE], component[32];
    BPTR lock;
    ULONG candidate;
    LONG error;
    lock = Lock(target, SHARED_LOCK);
    if (!lock) lock = CreateDir(target);
    if (!lock) { failure = IoErr(); return BNULL; }
    UnLock(lock);
    for (candidate = 0; candidate < 4096; candidate++)
    {
        snprintf(component, sizeof(component), "~#%lu", (unsigned long)candidate);
        if (!path_join(source_path, source, (CONST_STRPTR)component) ||
            !path_join(staging_directory, target, (CONST_STRPTR)component)) return BNULL;
        lock = Lock((CONST_STRPTR)source_path, SHARED_LOCK);
        if (lock) { UnLock(lock); continue; }
        error = IoErr();
        if (error != ERROR_OBJECT_NOT_FOUND && error != ERROR_DIR_NOT_FOUND)
        { failure = error ? error : ERROR_UNKNOWN; return BNULL; }
        lock = Lock((CONST_STRPTR)staging_directory, SHARED_LOCK);
        if (lock) { UnLock(lock); continue; }
        error = IoErr();
        if (error != ERROR_OBJECT_NOT_FOUND && error != ERROR_DIR_NOT_FOUND)
        { failure = error ? error : ERROR_UNKNOWN; return BNULL; }
        lock = CreateDir((CONST_STRPTR)staging_directory);
        if (lock) return lock;
        error = IoErr();
        if (error != ERROR_OBJECT_EXISTS)
        { failure = error ? error : ERROR_UNKNOWN; return BNULL; }
    }
    failure = ERROR_OBJECT_EXISTS;
    return BNULL;
}

static BOOL release_stage(BPTR stage)
{
    UnLock(stage);
    if (DeleteFile((CONST_STRPTR)staging_directory)) return TRUE;
    if (!failure) failure = IoErr() ? IoErr() : ERROR_UNKNOWN;
    return FALSE;
}

static BOOL copy_file(CONST_STRPTR source, CONST_STRPTR target, BOOL replace)
{
    char temporary[PATH_SIZE];
    BPTR in = BNULL, out = BNULL;
    UBYTE *buffer = NULL;
    LONG count;
    BOOL ok = FALSE;
    if (!path_join(temporary, (CONST_STRPTR)staging_directory, (CONST_STRPTR)"0")) return FALSE;
    in = Open(source, MODE_OLDFILE);
    if (in == BNULL) goto done;
    out = Open(temporary, MODE_NEWFILE);
    if (out == BNULL) goto done;
    buffer = AllocVec(16384, MEMF_ANY);
    if (!buffer) { SetIoErr(ERROR_NO_FREE_STORE); goto done; }
    while ((count = Read(in, buffer, 16384)) > 0)
        if (Write(out, buffer, count) != count) goto done;
    if (count < 0 || !Flush(out)) goto done;
    if (!Close(out)) { out = BNULL; goto done; }
    out = BNULL;
    if (!Close(in)) { in = BNULL; goto done; }
    in = BNULL;
    ok = replace ? ReplaceObject(temporary, target) : Rename(temporary, target);
done:
    if (!ok) failure = IoErr() ? IoErr() : ERROR_UNKNOWN;
    if (out != BNULL) Close(out);
    if (in != BNULL) Close(in);
    FreeVec(buffer);
    if (!ok) DeleteFile(temporary);
    return ok;
}

static BOOL clone_file(BPTR source_dir, CONST_STRPTR source, BPTR target_dir,
    CONST_STRPTR name)
{
    struct AfsplusExtRequest request;
    struct MsgPort *port = ((struct FileLock *)BADDR(source_dir))->fl_Task;
    BPTR file = Lock(source, SHARED_LOCK);
    BOOL ok;
    if (file == BNULL) { failure = IoErr(); return FALSE; }
    memset(&request, 0, sizeof(request));
    request.magic = AFSPLUS_EXT_MAGIC;
    request.version = AFSPLUS_EXT_VERSION;
    request.header_size = sizeof(request);
    request.operation = AFSPLUS_EXT_CLONE_FILE;
    request.object[0] = (uint64_t)(uintptr_t)file;
    request.object[1] = (uint64_t)(uintptr_t)target_dir;
    request.name1 = (const uint8_t *)name;
    request.name_length[1] = strlen(name);
    ok = DoPkt(port, ACTION_AFSPLUS_EXT, (SIPTR)&request, 0, 0, 0, 0) != 0;
    if (!ok) failure = IoErr() ? IoErr() : ERROR_UNKNOWN;
    UnLock(file);
    return ok;
}

/* A captured tree is published only after every file and directory succeeds.
   Locks and enumeration objects have one exit path, including recursive errors. */
static BOOL walk(CONST_STRPTR source, CONST_STRPTR target, BOOL clone,
    BOOL restore, ULONG depth)
{
    BPTR source_dir = BNULL, target_dir = BNULL;
    struct FileInfoBlock *fib = NULL;
    BOOL ok = FALSE;
    LONG error;
    char *paths = NULL;
    if (depth > 64) { failure = ERROR_TOO_MANY_LEVELS; return FALSE; }
    source_dir = Lock(source, SHARED_LOCK);
    if (source_dir == BNULL) goto done;
    target_dir = Lock(target, SHARED_LOCK);
    if (target_dir == BNULL) target_dir = CreateDir(target);
    if (target_dir == BNULL) goto done;
    paths = AllocVec(PATH_SIZE * 2, MEMF_ANY);
    if (!paths) { SetIoErr(ERROR_NO_FREE_STORE); goto done; }
    fib = AllocDosObject(DOS_FIB, NULL);
    if (!fib) { SetIoErr(ERROR_NO_FREE_STORE); goto done; }
    if (!Examine(source_dir, fib)) goto done;
    while (ExNext(source_dir, fib))
    {
        char *from = paths, *to = paths + PATH_SIZE;
        if (!path_join(from, source, fib->fib_FileName) ||
            !path_join(to, target, fib->fib_FileName)) goto done;
        if (fib->fib_DirEntryType == ST_USERDIR)
        {
            if (!walk(from, to, clone, restore, depth + 1)) goto done;
        }
        else if (fib->fib_DirEntryType == ST_FILE)
        {
            if (!(clone ? clone_file(source_dir, from, target_dir, fib->fib_FileName) :
                    copy_file(from, to, restore))) goto done;
            copied++;
        }
        else { failure = ERROR_OBJECT_WRONG_TYPE; goto done; }
    }
    error = IoErr();
    if (error != ERROR_NO_MORE_ENTRIES) { failure = error ? error : ERROR_UNKNOWN; goto done; }
    ok = TRUE;
done:
    if (!ok && !failure) failure = IoErr() ? IoErr() : ERROR_UNKNOWN;
    FreeVec(paths);
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (target_dir != BNULL) UnLock(target_dir);
    if (source_dir != BNULL) UnLock(source_dir);
    return ok;
}

static BOOL remove_tree(CONST_STRPTR path, ULONG depth)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    BOOL ok = TRUE;
    LONG error;
    if (depth > 64) { failure = ERROR_TOO_MANY_LEVELS; return FALSE; }
    lock = Lock(path, SHARED_LOCK);
    if (!lock) return IoErr() == ERROR_OBJECT_NOT_FOUND || IoErr() == ERROR_DIR_NOT_FOUND;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); failure = ERROR_NO_FREE_STORE; return FALSE; }
    if (!Examine(lock, fib)) ok = FALSE;
    else while (ok && ExNext(lock, fib))
    {
        char child[PATH_SIZE];
        if (!path_join(child, path, fib->fib_FileName)) { ok = FALSE; break; }
        ok = fib->fib_DirEntryType == ST_USERDIR ? remove_tree(child, depth + 1) : DeleteFile(child);
    }
    error = IoErr();
    if (ok && error != ERROR_NO_MORE_ENTRIES) ok = FALSE;
    if (!ok && !failure) failure = error ? error : ERROR_UNKNOWN;
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    if (ok) ok = DeleteFile(path);
    if (!ok && !failure) failure = IoErr() ? IoErr() : ERROR_UNKNOWN;
    return ok;
}

static BOOL clone_available(void)
{
    BPTR source = Lock("ENVARC:", SHARED_LOCK), target = Lock("SYS:Prefs", SHARED_LOCK);
    struct AfsplusExtRequest request;
    struct AfsplusArosCapabilities capabilities;
    BOOL available = FALSE;
    if (source && target && ((struct FileLock *)BADDR(source))->fl_Task ==
        ((struct FileLock *)BADDR(target))->fl_Task)
    {
        /* Query the mounted volume, rather than assuming a library group
           guarantees the capability on every formatted volume. */
        memset(&request, 0, sizeof(request));
        request.magic = AFSPLUS_EXT_MAGIC;
        request.version = AFSPLUS_EXT_VERSION;
        request.header_size = sizeof(request);
        memset(&capabilities, 0, sizeof(capabilities));
        capabilities.struct_size = sizeof(capabilities);
        request.operation = AFSPLUS_EXT_CAPABILITIES;
        request.buffer = &capabilities;
        request.buffer_size = sizeof(capabilities);
        available = DoPkt(((struct FileLock *)BADDR(source))->fl_Task,
            ACTION_AFSPLUS_EXT, (SIPTR)&request, 0, 0, 0, 0) &&
            capabilities.struct_size == sizeof(capabilities) &&
            capabilities.mount_mode == AFSPLUS_AROS_MOUNT_READ_WRITE &&
            (capabilities.capabilities & FSV2_CAP_CLONE_FILE) != 0;
    }
    if (target) UnLock(target);
    if (source) UnLock(source);
    return available;
}

int main(void)
{
    IPTR args[3] = {0};
    struct RDArgs *rdargs;
    const struct Library *dos = (const struct Library *)__aros_getbase_DOSBase();
    BOOL ok = FALSE, clone = FALSE;
    CONST_STRPTR root = NULL;
    char path[PATH_SIZE];
    BPTR state, operation_lock = BNULL, stage = BNULL;
    copied = 0;
    failure = 0;
    /* The loader opens DOS by major version. Check the revision before any
       filesystem operation can reach ReplaceObject's vector 227. */
    if (dos == NULL)
        return RETURN_FAIL;
    if (dos->lib_Version < 50 ||
        (dos->lib_Version == 50 && dos->lib_Revision < 84))
    {
        Printf("SettingsBoot requires dos.library 50.84 or newer.\n");
        SetIoErr(ERROR_INVALID_RESIDENT_LIBRARY);
        return RETURN_FAIL;
    }
    rdargs = ReadArgs("CAPTURE/S,RESTORE/S,QUIET/S", args, NULL);
    if (!rdargs || (args[0] != 0) == (args[1] != 0))
    {
        if (rdargs) FreeArgs(rdargs);
        Printf("SettingsBoot CAPTURE | RESTORE [QUIET]\n");
        return RETURN_FAIL;
    }
    /* Atomic directory creation serializes capture and restore across CLI
       processes. A crashed command leaves a conservative busy refusal until
       RAM is reset at the next boot. */
    operation_lock = CreateDir(BUSY);
    if (operation_lock == BNULL)
    {
        failure = IoErr() ? IoErr() : ERROR_OBJECT_EXISTS;
        goto finish;
    }
    if (args[0])
    {
        state = Lock(STATE, SHARED_LOCK);
        if (state != BNULL)
        {
            UnLock(state);
            failure = ERROR_OBJECT_EXISTS;
            goto finish;
        }
        DeleteFile(STATE);
        clone = clone_available();
        root = clone ? DISK_COPY : RAM_COPY;
        if (remove_tree(root, 0))
        {
            if (!clone) stage = prepare_stage((CONST_STRPTR)"ENVARC:", root);
            if (clone || stage) ok = walk("ENVARC:", root, clone, FALSE, 0);
            if (stage)
            {
                if (!release_stage(stage)) ok = FALSE;
                stage = BNULL;
            }
        }
        if (ok)
        {
            state = Open(STATE, MODE_NEWFILE);
            if (state)
            {
                ok = Write(state, root, strlen(root)) == (LONG)strlen(root);
                if (!ok) failure = IoErr();
                if (!Close(state)) { ok = FALSE; failure = IoErr(); }
            }
            else { failure = IoErr() ? IoErr() : ERROR_UNKNOWN; ok = FALSE; }
        }
        if (!ok) DeleteFile(STATE);
    }
    else
    {
        state = Open(STATE, MODE_OLDFILE);
        if (state)
        {
            LONG n = Read(state, path, sizeof(path) - 1);
            if (n > 0) { path[n] = 0; root = path; }
            if (!Close(state)) root = NULL;
        }
        if (root && (!strcmp(root, DISK_COPY) || !strcmp(root, RAM_COPY)))
        {
            stage = prepare_stage(root, (CONST_STRPTR)"ENV:");
            if (stage) ok = walk(root, "ENV:", FALSE, TRUE, 0);
            if (stage)
            {
                if (!release_stage(stage)) ok = FALSE;
                stage = BNULL;
            }
        }
        else failure = ERROR_OBJECT_NOT_FOUND;
        if (ok)
        {
            snprintf(path, sizeof(path), "%lu", (unsigned long)copied);
            SetVar("SettingsBootRestored", path, -1, GVF_GLOBAL_ONLY);
        }
    }
finish:
    if (operation_lock != BNULL)
    {
        UnLock(operation_lock);
        if (!DeleteFile(BUSY))
        {
            ok = FALSE;
            if (!failure) failure = IoErr() ? IoErr() : ERROR_UNKNOWN;
        }
    }
    if (args[1])
    {
        snprintf(path, sizeof(path), "%ld", (long)failure);
        SetVar("SettingsBootError", path, -1, GVF_GLOBAL_ONLY);
    }
    if (!args[2] || !ok)
        Printf("SettingsBoot %s: %s files=%lu mechanism=%s error=%ld\n",
            args[0] ? "capture" : "restore", ok ? "complete" : "failed",
            copied, clone ? "clone" : "copy", failure);
    FreeArgs(rdargs);
    SetIoErr(failure);
    return ok ? RETURN_OK : RETURN_ERROR;
}
