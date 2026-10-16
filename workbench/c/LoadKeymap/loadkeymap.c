/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: LoadKeymap -- load a keyboard layout by name and install it as the
          default keymap (SetKeyMapDefault). The heavy lifting lives in
          kms.library's OpenKeymap(), which loads a compiled keymap where the
          platform supports it and otherwise builds the keymap from the
          DEVS:Keymaps/<name>.akmd text descriptor at runtime. Re-run anytime
          to swap the layout; new shells/windows pick it up immediately.

          Usage:  LoadKeymap            show the current keymap + available layouts
                  LoadKeymap <name>     load keymap <name> and install it
                  LoadKeymap RESTORE    install the one saved in ENVARC:Keymap

          A name is a plain file name in DEVS:Keymaps: no volume, no
          directory, no quote. The result is 0 when the keymap is in use and,
          without NOPERSIST, saved; 5 when it is in use but could not be
          saved; 20 when it is not in use.
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <devices/keymap.h>
#include <libraries/kms.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/keymap.h>
#include <proto/kms.h>

#include <string.h>

const TEXT version[] = "$VER: LoadKeymap 1.1 (12.07.2026)\n";

#define ARG_TEMPLATE "NAME,NOPERSIST/S,RESTORE/S"
enum { ARG_NAME = 0, ARG_NOPERSIST, ARG_RESTORE, NOOFARGS };

#define KEYMAPS_DIR "DEVS:Keymaps"
#define CURRENT_ENV "ENV:Keymap"       /* session record, shown by the no-arg display */
#define PERSIST_ENV "ENVARC:Keymap"    /* survives reboot; `LoadKeymap RESTORE` reloads it */

struct KMSLibrary *KMSBase;

/* Whether an assign or volume of that name exists: opening a file on a
   missing one puts up an "insert volume" requester. */
static BOOL mounted(CONST_STRPTR name)
{
    struct DosList *dl = LockDosList(LDF_VOLUMES | LDF_ASSIGNS | LDF_READ);
    BOOL found = FindDosEntry(dl, name, LDF_VOLUMES | LDF_ASSIGNS) != NULL;
    UnLockDosList(LDF_VOLUMES | LDF_ASSIGNS | LDF_READ);
    return found;
}

/* Nonzero when name can only mean a file in DEVS:Keymaps. */
static BOOL nameOK(CONST_STRPTR name)
{
    LONG i, n = strlen((const char *)name);
    if (n == 0 || n > 60 || name[0] == '.')
        return FALSE;
    for (i = 0; i < n; i++)
    {
        UBYTE c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
              || c == '_' || c == '-' || c == '.'))
            return FALSE;
    }
    return TRUE;
}

/* The whole name written and closed, or FALSE. */
static BOOL record(CONST_STRPTR path, STRPTR name)
{
    LONG n = strlen((const char *)name);
    char volume[16];
    LONG v = 0;
    BPTR f;
    BOOL ok;
    while (path[v] && path[v] != ':' && v < (LONG)sizeof(volume) - 1) { volume[v] = path[v]; v++; }
    volume[v] = '\0';
    if (!mounted((CONST_STRPTR)volume)) return FALSE;
    f = Open(path, MODE_NEWFILE);
    if (!f) return FALSE;
    ok = Write(f, name, n) == n;
    return Close(f) && ok;
}

static ULONG doLoad(STRPTR name, BOOL persist)
{
    struct KeyMapNode *kmn;

    if (!nameOK(name))
    {
        Printf("LoadKeymap: '%s' is not a keymap name (keymap unchanged)\n", name);
        return RETURN_FAIL;
    }
    kmn = OpenKeymap(name);
    if (!kmn)
    {
        Printf("LoadKeymap: could not load keymap '%s' (keymap unchanged)\n", name);
        return RETURN_FAIL;
    }

    /* guard: never install a degenerate map (it would brick typing) */
    if (!kmn->kn_KeyMap.km_LoKeyMap || !kmn->kn_KeyMap.km_LoKeyMapTypes)
    {
        Printf("LoadKeymap: keymap '%s' looks invalid (keymap unchanged)\n", name);
        return RETURN_FAIL;
    }

    SetKeyMapDefault(&kmn->kn_KeyMap);
    record((CONST_STRPTR)CURRENT_ENV, name);
    if (persist && !record((CONST_STRPTR)PERSIST_ENV, name))
    {
        Printf("LoadKeymap: installed '%s' live, but it could not be saved in %s\n",
               (STRPTR)name, (STRPTR)PERSIST_ENV);
        return RETURN_WARN;
    }
    Printf("LoadKeymap: installed '%s' live%s\n", (STRPTR)name,
           persist ? (STRPTR)" (saved as the default for next boot)"
                   : (STRPTR)" (this session only)");
    return RETURN_OK;
}

