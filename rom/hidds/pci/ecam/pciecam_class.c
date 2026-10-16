/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/oop.h>
#include <proto/utility.h>
#include "pciecam.h"

static int valid(const struct PCIECAMConfig *h)
{
    if (!h || !h->complete_config_write || !AROSPCIECAMAddress(&h->config,h->config.first_bus,0,0,0,4) ||
        /* The existing PCI enumerator starts at bus zero. */
        h->config.first_bus != 0 ||
        h->window_count > PCIECAM_MAX_WINDOWS ||
        h->config.bytes < ((uint64_t)(h->config.last_bus-h->config.first_bus+1)<<20) ||
        h->config.bytes > UINTPTR_MAX-(uintptr_t)h->config.mapping) return 0;
    unsigned dma_ops=!!h->dma.allocate+!!h->dma.release+!!h->dma.to_bus+!!h->dma.to_cpu;
    if (dma_ops && dma_ops!=4) return 0;
    if (!!h->irq.add != !!h->irq.remove) return 0;
    for (unsigned i=0;i<h->window_count;++i) {
        const struct PCIECAMWindow *w=&h->windows[i];
        if (!w->bytes || w->bus>UINT64_MAX-w->bytes ||
            !w->cpu || w->bytes>UINTPTR_MAX || w->cpu>UINTPTR_MAX-w->bytes ||
            ((w->cpu|w->bus|w->bytes)&4095)) return 0;
        uint64_t config=(uintptr_t)h->config.mapping;
        if (w->cpu<config+h->config.bytes && config<w->cpu+w->bytes) return 0;
        for (unsigned j=0;j<i;++j) {
            const struct PCIECAMWindow *v=&h->windows[j];
            if ((w->bus<v->bus+v->bytes && v->bus<w->bus+w->bytes) ||
                (w->cpu<v->cpu+v->bytes && v->cpu<w->cpu+w->bytes)) return 0;
        }
    }
    return 1;
}
OOP_Object *PCIECAM__Root__New(OOP_Class *cl,OOP_Object *o,struct pRoot_New *msg)
{
    const struct PCIECAMConfig *host=(const void *)GetTagData(
        BASE(cl)->config_attr+aoHidd_PCIECAM_Config,0,msg->attrList);
    if (!valid(host)) return NULL;
    o=(OOP_Object *)OOP_DoSuperMethod(cl,o,(OOP_Msg)msg);
    if (o) ((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host=*host;
    return o;
}
void PCIECAM__Root__Get(OOP_Class *cl,OOP_Object *o,struct pRoot_Get *msg)
{
    if (msg->attrID==BASE(cl)->driver_attr+aoHidd_PCIDriver_DirectBus)
        *msg->storage=FALSE;
    else OOP_DoSuperMethod(cl,o,(OOP_Msg)msg);
}
static int reserved(const struct PCIECAMConfig *h,unsigned bus,unsigned dev)
{
    return dev<32 && bus==h->config.first_bus &&
        (h->reserved_root_slots & (UINT32_C(1)<<dev));
}
#define READ_METHOD(Name,Type,Width) \
Type PCIECAM__Hidd_PCIDriver__ReadConfig##Name(OOP_Class *cl,OOP_Object *o, \
 struct pHidd_PCIDriver_ReadConfig##Name *msg) { \
 const struct PCIECAMConfig *h=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host; \
 return reserved(h,msg->bus,msg->dev) ? (Type)-1 : \
 (Type)AROSPCIECAMRead(&h->config,msg->bus,msg->dev,msg->sub,msg->reg,Width); }
READ_METHOD(Byte,UBYTE,1)
READ_METHOD(Word,UWORD,2)
READ_METHOD(Long,ULONG,4)
#define WRITE_METHOD(Name,Width) \
void PCIECAM__Hidd_PCIDriver__WriteConfig##Name(OOP_Class *cl,OOP_Object *o, \
 struct pHidd_PCIDriver_WriteConfig##Name *msg) { \
 const struct PCIECAMConfig *h=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host; \
 if (!reserved(h,msg->bus,msg->dev) && \
 AROSPCIECAMWrite(&h->config,msg->bus,msg->dev,msg->sub,msg->reg,Width,msg->val)) \
 h->complete_config_write(); }
