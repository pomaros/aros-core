/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1.

    Desc: Virtio GPU driver using the shared framebuffer frontend.
*/

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/oop.h>

#include <exec/types.h>
#include <exec/lists.h>
#include <graphics/driver.h>
#include <graphics/gfxbase.h>
#include <hidd/gfx.h>
#include <oop/oop.h>
#include <utility/utility.h>
#include <aros/symbolsets.h>

#include "frontend.h"
#include "transport.h"
#include <hidd/pci.h>
#include <utility/hooks.h>
#include <aros/asmcall.h>

#include LC_LIBDEFS_FILE

#define DEBUG 0
#include <aros/debug.h>

static void FreeAttrBases(const STRPTR *iftable, OOP_AttrBase *bases, ULONG num)
{
    ULONG i;

    for (i = 0; i < num; i++)
    {
        if (bases[i])
        {
            OOP_ReleaseAttrBase(iftable[i]);
            bases[i] = 0;
        }
    }
}

static BOOL GetAttrBases(const STRPTR *iftable, OOP_AttrBase *bases, ULONG num)
{
    ULONG i;

    for (i = 0; i < num; i++)
    {
        bases[i] = OOP_ObtainAttrBase(iftable[i]);
        if (!bases[i])
        {
            FreeAttrBases(iftable, bases, i);
            return FALSE;
        }
    }

    return TRUE;
}

/* These must stay in the same order as attrBases[] entries assignment in efifbgfx_intern.h */
static const STRPTR interfaces[ATTRBASES_NUM] =
{
    IID_Hidd_ChunkyBM,
    IID_Hidd_BitMap,
    IID_Hidd_Gfx,
    IID_Hidd_PixFmt,
    IID_Hidd_Sync,
    IID_Hidd,
    IID_Hidd_Display,
    IID_Hidd_DMEnum
};

/* Kept alive for the installed driver, including reset quarantine. */
static OOP_Object *pci_root;
static struct VirtioGPU *gpu;

AROS_UFH3(static IPTR, FindGPU,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(OOP_Object *, device, A2),
    AROS_UFHA(APTR, message, A1))
{
    AROS_USERFUNC_INIT
    (void)hook; (void)message;
    if (!gpu)
        VirtioGPUOpen(OOPBase, device, &gpu);
    return 0;
    AROS_USERFUNC_EXIT
}
static void Present(APTR context, ULONG x, ULONG y, ULONG w, ULONG h)
{
    /* The transport reports the transition once and retains pending DMA until
     * it can reap a late completion. Repeated draws must not flood the log. */
    VirtioGPUPresent(context, x, y, w, h);
}
BOOL initEFIFBGfxHW(struct HWData *data)
{
    struct Hook hook = {0};
    struct VirtioGPUSurface surface;
    pci_root = OOP_NewObject(NULL, CLID_Hidd_PCI, NULL);
    if (!pci_root) { bug("[VirtioGfx] PCI root unavailable\n"); return FALSE; }
    hook.h_Entry = (HOOKFUNC)FindGPU;
    struct pHidd_PCI_EnumDevices enumerate = {
        OOP_GetMethodID(IID_Hidd_PCI, moHidd_PCI_EnumDevices), &hook, NULL};
    OOP_DoMethod(pci_root, (OOP_Msg)&enumerate);
    if (!VirtioGPUGetSurface(gpu, &surface)) { bug("[VirtioGfx] no usable GPU surface\n"); return FALSE; }
    data->framebuffer = surface.pixels;
    data->width = surface.width; data->height = surface.height;
    data->bytesperline = surface.stride;
    data->fbsize = surface.stride * surface.height;
    data->depth = 24; data->bitsperpixel = 32; data->bytesperpixel = 4;
    data->redmask = 0xff0000; data->greenmask = 0xff00; data->bluemask = 0xff;
    /* HIDD shifts align 32-bit color components, not byte offsets. */
    data->redshift = 8; data->greenshift = 16; data->blueshift = 24;
    data->present = Present; data->present_data = gpu;
    return TRUE;
}

static int EFIFBGfx_Init(LIBBASETYPEPTR LIBBASE)
{
    struct EFIFBGfx_staticdata *xsd = &LIBBASE->vsd;
    struct GfxBase *GfxBase;
    ULONG err;
    int res = FALSE;

    /*
     * Open graphics.library ourselves because we will close it
     * after adding the driver.
     * Autoinit code would close it only upon driver expunge.
     */
    bug("[VirtioGfx] init begin\n");
    GfxBase = (struct GfxBase *)TaggedOpenLibrary(TAGGEDOPEN_GRAPHICS);
    if (GfxBase)
    {
        if (initEFIFBGfxHW(&xsd->data))
        {
            if (GetAttrBases(interfaces, xsd->attrBases, ATTRBASES_NUM))
            {
                xsd->basebm = OOP_FindClass(CLID_Hidd_BitMap);
                xsd->mid_Dispose = OOP_GetMethodID(IID_Root, moRoot_Dispose);
                D(bug("[EFIFBGfx] BitMap class @ 0x%p\n", xsd->basebm));

                InitSemaphore(&xsd->framebufferlock);
                InitSemaphore(&xsd->HW_acc);

                D(bug("[EFIFBGfx] Init: Everything OK, installing driver\n"));

                /* Fixed boot display: instantiate when graphics requests a mode,
                 * after the deferred UI dependencies have been initialized. */
                err = AddDisplayDriver(xsd->efifbgfxclass, NULL,
                                       DDRV_BootMode, TRUE, TAG_DONE);

                bug("[VirtioGfx] AddDisplayDriver result %u\n", err);
                if (!err)
                {
                    /* expunge protection */
                    LIBBASE->library.lib_OpenCnt = 1;
                    res = TRUE;
                    bug("[VM] virtio graphics driver registered\n");
                }
            }
        }
        CloseLibrary(&GfxBase->LibNode);
    }
    else
    {
        D(bug("[EFIFBGfx] Failed to open graphics.library!\n"));
    }
    if (!res) {
        FreeAttrBases(interfaces, xsd->attrBases, ATTRBASES_NUM);
        if (gpu && !VirtioGPUClose(gpu)) {
            /* Retain code and OOP dependencies while DMA can still execute. */
            LIBBASE->library.lib_OpenCnt = 1;
            bug("[VirtioGfx] reset unacknowledged; driver quarantined\n");
            return TRUE;
        }
        gpu = NULL;
        if (pci_root) OOP_DisposeObject(pci_root);
        pci_root = NULL;
    }
    return res;
}

ADD2INITLIB(EFIFBGfx_Init, 0)
