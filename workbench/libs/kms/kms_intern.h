/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License, version 1.1.
 */


#include <exec/interrupts.h>
#include <exec/semaphores.h>
#include <dos/bptr.h>

#include <libraries/kms.h>

#define PREFIX_STR "DEVS:Keymaps"
#define PREFIX_LEN 14			/* Two more bytes for '/' and NULL terminator */

struct kms_base
{
    struct KMSLibrary pub;
    struct KeyMapResource *kmr;
    struct Interrupt input_Int;
    struct Library *keymap; /* Initialized before the input handler is installed. */
    struct SignalSemaphore akmd_lock;
};

/* routine to expand the keymap into le/64bit usable data */
extern BPTR parsekeymapseg(BPTR km_seg);
