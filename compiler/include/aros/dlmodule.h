#ifndef AROS_DLMODULE_H
#define AROS_DLMODULE_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: the descriptor of a module that dlopen() loads

    A dlopen module is an ELF object linked like an AROS program, so that
    LoadSeg() loads and relocates it, but with dlmodule-startup.o in place
    of the program startup. The code at the start of its first hunk is the
    module's entry, marked with AROS_DLMODULE_MAGIC and the version at
    AROS_DLMODULE_MARK_OFFSET. Called with argsize AROS_DLMODULE_MAGIC, as
    RunCommand() calls a program, it returns the module's descriptor; called
    any other way (run from a Shell) it returns RETURN_FAIL and does nothing.
*/

#include <exec/types.h>

#define AROS_DLMODULE_MAGIC   0x41444C4DUL      /* 'ADLM' */
#define AROS_DLMODULE_VERSION 1

/* The entry starts with a 4-byte branch (a short jump and two NOPs on
   x86_64) over two 32-bit words, the magic and the version, at this offset
   from the start of the first hunk's code. */
#define AROS_DLMODULE_MARK_OFFSET 4

/* One exported symbol. dlmodule-link generates the table, sorted by
   strcmp() on de_Name, so that dlsym() can search it by bisection. */
struct AROSDLExport
{
    const char *de_Name;
    void       *de_Address;
};

struct AROSDLModule
{
    ULONG                       dm_Magic;       /* AROS_DLMODULE_MAGIC */
    UWORD                       dm_Version;     /* AROS_DLMODULE_VERSION */
    UWORD                       dm_Reserved;
    /* Opens the module's automatic libraries and runs its INIT, CTORS and
       init_array sets, as genmodule's InitLib does for a library. Returns
       FALSE, with everything it opened closed again, on failure. Called once,
       by the first dlopen() of the module, in the caller's task. */
    BOOL                      (*dm_Init)(void);
    /* The reverse: fini_array, DTORS, EXIT, then the libraries. Called once,
       by the dlclose() that releases the last reference. */
    void                      (*dm_Expunge)(void);
    const struct AROSDLExport  *dm_Exports;
    ULONG                       dm_ExportCount;
};

#endif /* AROS_DLMODULE_H */
