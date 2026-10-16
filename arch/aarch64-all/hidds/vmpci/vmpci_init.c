/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/openfirmware.h>
#include <proto/kernel.h>
#include <aros/kernel.h>
#include <aros/symbolsets.h>
#include <interface/HW.h>
#include "vmpci.h"
#include <aros/debug.h>

static uint32_t cell(const UBYTE *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void complete_write(void) { __asm__ volatile("dsb sy" ::: "memory"); }
/* Library initialization is serialized; this pointer is only used by the
 * synchronous Supervisor call, not by the permanent driver. */
static struct PCIECAMConfig *initializing;
static IPTR reserve_console(void)
{
    for (unsigned slot=0;slot<32;++slot)
        if (AROSPCIECAMRead(&initializing->config,0,slot,0,0,4)==0x10431af4)
            initializing->reserved_root_slots |= UINT32_C(1)<<slot;
    return 0;
}
/* Resident modules are available before DOS can resolve DEVS: paths. */
static struct Library *open_driver(CONST_STRPTR name, CONST_STRPTR path)
{
    struct Library *lib=OpenLibrary(name,0);
    return lib ? lib : OpenLibrary(path,0);
}
static int init(struct VMPciBase *base)
{
    struct Library *OOPBase=base->oop;
    APTR OpenFirmwareBase=OpenResource("openfirmware.resource");
    APTR KernelBase=OpenResource("kernel.resource");
    OOP_AttrBase attr=0;
    BOOL admitted=FALSE;
    struct PCIECAMConfig *host=&base->host;
    if (!OpenFirmwareBase || !KernelBase) { bug("[VMPci] missing resources\n"); return FALSE; }
    /* This module admits the firmware-generated VM profile only. Generic
     * ECAM remains reusable by platforms with their own mapping policy. */
    if (!OF_FindNodeByCompatible(NULL,"aros,virtualization")) return FALSE;
    APTR node=OF_FindNodeByCompatible(NULL,"pci-host-ecam-generic");
    APTR reg=node?OF_FindProperty(node,"reg"):NULL;
    APTR buses=node?OF_FindProperty(node,"bus-range"):NULL;
    APTR ranges=node?OF_FindProperty(node,"ranges"):NULL;
    if (!reg || OF_GetPropLen(reg)!=16 || !buses || OF_GetPropLen(buses)!=8 || !ranges)
        return FALSE;
    const UBYTE *r=OF_GetPropValue(reg),*b=OF_GetPropValue(buses);
    host->config.mapping=(void *)(IPTR)(((uint64_t)cell(r)<<32)|cell(r+4));
    host->config.bytes=((uint64_t)cell(r+8)<<32)|cell(r+12);
    host->config.first_bus=cell(b);host->config.last_bus=cell(b+4);
    if (host->config.first_bus || host->config.bytes<(1u<<20)) return FALSE;
    unsigned bytes=OF_GetPropLen(ranges);
    if (!bytes || bytes%28 || bytes>PCIECAM_MAX_WINDOWS*28) return FALSE;
    r=OF_GetPropValue(ranges);
    for (unsigned at=0;at<bytes;at+=28) {
        unsigned space=(cell(r+at)>>24)&3;
        if (space==1) continue;
        if (space!=2 && space!=3) return FALSE;
        struct PCIECAMWindow *w=&host->windows[host->window_count++];
        w->bus=((uint64_t)cell(r+at+4)<<32)|cell(r+at+8);
        w->cpu=((uint64_t)cell(r+at+12)<<32)|cell(r+at+16);
        w->bytes=((uint64_t)cell(r+at+20)<<32)|cell(r+at+24);
        if (w->bytes>UINT32_MAX) return FALSE;
    }
    host->complete_config_write=complete_write;
    const char *stage="hiddclass";
    base->hidd=open_driver("hiddclass.hidd","DEVS:Drivers/hiddclass.hidd");
    if (!base->hidd) goto fail;
    stage="pci";
    base->pci=open_driver("pci.hidd","DEVS:Drivers/pci.hidd");
    if (!base->pci) goto fail;
    stage="ecam";
    base->ecam=open_driver("pciecam.hidd","DEVS:Drivers/pciecam.hidd");
    if (!base->ecam) goto fail;
    stage="attributes";
    attr=OOP_ObtainAttrBase(IID_Hidd_PCIECAM);
    if (!attr) goto fail;
    struct TagItem tags[]={{attr+aoHidd_PCIECAM_Config,(IPTR)host},{TAG_DONE,0}};
    /* Constructor validates bounds/alignment/overlap before access admission.
     * Stage2 has separately checked these retained apertures against RAM. */
    stage="mapping validation";
    OOP_Object *check=OOP_NewObject(NULL,CLID_Hidd_PCIECAM,tags);
    if (!check) goto fail;
    OOP_DisposeObject(check);
    initializing=host;Supervisor((APTR)reserve_console);initializing=NULL;
    host->config.last_bus=0;host->config.bytes=1u<<20;
    KrnSetProtection((APTR)host->config.mapping,host->config.bytes,MAP_Readable|MAP_Writable);
    for (unsigned i=0;i<host->window_count;++i)
        KrnSetProtection((APTR)(IPTR)host->windows[i].cpu,host->windows[i].bytes,MAP_Readable|MAP_Writable);
    admitted=TRUE;
    base->kernel=KernelBase;
    VMPciDMAInit(base);
    stage="IRQ admission";
    if (!VMPciIRQInit(base,OpenFirmwareBase,node)) goto fail;
    stage="PCI root";
    base->root=OOP_NewObject(NULL,CLID_Hidd_PCI,NULL);
    if (!base->root) goto fail;
    struct pHW_AddDriver add={OOP_GetMethodID(IID_HW,moHW_AddDriver),OOP_FindClass(CLID_Hidd_PCIECAM),tags};
    stage="bridge registration";
    base->bridge=(OOP_Object *)OOP_DoMethod(base->root,(OOP_Msg)&add);
    if (!base->bridge) goto fail;
    OOP_ReleaseAttrBase(IID_Hidd_PCIECAM);
    /* noexpunge: the module owns callback code, root, bridge and mappings for
     * the VM lifetime. DMA and IRQ callbacks use explicit resident platform policies. */
    bug("[VMPci] resident bridge registered\n");
    return TRUE;
fail:
    bug("[VMPci] init failed at %s\n",stage);
    if (base->root) { OOP_DisposeObject(base->root);base->root=NULL; }
    if (admitted) {
        KrnSetProtection((APTR)host->config.mapping,host->config.bytes,MAP_Readable|MAP_Writable|MAP_Supervisor);
        for (unsigned i=0;i<host->window_count;++i)
            KrnSetProtection((APTR)(IPTR)host->windows[i].cpu,host->windows[i].bytes,MAP_Readable|MAP_Writable|MAP_Supervisor);
    }
    if (base->device_attr) { OOP_ReleaseAttrBase(IID_Hidd_PCIDevice);base->device_attr=0; }
    if (attr) OOP_ReleaseAttrBase(IID_Hidd_PCIECAM);
    if (base->ecam) { CloseLibrary(base->ecam);base->ecam=NULL; }
    if (base->pci) { CloseLibrary(base->pci);base->pci=NULL; }
    if (base->hidd) { CloseLibrary(base->hidd);base->hidd=NULL; }
    return FALSE;
}
ADD2INITLIB(init, 0)
