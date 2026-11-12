/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Transport service object: one logical GPU client of the display's owner.
 * Every method forwards to the owner with this object as the identity that
 * owns IDs and backing allocations. */
#include "frontend.h"
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/utility.h>

OOP_Object *VirtioGPUService__Root__New(OOP_Class *cl, OOP_Object *o, struct pRoot_New *msg)
{
    struct VirtioGPU *gpu = NULL;
    struct TagItem *state = msg->attrList, *tag;
    struct VirtioGPUServiceData *d;

    /* No utility.library global base is needed during owner construction. */
    while ((tag = NextTagItem(&state)))
        if (tag->ti_Tag == VIRTIOGPU_OWNER_TAG)
            gpu = (APTR)tag->ti_Data;
    if (!VirtioGPUHasVirGL(gpu) || !VirtioGPUAcquire(gpu))
        return NULL;
    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    if (!o) {
        VirtioGPURelease(gpu);
        return NULL;
    }
    d = OOP_INST_DATA(cl, o);
    d->gpu = gpu;
    d->references = 1;
    InitSemaphore(&d->lock);
    /* Pin the owner module for as long as this service object exists. */
    d->module = OpenLibrary("virtiogfx.hidd", 0);
    if (!d->module) {
        OOP_DisposeObject(o);
        return NULL;
    }
    return o;
}

VOID VirtioGPUService__Root__Dispose(OOP_Class *cl, OOP_Object *o, struct pRoot_Dispose *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    struct Library *module = d->module;

    /* Live or uncertain objects of this identity become orphans; the owner
     * keeps their device-visible state until a safe physical stop. */
    VirtioGPUForgetOwner(d->gpu, o);
    VirtioGPURelease(d->gpu);
    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    if (module)
        CloseLibrary(module);
}

IPTR VirtioGPUService__Hidd_VirtioGPU__Acquire(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_Acquire *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    BOOL okay = FALSE;

    ObtainSemaphore(&d->lock);
    if (msg->version == VIRTIOGPU_TRANSPORT_VERSION && d->references && d->references != ~(ULONG)0) {
        d->references++;
        okay = TRUE;
    }
    ReleaseSemaphore(&d->lock);
    return okay;
}

IPTR VirtioGPUService__Hidd_VirtioGPU__Release(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_Release *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    BOOL dispose = FALSE;

    (void)msg;
    ObtainSemaphore(&d->lock);
    if (d->references)
        dispose = !--d->references;
    ReleaseSemaphore(&d->lock);
    if (dispose)
        OOP_DisposeObject(o);
    return 0;
}

IPTR VirtioGPUService__Hidd_VirtioGPU__Command(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_Command *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    return VirtioGPUCommand(d->gpu, o, msg->request, msg->bytes, msg->response, msg->response_bytes, msg->response_type);
}

IPTR VirtioGPUService__Hidd_VirtioGPU__AllocateID(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_AllocateID *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    return VirtioGPUAllocateID(d->gpu, o, msg->context);
}

IPTR VirtioGPUService__Hidd_VirtioGPU__Healthy(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_Healthy *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    (void)msg;
    return VirtioGPUHasVirGL(d->gpu);
}

IPTR VirtioGPUService__Hidd_VirtioGPU__Allocate(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_Allocate *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    return (IPTR)VirtioGPUAlloc(d->gpu, o, msg->bytes);
}

IPTR VirtioGPUService__Hidd_VirtioGPU__Free(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_Free *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    return VirtioGPUFree(d->gpu, o, msg->pointer);
}

IPTR VirtioGPUService__Hidd_VirtioGPU__Address(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_Address *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    return VirtioGPUAddress(d->gpu, o, msg->pointer, msg->address);
}

IPTR VirtioGPUService__Hidd_VirtioGPU__BackingBegin(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_BackingBegin *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    return VirtioGPUBackingBegin(d->gpu, o, msg->pointer, msg->flags);
}

IPTR VirtioGPUService__Hidd_VirtioGPU__BackingEnd(OOP_Class *cl, OOP_Object *o, struct pHidd_VirtioGPU_BackingEnd *msg)
{
    struct VirtioGPUServiceData *d = OOP_INST_DATA(cl, o);
    VirtioGPUBackingEnd(d->gpu, o, msg->pointer);
    return 0;
}
