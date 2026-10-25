/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
/* HOST type adapters match the pinned AArch64 public scalar widths. Actual
 * production context and TLS helper definitions are included unchanged. */
typedef uint64_t UQUAD;
typedef uint32_t ULONG;
typedef void *APTR;
#include "task_tls.h"

uint64_t host_cpu_tp[2];
unsigned host_cpu;

struct owned {
    struct AROSAppleTaskContext context;
    uint64_t simd[66];
};
_Static_assert(sizeof(struct owned) == AROS_APPLE_DISPATCH_CONTEXT_SIZE,
    "private task pointer does not grow owned context");
_Static_assert(offsetof(struct owned, simd) == AROS_APPLE_DISPATCH_SIMD_OFFSET,
    "SIMD offset is preserved");

int main(void)
{
    struct owned a,b,foreign;
    unsigned char prefix[280];
    memset(&a,0xa5,sizeof(a));
    a.context.registers.sp=0x1000;
    a.context.registers.fpuContext=a.simd;
    memcpy(prefix,&a.context.registers,sizeof(prefix));
    AROSAppleTaskTLSInit(&a.context.registers);
    assert(a.context.tls_anchor==0);
    assert(!memcmp(prefix,&a.context.registers,sizeof(prefix)));
    assert(a.simd[0]==UINT64_C(0xa5a5a5a5a5a5a5a5));
    memset(&b,0x5a,sizeof(b));
    AROSAppleTaskTLSInit(&b.context.registers);

    /* Dispatch installs the address of the task's own anchor: the same
     * value for the task's whole life, whatever the runtime stores in it. */
    host_cpu=0;host_cpu_tp[0]=UINT64_MAX;
    AROSAppleTaskTLSRestore(&a.context.registers);
    assert(host_cpu_tp[0]==(uint64_t)(uintptr_t)&a.context.tls_anchor);
    a.context.tls_anchor=UINT64_C(0x1234567887654321);
    AROSAppleTaskTLSRestore(&b.context.registers);
    assert(host_cpu_tp[0]==(uint64_t)(uintptr_t)&b.context.tls_anchor);

    /* Task A migrates onto another CPU: same thread pointer, anchor kept. */
    host_cpu=1;host_cpu_tp[1]=UINT64_C(0xeeeeeeeeeeeeeeee);
    AROSAppleTaskTLSRestore(&a.context.registers);
    assert(host_cpu_tp[1]==(uint64_t)(uintptr_t)&a.context.tls_anchor);
    assert(a.context.tls_anchor==UINT64_C(0x1234567887654321));
    assert(host_cpu_tp[0]==(uint64_t)(uintptr_t)&b.context.tls_anchor);

    /* A library/OOP/non-pthread task takes exactly the same context path. */
    memset(&foreign,0xcc,sizeof(foreign));
    AROSAppleTaskTLSInit(&foreign.context.registers);
    AROSAppleTaskTLSRestore(&foreign.context.registers);
    assert(host_cpu_tp[1]==(uint64_t)(uintptr_t)&foreign.context.tls_anchor);
    assert(foreign.context.tls_anchor==0);
    return 0;
}
