/*
    Copyright (C) 2013-2026, The AROS Development Team. All rights reserved.
*/

#ifndef KERNEL_INTERN_H_
#define KERNEL_INTERN_H_

#include <aros/libcall.h>
#include <inttypes.h>
#include <exec/lists.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <utility/tagitem.h>
#include <stdio.h>
#include <stdarg.h>

#include <aros/aarch64/cpucontext.h>

#include "kernel_arm.h"

#undef KernelBase
struct KernelBase;

#define KERNEL_PHYS_BASE        0x07800000
#define KERNEL_VIRT_BASE        0xff800000

/* AArch64 MMU control bits */
#define ENABLE_MMU              (1 << 0)
#define ENABLE_D_CACHE          (1 << 2)
#define ENABLE_I_CACHE          (1 << 12)

void dt_set_root(void * r);
void *dt_find_node_by_phandle(uint32_t phandle);
void *dt_find_node(char *key);
void *dt_find_property(void *key, char *propname);
int dt_get_prop_len(void *prop);
void *dt_get_prop_value(void *prop);

void cpu_Probe(struct ARM_Implementation *);
void cpu_Init(struct ARM_Implementation *, struct TagItem *);

void platform_Init(struct ARM_Implementation *, struct TagItem *);

void core_SetupMMU(struct TagItem *msg);
void core_SetupIntr(void);
void core_SetupIntrCore(void);

void *KrnAddSysTimerHandler(struct KernelBase *);

intptr_t krnGetTagData(Tag tagValue, intptr_t defaultVal, const struct TagItem *tagList);
struct TagItem *krnFindTagItem(Tag tagValue, const struct TagItem *tagList);
struct TagItem *krnNextTagItem(const struct TagItem **tagListPtr);

struct KernelBase *getKernelBase();

#ifdef bug
#undef bug
#endif
#ifdef D
#undef D
#endif
#if DEBUG
#define D(x) x
#define DALLOCMEM(x)
#else
#define D(x)
#define DALLOCMEM(x)
#endif

#define __STR(x) #x
#define STR(x) __STR(x)

AROS_LD2(int, KrnBug,
         AROS_LDA(const char *, format, A0),
         AROS_LDA(va_list, args, A1),
         struct KernelBase *, KernelBase, 12, Kernel);

static inline void bug(const char *format, ...)
{
    struct KernelBase *kbase = getKernelBase();
    va_list args;
    /* KrnBug is reached through KernelBase's jumptable; before KernelBase
     * exists (early kernel_cstart) the call would dereference NULL. Bail
     * out so early callers are no-ops -- they use uart_puts for tracing. */
    if (!kbase)
        return;
    va_start(args, format);
    AROS_SLIB_ENTRY(KrnBug, Kernel, 12)(format, args, kbase);
    va_end(args);
}

/* Owned context and vector-copy contract, shared with layout fixtures. */
#include "task_context.h"

#endif /*KERNEL_INTERN_H_*/