static ULONG doRestore(void)
{
    BPTR f = mounted((CONST_STRPTR)"ENVARC") ? Open((CONST_STRPTR)PERSIST_ENV, MODE_OLDFILE) : BNULL;
    char buf[64], *q;
    LONG n;
    if (!f) return RETURN_OK;                       /* nothing saved; keep built-in default */
    n = Read(f, buf, sizeof(buf) - 1);
    Close(f);
    if (n <= 0) return RETURN_OK;
    buf[n] = '\0';
    for (q = buf; *q && *q != '\n' && *q != '\r'; q++) ;
    *q = '\0';
    if (!buf[0]) return RETURN_OK;
    return doLoad((STRPTR)buf, FALSE);              /* already saved; do not rewrite */
}

static void printCurrent(void)
{
    BPTR f = mounted((CONST_STRPTR)"ENV") ? Open((CONST_STRPTR)CURRENT_ENV, MODE_OLDFILE) : BNULL;
    if (f)
    {
        char buf[64]; LONG n = Read(f, buf, sizeof(buf) - 1);
        Close(f);
        if (n > 0)
        {
            char *q; buf[n] = '\0';
            for (q = buf; *q && *q != '\n' && *q != '\r'; q++) ;
            *q = '\0';
            if (buf[0]) { Printf("Current keymap: %s\n", (STRPTR)buf); return; }
        }
    }
    Printf("Current keymap: built-in default (no keymap loaded yet)\n");
}

static void printAvailable(void)
{
    BPTR lock = Lock((CONST_STRPTR)KEYMAPS_DIR, SHARED_LOCK);
    struct FileInfoBlock *fib;

    if (!lock) { Printf("Available layouts: (cannot open %s)\n", (STRPTR)KEYMAPS_DIR); return; }

    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib && Examine(lock, fib))
    {
        Printf("Available layouts in %s:\n", (STRPTR)KEYMAPS_DIR);
        while (ExNext(lock, fib))
        {
            /* A compiled keymap is listed by its name, a text descriptor
               without its .akmd; icons and directories are not layouts. */
            STRPTR nm = (STRPTR)fib->fib_FileName;
            LONG l = strlen((const char *)nm);
            if (fib->fib_DirEntryType > 0 || (l > 5 && strcmp((const char *)nm + l - 5, ".info") == 0))
                continue;
            if (l > 5 && strcmp((const char *)nm + l - 5, ".akmd") == 0)
                nm[l - 5] = '\0';
            Printf("  %s\n", nm);
        }
    }
    if (fib) FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
}

int main(void)
{
    IPTR args[NOOFARGS] = { 0 };
    struct RDArgs *rda;
    ULONG rc = RETURN_FAIL;

    KMSBase = (struct KMSLibrary *)OpenLibrary((CONST_STRPTR)"kms.library", 0);
    if (!KMSBase) { Printf("LoadKeymap: cannot open kms.library\n"); return RETURN_FAIL; }

    rda = ReadArgs((CONST_STRPTR)ARG_TEMPLATE, args, NULL);
    if (rda)
    {
        if (args[ARG_RESTORE])
            rc = doRestore();
        else if (args[ARG_NAME])
            rc = doLoad((STRPTR)args[ARG_NAME], !args[ARG_NOPERSIST]);
        else
        {
            printCurrent();
            printAvailable();
            rc = RETURN_OK;
        }
        FreeArgs(rda);
    }
    else
        PrintFault(IoErr(), (CONST_STRPTR)"LoadKeymap");

    CloseLibrary((struct Library *)KMSBase);
    return rc;
}
