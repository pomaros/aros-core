/*
    Copyright (C) 2026 John Knipper. All rights reserved.
    Licensed under the AROS Public License 1.1.

    Desc: Compile-time identity probe for the freestanding Apple target.
*/

#include <aros/apple/bootinfo.h>

#include "bootinfo_validate.h"

#if !defined(__aarch64__)
# error "The Apple Silicon target requires an AArch64 compiler"
#endif

#if !defined(__AROS__)
# error "The Apple Silicon target requires the AROS cross toolchain"
#endif

const char AROSAppleTargetIdentity[] = "AROS:apple-aarch64:probe-v1";
const unsigned int AROSAppleBootInfoSize = sizeof(struct AROSBootInfo);
enum AROSAppleBootInfoStatus (*const AROSAppleBootInfoValidator)(
    const struct AROSBootInfo *) = AROSAppleBootInfoValidate;
