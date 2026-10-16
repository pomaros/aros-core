/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    KeymapWatch -- apply a host-requested keymap change live.

    The chosen keymap is kept in ENVARC:Keymap, wherever ENVARC: is assigned
    (SYS:Prefs/Env-Archive on the hosted port, a writable volume on a
    machine whose SYS: is read-only). The boot applies it
    once via `LoadKeymap RESTORE`. This tiny background task polls that file and,
    whenever its contents change (e.g. the Mac side writes a new keymap name via
    `aros-ctl keymap <name>`), re-applies it with `LoadKeymap RESTORE` so the
    keyboard switches live, without rebooting and without injecting keystrokes
    (which would themselves be remapped by the active keymap).

    It watches a second file in the host share as well. A packaged host
    application may have no writable system directory at all, so its settings
    window cannot write ENVARC:; the share is the one place both sides can
    write. That request names the layout directly and is applied without being
    persisted, leaving ENVARC: as the boot-time choice. Each file is read
    only while its volume or assign exists: asking DOS for a missing one
    would put up an "insert volume" requester nobody can answer.
    Its name is used only if it is a plain keymap name.

    Started at boot with `Run <NIL: >NIL: C:KeymapWatch`. Built with detach=yes
    (detach.o): it loops forever, and a non-detached CLI child would keep the
    boot console window open in front of the desktop after EndCLI.
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#define REQFILE "ENVARC:Keymap"
#define SHAREVOLUME "MacRW"
#define SHAREFILE "MacRW:.pomaros-keymap"
#define POLL_TICKS 40           /* ~0.8s (TICKS_PER_SECOND is 50) */

/* read the trimmed keymap name from `path` into out (<= len); empty if none */
static void readreq(CONST_STRPTR path, char *out, int len)
{
    BPTR f = Open(path, MODE_OLDFILE);
    out[0] = '\0';
    if (f)
    {
        LONG n = Read(f, out, len - 1);
        Close(f);
        if (n > 0)
        {
            char *p;
            out[n] = '\0';
            for (p = out; *p && *p != '\n' && *p != '\r'; p++) ;
            *p = '\0';
        }
        else
            out[0] = '\0';
    }
}

/* Whether a volume or assign of that name is mounted; never a requester. */
static BOOL mounted(CONST_STRPTR name)
{
    struct DosList *dl = LockDosList(LDF_VOLUMES | LDF_ASSIGNS | LDF_READ);
    BOOL found = FindDosEntry(dl, name, LDF_VOLUMES | LDF_ASSIGNS) != NULL;
    UnLockDosList(LDF_VOLUMES | LDF_ASSIGNS | LDF_READ);
    return found;
}

/* The same rule as LoadKeymap's: a plain file name in DEVS:Keymaps. */
static BOOL name_ok(const char *name)
{
    int i, n = strlen(name);
    if (n == 0 || n > 60 || name[0] == '.')
        return FALSE;
    for (i = 0; i < n; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
              || c == '_' || c == '-' || c == '.'))
            return FALSE;
    }
    return TRUE;
}

/* Run one shell command with no console attached. */
static void run_quiet(CONST_STRPTR command)
{
    BPTR in  = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    BPTR out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);

    /* System() closes the supplied handles. */
    SystemTags(command, SYS_Input, (IPTR)in, SYS_Output, (IPTR)out, TAG_DONE);
}

int main(void)
{
    char last[80], cur[80];
    char shared_last[80], shared[80];
    char command[128];

    /* Seed with the current values so we do not re-apply what the boot already
       loaded; only react to later changes. */
    last[0] = '\0';
    if (mounted((CONST_STRPTR)"ENVARC"))
        readreq(REQFILE, last, sizeof(last));
    shared_last[0] = '\0';
    if (mounted((CONST_STRPTR)SHAREVOLUME))
        readreq(SHAREFILE, shared_last, sizeof(shared_last));

    for (;;)
    {
        Delay(POLL_TICKS);

        cur[0] = '\0';
        if (mounted((CONST_STRPTR)"ENVARC"))
            readreq(REQFILE, cur, sizeof(cur));
        if (cur[0] && strcmp(cur, last) != 0)
        {
            /* RESTORE re-reads ENVARC:Keymap (= REQFILE) and SetKeyMapDefault()s it. */
            run_quiet((CONST_STRPTR)"C:LoadKeymap RESTORE");
            strcpy(last, cur);
        }

        shared[0] = '\0';
        if (mounted((CONST_STRPTR)SHAREVOLUME))
            readreq(SHAREFILE, shared, sizeof(shared));
        if (shared[0] && strcmp(shared, shared_last) != 0 && name_ok(shared))
        {
            /* NOPERSIST: the request is the running choice, not the stored one,
               and the stored one may live somewhere we cannot write. */
            snprintf(command, sizeof(command), "C:LoadKeymap \"%s\" NOPERSIST", shared);
            run_quiet((CONST_STRPTR)command);
            strcpy(shared_last, shared);
        }
    }
    return 0;
}
