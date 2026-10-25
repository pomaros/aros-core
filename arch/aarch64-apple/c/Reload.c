/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: Reload NAME FILE [DEVICE] [DRYRUN] - replace a dynamically loaded
          library, HIDD or device on the running system, without a reboot.

    The contract of the component reload design (aros-apple-silicon
    docs/features/hot-reload): the candidate is identified before anything
    stops (its SHA-256 and $VER string); a module somebody holds open is
    refused, never forced; the old module is removed through its own expunge
    and must leave the system list, or the reload stops there with the old
    code in place; then the candidate is loaded from FILE and must answer
    under NAME. Rollback is the same command with the retained old file.

    A module that keeps itself open (a service process running its code, a
    driver that owns hardware for the rest of the boot) refuses here by
    design: its open count never reaches zero.

    One verdict line:

      [reload] result=PASS name=applemca.audio kind=library old=6.2 new=6.3
               sha256=<64 hex> ver="applemca.audio 6.3 (17.10.2026)"

    ver is the candidate's $VER string or, for a module without one, the
    IdString of the module now loaded.

    Exit 0 on PASS, 10 when refused (busy, kept by its own expunge, wrong
    candidate), 20 on a failure after the old module left.
*/

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/devices.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "../bootstrap/stage2_sha256.h"

#define TEMPLATE    "NAME/A,FILE/A,DEVICE/S,DRYRUN/S"
#define MAX_FILE    (32UL * 1024 * 1024)

static const char *kind_name(BOOL device)
{
    return device ? "device" : "library";
}

/* The module of that name in the system list, with its counters read under
 * Forbid: the base may go away as soon as it is released. */
static struct Library *find_module(const char *name, BOOL device, UWORD *version,
                                   UWORD *revision, UWORD *opens)
{
    struct Library *lib;

    Forbid();
    lib = (struct Library *)FindName(device ? &SysBase->DeviceList : &SysBase->LibList, name);
    if (lib) {
        *version = lib->lib_Version;
        *revision = lib->lib_Revision;
        *opens = lib->lib_OpenCnt;
    }
    Permit();
    return lib;
}

/* The candidate's identity: its SHA-256 and the text of its $VER string. */
static BOOL identify(const char *path, char hex[65], char *ver, size_t versize, ULONG *bytes)
{
    BPTR fh = Open(path, MODE_OLDFILE);
    struct FileInfoBlock *fib;
    UBYTE *data = NULL, digest[AROS_APPLE_STAGE2_SHA256_SIZE];
    LONG size = -1;
    ULONG i;

    ver[0] = 0;
    if (!fh)
        return FALSE;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib && ExamineFH(fh, fib))
        size = fib->fib_Size;
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    if (size > 0 && (ULONG)size <= MAX_FILE)
        data = AllocVec(size, MEMF_ANY);
    if (!data || Read(fh, data, size) != size) {
        Close(fh);
        if (data)
            FreeVec(data);
        return FALSE;
    }
    Close(fh);
    AROSAppleStage2SHA256(data, (size_t)size, digest);
    for (i = 0; i < AROS_APPLE_STAGE2_SHA256_SIZE; ++i)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    for (i = 0; i + 5 < (ULONG)size; ++i)
        if (memcmp(data + i, "$VER:", 5) == 0) {
            ULONG j = 0;

            i += 5;
            while (i < (ULONG)size && data[i] == ' ')
                ++i;
            while (i + j < (ULONG)size && data[i + j] >= 0x20 && data[i + j] < 0x7f
                   && j + 1 < versize) {
                ver[j] = data[i + j];
                ++j;
            }
            ver[j] = 0;
            break;
        }
    FreeVec(data);
    *bytes = (ULONG)size;
    return TRUE;
}

/* Load the candidate through the ordinary loader and release it again: it
 * stays in the system list, as a module any program opened and closed. */
static BOOL load_candidate(const char *path, BOOL device, struct Library **base)
{
    *base = NULL;
    if (!device) {
        *base = OpenLibrary(path, 0);
        if (*base) {
            CloseLibrary(*base);
            return TRUE;
        }
        return FALSE;
    } else {
        struct IORequest *io = AllocMem(sizeof(struct IOStdReq), MEMF_PUBLIC | MEMF_CLEAR);
        BOOL ok;

        if (!io)
            return FALSE;
        io->io_Message.mn_Length = sizeof(struct IOStdReq);
        /* Unit 0 may not exist for every device; loading is what counts. */
        ok = OpenDevice(path, 0, io, 0) == 0;
        if (ok) {
            *base = (struct Library *)io->io_Device;
            CloseDevice(io);
        }
        FreeMem(io, sizeof(struct IOStdReq));
        return ok;
    }
}

