/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/kernel.h>
#include <proto/openfirmware.h>
#include <aros/kernel.h>
#include <resources/kernel.h>
#include <aros/asmcall.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include "vmpci.h"
#define OOPBase (base->oop)
struct IRQRoute {
    struct Node node;
    OOP_Object *device;
    struct Interrupt *handler;
    APTR cookie;
    volatile uint32_t *table;
    uint32_t saved[4];
    uint16_t cap, control, command;
    uint32_t source;
};
static void ordered(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
/* Volatile C accesses can still become post-indexed loads/stores. Those
 * instructions need software decoding when a stage-2 MMIO trap has ISV=0;
 * QEMU HVF's MMIO path requires a valid instruction syndrome. Keep the
 * hardware access width explicit and use a plain base-register instruction.
 * Ordering remains at the protocol boundaries through ordered(). */
static uint32_t table_read(volatile uint32_t *address)
{
#if defined(__aarch64__)
    uint32_t value;
    __asm__ volatile("ldr %w0, [%1]" : "=r"(value) : "r"(address) : "memory");
    return value;
#else
    return *address;
#endif
}
static void table_write(volatile uint32_t *address,uint32_t value)
{
#if defined(__aarch64__)
    __asm__ volatile("str %w0, [%1]" :: "r"(value), "r"(address) : "memory");
#else
    *address=value;
#endif
}
static uint32_t read_config(struct VMPciBase *base,OOP_Object *dev,unsigned reg,unsigned width)
{
    if (width==1) {
        struct pHidd_PCIDevice_ReadConfigByte m={base->device_methods+moHidd_PCIDevice_ReadConfigByte,reg};
        return OOP_DoMethod(dev,(OOP_Msg)&m);
    }
    if (width==2) {
        struct pHidd_PCIDevice_ReadConfigWord m={base->device_methods+moHidd_PCIDevice_ReadConfigWord,reg};
        return OOP_DoMethod(dev,(OOP_Msg)&m);
    }
    struct pHidd_PCIDevice_ReadConfigLong m={base->device_methods+moHidd_PCIDevice_ReadConfigLong,reg};
    return OOP_DoMethod(dev,(OOP_Msg)&m);
}
static void write_word(struct VMPciBase *base,OOP_Object *dev,unsigned reg,uint16_t value)
{
    struct pHidd_PCIDevice_WriteConfigWord m={base->device_methods+moHidd_PCIDevice_WriteConfigWord,reg,value};
    OOP_DoMethod(dev,(OOP_Msg)&m);
}
static void dispatch(void *data,void *unused)
{
    struct Interrupt *handler=data;
    (void)unused;
    AROS_INTC1(handler->is_Code,handler->is_Data);
}
static void restore(struct VMPciBase *base,struct IRQRoute *route)
{
    /* Stop MSI-X emission before removing its handler. The caller must have
     * stopped its queue/device users before RemoveInterrupt. */
    write_word(base,route->device,route->cap+2,route->control|0x4000);
    (void)read_config(base,route->device,route->cap+2,2);
    ordered();
    for (unsigned i=0;i<4;++i) table_write(&route->table[i],route->saved[i]);
    ordered();
    write_word(base,route->device,route->cap+2,route->control);
    uint16_t command=read_config(base,route->device,4,2);
    write_word(base,route->device,4,(command&~0x400)|(route->command&0x400));
}
static BOOL add_irq(void *context,OOP_Object *dev,struct Interrupt *handler)
{
    struct VMPciBase *base=context;
    APTR KernelBase=base->kernel;
    struct IRQRoute *route=NULL,*existing;
    IPTR owner=0;
    if (!handler->is_Code) return FALSE;
    OOP_GetAttr(dev,base->device_attr+aoHidd_PCIDevice_Driver,&owner);
    if ((OOP_Object *)owner!=base->bridge) return FALSE;
    ObtainSemaphore(&base->irq_lock);
    ForeachNode(&base->irq_routes,existing) {
        if (existing->handler==handler) goto fail;
        if (existing->device==dev) {
            if (existing->handler) goto fail;
            route=existing;
        }
    }
    if (!route && base->msi_next>=base->msi_count) goto fail;
    /* Inspect the bounded standard capability chain, rejecting loops and
     * another active MSI/MSI-X owner before modifying anything. */
    uint64_t visited=0;
    unsigned cap=read_config(base,dev,0x34,1),msix=0;
    for (unsigned steps=0;cap;++steps) {
        if (steps==48 || cap<0x40 || cap>0xfc || (cap&3)) goto fail;
        uint64_t bit=UINT64_C(1)<<(cap/4);
        if (visited&bit) goto fail;
        visited|=bit;
        unsigned id=read_config(base,dev,cap,1);
        if (id==5 && (read_config(base,dev,cap+2,2)&1)) goto fail;
        if (id==0x11) { if (msix || cap>0xf4) goto fail;msix=cap; }
        cap=read_config(base,dev,cap+1,1);
    }
    if (!msix) goto fail;
    uint16_t control=read_config(base,dev,msix+2,2),command=read_config(base,dev,4,2);
    if ((control&0x8000) || !(command&2)) goto fail;
    unsigned entries=(control&0x7ff)+1;
    uint32_t location=read_config(base,dev,msix+4,4);
    unsigned bar=location&7,offset=location&~7U;
    if (bar>5) goto fail;
    for (unsigned i=0;i<=bar;++i) {
        unsigned raw=read_config(base,dev,0x10+4*i,4);
        if (i==bar && (raw&1)) goto fail;
        if (!(raw&1) && (raw&6)==4) { if (i+1==bar) goto fail;++i; }
    }
    IPTR address=0,bytes=0;
    OOP_GetAttr(dev,base->device_attr+aoHidd_PCIDevice_Base0+3*bar,&address);
    OOP_GetAttr(dev,base->device_attr+aoHidd_PCIDevice_Size0+3*bar,&bytes);
    address&=~(IPTR)15;
    unsigned span=entries*16;
    if (!address || offset>bytes || span>bytes-offset || offset>UINTPTR_MAX-address) goto fail;
    address+=offset;
    volatile uint32_t *table=NULL;
    for (unsigned i=0;i<base->host.window_count;++i) {
        const struct PCIECAMWindow *w=&base->host.windows[i];
        if (address>=w->bus && address-w->bus<w->bytes && span<=w->bytes-(address-w->bus))
            table=(void *)(IPTR)(w->cpu+address-w->bus);
    }
    if (!table) goto fail;
    /* One shared vector, table index zero. Refuse unmasked unrelated entries
     * instead of changing other vectors behind a driver's back. */
    for (unsigned i=1;i<entries;++i) if (!(table_read(&table[i*4+3])&1)) goto fail;
    if (!route) {
        route=AllocMem(sizeof(*route),MEMF_PUBLIC|MEMF_CLEAR);
        if (!route) goto fail;
        route->device=dev;
        route->source=base->msi_first+base->msi_next++;
        AddTail(&base->irq_routes,&route->node);
    }
    route->handler=handler;route->table=table;
    route->cap=msix;route->control=control;route->command=command;
    for (unsigned i=0;i<4;++i) route->saved[i]=table_read(&table[i]);
    /* Reserve a source for this PCI device for the bridge lifetime. The
     * resident bridge/root keeps device objects stable; hot removal is not
     * supported. Close/reopen of the SAME reset device may reuse its source:
     * late MSI can only wake its current owner, which must check queue state.
     * Never transfer that source to another device. Retiring one source per
     * session would exhaust the frame during ordinary clipboard/file usage. */
    unsigned source=route->source;
    struct TagItem tags[]={{KERNELTAG_IRQ_TRIGGERLEVEL,2},{KERNELTAG_IRQ_POLARITY,1},{TAG_DONE,0}};
    if (!KrnModifyIRQA(source,tags)) goto clear_route;
    route->cookie=KrnAddIRQHandler(source,dispatch,handler,NULL);
    if (!route->cookie) goto clear_route;
    write_word(base,dev,msix+2,control|0x4000);
    write_word(base,dev,4,command|0x400);
    table_write(&table[3],1);ordered();
    uint64_t doorbell=base->msi_frame+0x40;
    table_write(&table[0],(uint32_t)doorbell);table_write(&table[1],(uint32_t)(doorbell>>32));table_write(&table[2],source);
    ordered();table_write(&table[3],0);ordered();
    write_word(base,dev,msix+2,(control|0x8000)&~0x4000);
    if ((read_config(base,dev,msix+2,2)&0xc000)!=0x8000) {
        restore(base,route);KrnRemIRQHandler(route->cookie);goto clear_route;
    }
    ReleaseSemaphore(&base->irq_lock);return TRUE;
clear_route:
    route->handler=NULL;route->cookie=NULL;route->table=NULL;
fail:
    ReleaseSemaphore(&base->irq_lock);return FALSE;
}
static void remove_irq(void *context,OOP_Object *dev,struct Interrupt *handler)
{
    struct VMPciBase *base=context;
    APTR KernelBase=base->kernel;
    struct IRQRoute *route;
    ObtainSemaphore(&base->irq_lock);
    ForeachNode(&base->irq_routes,route) {
        if (route->device!=dev || route->handler!=handler) continue;
        restore(base,route);
        KrnRemIRQHandler(route->cookie);
        /* Keep only the device/source reservation. No callback or table
         * pointer may outlive the driver's acknowledged reset and removal. */
        route->handler=NULL;route->cookie=NULL;route->table=NULL;break;
    }
    ReleaseSemaphore(&base->irq_lock);
}
static uint32_t be32(const void *data)
{
    const UBYTE *p=data;
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static uintptr_t probing_frame;
static uint32_t frame_type;
static IPTR read_frame(void)
{
    ordered();frame_type=*(volatile uint32_t *)(probing_frame+8);ordered();return 0;
}
BOOL VMPciIRQInit(struct VMPciBase *base,APTR OpenFirmwareBase,APTR pci)
{
    NEWLIST(&base->irq_routes);InitSemaphore(&base->irq_lock);
    APTR parent=OF_FindProperty(pci,"msi-parent");
    if (!parent) return TRUE;
    APTR node=OF_FindNodeByCompatible(NULL,"arm,gic-v2m-frame");
    if (!node) return TRUE; /* Unsupported controller: no callbacks published. */
    APTR phandle=OF_FindProperty(node,"phandle"),reg=OF_FindProperty(node,"reg");
    if (OF_GetPropLen(parent)!=4 || !phandle || OF_GetPropLen(phandle)!=4 ||
        be32(OF_GetPropValue(parent))!=be32(OF_GetPropValue(phandle)) ||
        !reg || OF_GetPropLen(reg)!=16) return FALSE;
    const UBYTE *r=OF_GetPropValue(reg);
    base->msi_frame=((uint64_t)be32(r)<<32)|be32(r+4);
    if (!base->msi_frame || (base->msi_frame&4095) || be32(r+8) || be32(r+12)!=4096) return FALSE;
    /* The admitted VM firmware profile has a direct, identity-translated
     * child of the GIC. Stage2 verified this association before mapping it. */
    Disable();probing_frame=base->msi_frame;Supervisor((APTR)read_frame);Enable();
    base->msi_first=(frame_type>>16)&1023;base->msi_count=frame_type&1023;
    APTR first=OF_FindProperty(node,"arm,msi-base-spi"),count=OF_FindProperty(node,"arm,msi-num-spis");
    if (!!first!=!!count) return FALSE;
    if (first) {
        if (OF_GetPropLen(first)!=4 || OF_GetPropLen(count)!=4) return FALSE;
        base->msi_first=be32(OF_GetPropValue(first));base->msi_count=be32(OF_GetPropValue(count));
    }
    if (base->msi_first<32 || base->msi_first>=1020 || !base->msi_count ||
        base->msi_count>1020-base->msi_first) return FALSE;
    base->device_attr=OOP_ObtainAttrBase(IID_Hidd_PCIDevice);
    if (!base->device_attr) return FALSE;
    base->device_methods=OOP_GetMethodID(IID_Hidd_PCIDevice,0);
    base->host.irq=(struct PCIECAMIRQ){base,add_irq,remove_irq};
    return TRUE;
}
