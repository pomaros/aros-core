/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include <stdint.h>
#include <stddef.h>
typedef uint64_t UQUAD;
typedef uint32_t ULONG;
typedef uintptr_t IPTR;
typedef void *APTR;
#include "task_context.h"

/* Only the Task fields the actual production copy macros dereference. */
struct ETask { void *et_RegFrame; };
struct Task {
    union { struct ETask *tc_ETask; } tc_UnionETask;
    void *tc_SPReg;
};
struct owned {
    struct AROSAArch64NativeTaskContext prefix;
    struct VFPContext simd;
};
_Static_assert(sizeof(struct owned) == AARCH64_NATIVE_TASK_SIZE, "owned size");
_Static_assert(offsetof(struct owned, simd) == AARCH64_FRAME_FPU, "SIMD offset");

void save(struct Task *task, void *frame) { STORE_TASKSTATE(task, frame) }
void restore(struct Task *task, void *frame) { RESTORE_TASKSTATE(task, frame) }

#ifdef NATIVE_TLS_HOST
#include <assert.h>
#include <string.h>
uint64_t host_cpu_tp[2];
unsigned host_cpu;

int main(void)
{
    struct owned a,b,frame,foreign;
    struct ETask ea={&a},eb={&b},ef={&foreign};
    struct Task ta={{&ea},0},tb={{&eb},0},tf={{&ef},0};
    uint64_t anchor_a,anchor_b;
    memset(&a,0xa5,sizeof(a));memset(&b,0x5a,sizeof(b));
    a.prefix.registers.fpuContext=&a.simd;
    b.prefix.registers.fpuContext=&b.simd;
    AROSAArch64NativeTaskTLSInit(&a.prefix.registers);
    AROSAArch64NativeTaskTLSInit(&b.prefix.registers);
    assert(!a.prefix.tls_anchor && !b.prefix.tls_anchor);
    assert(a.simd.fpr[0]==UINT64_C(0xa5a5a5a5a5a5a5a5));
    anchor_a=(uint64_t)(uintptr_t)&a.prefix.tls_anchor;
    anchor_b=(uint64_t)(uintptr_t)&b.prefix.tls_anchor;
    memset(&frame,0x33,sizeof(frame));
    frame.prefix.registers.sp=0x8000;frame.prefix.registers.pc=0x4444;
    frame.prefix.tls_anchor=UINT64_C(0xeeeeeeeeeeeeeeee);
    /* A switch copies registers only: the anchor is not register state. */
    host_cpu=0;host_cpu_tp[0]=anchor_a;
    a.prefix.tls_anchor=UINT64_C(0x1234567887654321);
    save(&ta,&frame);
    assert(a.prefix.tls_anchor==UINT64_C(0x1234567887654321) && ta.tc_SPReg==(void *)0x8000);
    assert(a.prefix.registers.pc==0x4444);
    assert(a.prefix.registers.fpuContext==&a.simd);
    assert(a.simd.fpcr==frame.simd.fpcr && a.simd.fpr[63]==frame.simd.fpr[63]);
    assert(frame.prefix.tls_anchor==UINT64_C(0xeeeeeeeeeeeeeeee));
    tb.tc_SPReg=(void *)0x9000;
    restore(&tb,&frame);
    assert(host_cpu_tp[0]==anchor_b && frame.prefix.registers.sp==0x9000);
    save(&tb,&frame);
    /* Task A migrates onto CPU 1: the same thread pointer as before. */
    host_cpu=1;host_cpu_tp[1]=UINT64_MAX;
    restore(&ta,&frame);
    assert(host_cpu_tp[1]==anchor_a && host_cpu_tp[0]==anchor_b);
    assert(a.prefix.tls_anchor==UINT64_C(0x1234567887654321));
    memset(&foreign,0,sizeof(foreign));
    foreign.prefix.registers.fpuContext=&foreign.simd;
    AROSAArch64NativeTaskTLSInit(&foreign.prefix.registers);
    tf.tc_SPReg=(void *)0xa000;
    restore(&tf,&frame);
    assert(host_cpu_tp[1]==(uint64_t)(uintptr_t)&foreign.prefix.tls_anchor);
    return 0;
}
#endif
