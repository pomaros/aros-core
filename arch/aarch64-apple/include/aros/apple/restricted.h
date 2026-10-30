/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_RESTRICTED_H
#define AROS_RESTRICTED_H
#include <stddef.h>
#include <stdint.h>
/* Experimental native control API. Payloads use the scalar SVC ABI only;
 * native libraries and this control table are inaccessible in their root. */
#define AROS_RESTRICTED_KATTR_API UINT32_C(0x83f10021)
#define AROS_RESTRICTED_CODE UINT64_C(0x100000000)
#define AROS_RESTRICTED_DATA UINT64_C(0x100001000)
#define AROS_RESTRICTED_STACK UINT64_C(0x100003000)
#define AROS_RESTRICTED_ENTER 0xa530
#define AROS_RESTRICTED_YIELD 0xa540
#define AROS_RESTRICTED_EXIT 0xa541
#define AROS_RESTRICTED_SUBMIT 0xa542
#define AROS_RESTRICTED_COLLECT 0xa543
#define AROS_RESTRICTED_CALLBACK_RETURN 0xa544
struct AROSRestrictedResult {
    int64_t status;
    uint64_t value, esr, far, pc, yields, denied, replies, rejected, callbacks;
};
struct AROSRestrictedWork {
    uint64_t domain,token,length;
    unsigned char bytes[256];
};
struct AROSRestrictedAPI {
    uint32_t version, size;
    int (*run)(const void *code, size_t bytes, uint64_t seed,
        struct AROSRestrictedResult *result);
    int (*take)(struct AROSRestrictedWork *);
    int (*complete)(const struct AROSRestrictedWork *);
};
#endif
