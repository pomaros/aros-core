/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/kernel.h>
#include <aros/kernel.h>
#include <exec/memory.h>
#include "vmpci.h"

/* CPU/bus translation is used by interrupt handlers. This VM profile has
 * one CPU: mask interrupts only while publishing, unlinking or searching the
 * registry; never allocate, free or change mappings inside that section.
 * Callers must quiesce DMA/IRQ users before releasing their owned buffer.
 * A future SMP profile needs a cross-CPU IRQ-safe registry lock. */
struct DMABuffer {
    struct Node node;
    void *cpu;
    uintptr_t bus;
    uint32_t requested, allocated;
};
static void *allocate(void *context,uint32_t bytes)
{
    struct VMPciBase *base=context;
    APTR KernelBase=base->kernel;
    if (!bytes || bytes>UINT32_MAX-4095) return NULL;
    uint32_t rounded=(bytes+4095)&~4095u;
    struct DMABuffer *b=AllocMem(sizeof(*b),MEMF_PUBLIC|MEMF_CLEAR);
    if (!b) return NULL;
    b->cpu=KrnAllocPages(NULL,rounded,MEMF_CLEAR);
    if (!b->cpu) { FreeMem(b,sizeof(*b));return NULL; }
    b->bus=(uintptr_t)KrnVirtualToPhysical(b->cpu);
    /* This software-VM profile has coherent, guest-physical DMA, no IOMMU.
     * Check every allocated page. Do not infer this policy for native PCI. */
    if (!b->bus || (b->bus&4095) || ((uintptr_t)b->cpu&4095) ||
        rounded>UINTPTR_MAX-b->bus || rounded>UINTPTR_MAX-(uintptr_t)b->cpu) goto fail;
    for (uint32_t offset=0;offset<rounded;offset+=4096)
        if ((uintptr_t)KrnVirtualToPhysical((UBYTE *)b->cpu+offset)!=b->bus+offset) goto fail;
    b->requested=bytes;b->allocated=rounded;
    KrnSetProtection(b->cpu,rounded,MAP_Readable|MAP_Writable);
    Disable();
    AddTail(&base->dma_buffers,&b->node);
    Enable();
    return b->cpu;
fail:
    KrnFreePages(b->cpu,rounded);FreeMem(b,sizeof(*b));return NULL;
}
static void release(void *context,void *address)
{
    struct VMPciBase *base=context;
    APTR KernelBase=base->kernel;
    struct DMABuffer *b;
    Disable();
    ForeachNode(&base->dma_buffers,b) {
        if (b->cpu==address) {
            Remove(&b->node);
            Enable();
            KrnFreePages(b->cpu,b->allocated);FreeMem(b,sizeof(*b));return;
        }
    }
    Enable();
}
static void *translate(void *context,void *address,int inverse)
{
    struct VMPciBase *base=context;
    struct DMABuffer *b;
    uintptr_t value=(uintptr_t)address;
    void *result=(void *)-1;
    Disable();
    ForeachNode(&base->dma_buffers,b) {
        uintptr_t start=inverse?b->bus:(uintptr_t)b->cpu;
        if (value>=start && value-start<b->requested) {
            result=(void *)((inverse?(uintptr_t)b->cpu:b->bus)+(value-start));break;
        }
    }
    Enable();
    return result;
}
static void *to_bus(void *context,void *address) { return translate(context,address,0); }
static void *to_cpu(void *context,void *address) { return translate(context,address,1); }
void VMPciDMAInit(struct VMPciBase *base)
{
    NEWLIST(&base->dma_buffers);
    base->host.dma=(struct PCIECAMDMA){base,allocate,release,to_bus,to_cpu};
}