WRITE_METHOD(Byte,1)
WRITE_METHOD(Word,2)
WRITE_METHOD(Long,4)
APTR PCIECAM__Hidd_PCIDriver__MapPCI(OOP_Class *cl,OOP_Object *o,
 struct pHidd_PCIDriver_MapPCI *msg)
{
    const struct PCIECAMConfig *h=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host;
    uint64_t address=(IPTR)msg->PCIAddress, bytes=msg->Length;
    if (!bytes) return (APTR)-1;
    for (unsigned i=0;i<h->window_count;++i) {
        const struct PCIECAMWindow *w=&h->windows[i];
        if (address>=w->bus && address-w->bus<w->bytes &&
            bytes<=w->bytes-(address-w->bus))
            return (APTR)(IPTR)(w->cpu+(address-w->bus));
    }
    return (APTR)-1;
}
/* The platform owns these permanent mappings. DMA requires explicit callbacks;
 * IRQ admission remains separate. Never inherit implicit identity DMA or
 * legacy interrupt-line assumptions from the base class. */
void PCIECAM__Hidd_PCIDriver__UnmapPCI(OOP_Class *cl,OOP_Object *o,struct pHidd_PCIDriver_UnmapPCI *msg) {(void)cl;(void)o;(void)msg;}
APTR PCIECAM__Hidd_PCIDriver__PCItoCPU(OOP_Class *cl,OOP_Object *o,struct pHidd_PCIDriver_PCItoCPU *msg)
{
    const struct PCIECAMDMA *d=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host.dma;
    return d->to_cpu ? d->to_cpu(d->context,msg->address) : (APTR)-1;
}
APTR PCIECAM__Hidd_PCIDriver__CPUtoPCI(OOP_Class *cl,OOP_Object *o,struct pHidd_PCIDriver_CPUtoPCI *msg)
{
    const struct PCIECAMDMA *d=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host.dma;
    return d->to_bus ? d->to_bus(d->context,msg->address) : (APTR)-1;
}
APTR PCIECAM__Hidd_PCIDriver__AllocPCIMem(OOP_Class *cl,OOP_Object *o,struct pHidd_PCIDriver_AllocPCIMem *msg)
{
    const struct PCIECAMDMA *d=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host.dma;
    return d->allocate && msg->Size ? d->allocate(d->context,msg->Size) : NULL;
}
void PCIECAM__Hidd_PCIDriver__FreePCIMem(OOP_Class *cl,OOP_Object *o,struct pHidd_PCIDriver_FreePCIMem *msg)
{
    const struct PCIECAMDMA *d=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host.dma;
    if (d->release) d->release(d->context,msg->Address);
}
BOOL PCIECAM__Hidd_PCIDriver__AddInterrupt(OOP_Class *cl,OOP_Object *o,struct pHidd_PCIDriver_AddInterrupt *msg)
{
    const struct PCIECAMIRQ *irq=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host.irq;
    return irq->add && msg && msg->device && msg->interrupt ?
        irq->add(irq->context,msg->device,msg->interrupt) : FALSE;
}
void PCIECAM__Hidd_PCIDriver__RemoveInterrupt(OOP_Class *cl,OOP_Object *o,struct pHidd_PCIDriver_RemoveInterrupt *msg)
{
    const struct PCIECAMIRQ *irq=&((struct PCIECAMData *)OOP_INST_DATA(cl,o))->host.irq;
    if (irq->remove && msg && msg->device && msg->interrupt)
        irq->remove(irq->context,msg->device,msg->interrupt);
}