int main(void)
{
    IPTR args[4] = { 0, 0, 0, 0 };
    struct RDArgs *rda;
    const char *name, *file;
    BOOL device, dryrun;
    struct Library *old, *now;
    UWORD oldver = 0, oldrev = 0, opens = 0, newver = 0, newrev = 0, newopens = 0;
    struct Library *loaded;
    char hex[65] = "", ver[96];
    ULONG bytes = 0;
    char oldtext[16] = "-";

    rda = ReadArgs(TEMPLATE, args, NULL);
    if (!rda) {
        PrintFault(IoErr(), "Reload");
        return RETURN_ERROR;
    }
    name = (const char *)args[0];
    file = (const char *)args[1];
    device = args[2] != 0;
    dryrun = args[3] != 0;

    if (!identify(file, hex, ver, sizeof(ver), &bytes)) {
        printf("[reload] result=REFUSED name=%s reason=candidate-unreadable file=%s\n", name, file);
        FreeArgs(rda);
        return RETURN_ERROR;
    }

    old = find_module(name, device, &oldver, &oldrev, &opens);
    if (old)
        snprintf(oldtext, sizeof(oldtext), "%u.%u", oldver, oldrev);
    if (old && opens) {
        printf("[reload] result=REFUSED name=%s kind=%s reason=busy openers=%u old=%s"
               " sha256=%s: close its users first; a module that keeps itself open"
               " is reloaded only by a reboot\n", name, kind_name(device), opens, oldtext, hex);
        FreeArgs(rda);
        return RETURN_ERROR;
    }
    if (dryrun) {
        printf("[reload] result=DRYRUN name=%s kind=%s old=%s loaded=%s sha256=%s bytes=%lu ver=\"%s\"\n",
               name, kind_name(device), oldtext, old ? "yes" : "no", hex, (unsigned long)bytes, ver);
        FreeArgs(rda);
        return RETURN_OK;
    }

    if (old) {
        /* Its own expunge decides; nothing is freed behind its back. */
        Forbid();
        if (device)
            RemDevice((struct Device *)old);
        else
            RemLibrary(old);
        Permit();
        if (find_module(name, device, &oldver, &oldrev, &opens)) {
            printf("[reload] result=REFUSED name=%s kind=%s reason=kept-by-expunge old=%s:"
                   " the module refused to leave (noexpunge, or work still running); old code"
                   " stays in place\n", name, kind_name(device), oldtext);
            FreeArgs(rda);
            return RETURN_ERROR;
        }
    }

    if (!load_candidate(file, device, &now)) {
        printf("[reload] result=FAIL name=%s kind=%s reason=candidate-did-not-load old=%s file=%s"
               " ioerr=%ld: the old module is gone; load the retained one with the same command\n",
               name, kind_name(device), oldtext, file, (long)IoErr());
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    if (!(loaded = find_module(name, device, &newver, &newrev, &newopens))) {
        printf("[reload] result=FAIL name=%s kind=%s reason=candidate-has-another-name file=%s\n",
               name, kind_name(device), file);
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    /* A module built without a $VER string (every AHI driver: their
     * identity is the library's IdString) is named by what it reports. */
    if (!ver[0]) {
        Forbid();
        if (loaded->lib_IdString) {
            const char *id = (const char *)loaded->lib_IdString;
            size_t j = 0;

            while (id[j] >= 0x20 && id[j] < 0x7f && j + 1 < sizeof(ver)) {
                ver[j] = id[j];
                ++j;
            }
            ver[j] = 0;
        }
        Permit();
    }
    /* The bases tell a replacement from a module that never left. */
    printf("[reload] result=PASS name=%s kind=%s old=%s new=%u.%u old_base=%p new_base=%p sha256=%s"
           " bytes=%lu ver=\"%s\"\n", name, kind_name(device), oldtext, newver, newrev,
           (void *)old, (void *)loaded, hex, (unsigned long)bytes, ver);
    FreeArgs(rda);
    return RETURN_OK;
}
