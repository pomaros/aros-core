/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Reuse the upstream Exec TLS layout with an EL0-readable CPU anchor. */
#ifndef APPLE_EXEC_TLS_H
#define APPLE_EXEC_TLS_H
#include <exec/types.h>
#include <aros/apple/runtime_local.h>
#define TLS_PTR_GET() \
    ({ struct AROSAppleRuntimeLocal *__anchor; \
       __asm__ volatile("mrs %0,tpidrro_el0":"=r"(__anchor)); \
       /* Exec access before CPU binding is a boot-order contract violation. */ \
       if (!__anchor || !__anchor->exec_tls) __builtin_trap(); \
       (tls_t *)__anchor->exec_tls; })
#include "../../aarch64-native/kernel/tls.h"
#endif
