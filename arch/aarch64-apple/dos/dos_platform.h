/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#ifndef DOS_PLATFORM_H
#define DOS_PLATFORM_H

/* The native startup script loads saved screen preferences synchronously
   before G4Ready releases Wanderer and its visible Shell. Opening the boot
   console earlier would create Workbench with default screen preferences.
   An explicitly requested interactive boot still gets its ordinary console.
   Process creation, script execution and ordinary CON: are unchanged. */
#define DOS_INITIAL_CONSOLE_NAME(flags) \
    (((flags) & BF_NO_STARTUP_SEQUENCE) ? \
        "CON:////AROS/AUTO/CLOSE/SMART/BOOT" : "NIL:")

#endif
