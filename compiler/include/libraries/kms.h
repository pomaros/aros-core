/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License, version 1.1.
 */
/*
    Copyright (C) 2011, The AROS Development Team. All rights reserved.
*/

#ifndef LIBRARIES_KMS_H
#define LIBRARIES_KMS_H 1

#include <devices/keymap.h>
#include <exec/libraries.h>

#define	KMSNAME "kms.library"

struct KMSLibrary
{
    struct Library kms_Lib;
    /* Configuration is owned by keymap.library, via ConfigureKeyMapSwitcher. */
};

#define KMS_QUAL_DISABLE 0xFFFF	/* Disabled switcher qualifier in preferences */
#define KMS_CODE_NOKEY   0xFFFF /* Qualifier-only hotkey in preferences */

#endif
