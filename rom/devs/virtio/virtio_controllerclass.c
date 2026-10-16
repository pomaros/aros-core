/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <proto/exec.h>
#define __NOLIBBASE__
#include <proto/kernel.h>
#include <proto/utility.h>
#include <proto/alib.h>

#include <hidd/hidd.h>
#include <hidd/pci.h>
#include <hidd/virtio.h>
#include <hidd/storage.h>

#include <oop/oop.h>
#include <utility/tagitem.h>

#include <stdio.h>
#include <string.h>

#include LC_LIBDEFS_FILE

#include "virtio_debug.h"
#include "virtio_intern.h"
#include "virtio_hw.h"
#include "virtio_queue.h"

extern const char GM_UNIQUENAME(LibName)[];

static AROS_INTH1(VIRTIO_ResetHandler, device_t, dev)
{
    AROS_INTFUNC_INIT

    /* Best-effort: attempt to put the device back to status 0. */
    if (dev && dev->dev_CommonCfg) {
        dev->dev_CommonCfg->device_status = 0;
    }
    return TRUE;

    AROS_INTFUNC_EXIT
}

OOP_Object *VirtIO__Root__New(OOP_Class *cl, OOP_Object *o, struct pRoot_New *msg)
{
    struct VirtIOBase *VirtIOBase = (struct VirtIOBase *)cl->UserData;
    device_t dev = (device_t)GetTagData(aHidd_DriverData, 0, msg->attrList);
    OOP_Object *vController;

    vController = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    if (vController) {
        struct TagItem pciActivateBusmaster[] = {
            { aHidd_PCIDevice_isMEM, TRUE },
            { aHidd_PCIDevice_isMaster, TRUE },
            { TAG_DONE, 0UL },
        };
        struct virtio_Controller *data = OOP_INST_DATA(cl, vController);
        UQUAD wanted_features;

        D(bug("[VIRTIO:Controller] %s: Obj @ %p, dev=%p\n", __func__, vController, dev);)

        data->ac_Class  = cl;
        data->ac_Object = vController;
        data->ac_dev    = dev;

        if (!dev) {
            bug("[VIRTIO:Controller] %s: device data missing\n", __func__);
            OOP_DisposeObject(vController);
            return NULL;
        }

        dev->dev_Controller = vController;

        /* Modern transport: walk capability list and map common/notify/ISR/device cfg. */
        if (!virtio_hw_map(dev)) {
            D(bug("[VIRTIO:Controller] %s: hw_map failed (legacy device unsupported)\n", __func__);)
            OOP_DisposeObject(vController);
            return NULL;
        }

        /* Only block devices require an aligned 64-bit capacity field. */
        if (dev->dev_PCIMapping.bytes[3] < 8 || ((IPTR)dev->dev_DeviceCfg & 7)) {
            OOP_DisposeObject(vController);
            return NULL;
        }

        data->ac_PCICommand = HIDD_PCIDevice_ReadConfigWord(dev->dev_Object, 4);
        data->ac_Activated = TRUE;
        OOP_SetAttrs(dev->dev_Object, (struct TagItem *)pciActivateBusmaster);

        if (!virtio_hw_reset(dev)) {
            bug("[VIRTIO:Controller] %s: device reset failed\n", __func__);
            OOP_DisposeObject(vController);
            return NULL;
        }

        wanted_features =
            ((UQUAD)1 << VIRTIO_F_VERSION_1)        |
            ((UQUAD)1 << VIRTIO_F_ANY_LAYOUT)       |
            ((UQUAD)1 << VIRTIO_BLK_F_SIZE_MAX)     |
            ((UQUAD)1 << VIRTIO_BLK_F_SEG_MAX)      |
            ((UQUAD)1 << VIRTIO_BLK_F_GEOMETRY)     |
            ((UQUAD)1 << VIRTIO_BLK_F_BLK_SIZE)     |
            ((UQUAD)1 << VIRTIO_BLK_F_FLUSH)        |
            ((UQUAD)1 << VIRTIO_BLK_F_RO);

        /* Feature numbers are device-specific. Keep block configuration
         * bounds here, not in the transport shared with GPU/network devices. */
        if (dev->dev_PCIMapping.bytes[3] < 24)
            wanted_features &= ~((UQUAD)1 << VIRTIO_BLK_F_BLK_SIZE);
        if (dev->dev_PCIMapping.bytes[3] < 20)
            wanted_features &= ~((UQUAD)1 << VIRTIO_BLK_F_GEOMETRY);

        if (!virtio_hw_negotiate_features(dev, wanted_features)) {
            bug("[VIRTIO:Controller] %s: feature negotiation failed\n", __func__);
            OOP_DisposeObject(vController);
            return NULL;
        }

        virtio_hw_read_blk_config(dev);

        /*
         * Allocate and configure the single IO virtqueue.
         */
        dev->dev_VQ = virtio_alloc_queue(dev, 0, 128);
        if (!dev->dev_VQ) {
            bug("[VIRTIO:Controller] %s: failed to allocate virtqueue\n", __func__);
            OOP_DisposeObject(vController);
            return NULL;
        }
        if (!virtio_hw_setup_queue(dev, 0, dev->dev_VQ)) {
            bug("[VIRTIO:Controller] %s: failed to register virtqueue with device\n", __func__);
            OOP_DisposeObject(vController);
            return NULL;
        }

        /* Now publish the bus. We always advertise exactly one unit per controller. */
        {
            struct TagItem attrs[] = {
                {aHidd_Name,                (IPTR)GM_UNIQUENAME(LibName)                    },
                {aHidd_HardwareName,        0                                               },
#define VBUS_HARDWARENAME 1
                {aHidd_Producer,            GetTagData(aHidd_Producer, 0, msg->attrList)    },
                {aHidd_Product,             GetTagData(aHidd_Product, 0, msg->attrList)     },
                {aHidd_DriverData,          (IPTR)dev                                       },
                {aHidd_Bus_MaxUnits,        1                                               },
                {aHidd_StorageUnit_Model,   (IPTR)"VirtIO Block Device"                     },
                {aHidd_StorageUnit_Serial,  (IPTR)""                                        },
                {aHidd_StorageUnit_Revision,(IPTR)"1.0"                                     },
                {TAG_DONE,                  0                                               }
            };
            attrs[VBUS_HARDWARENAME].ti_Data = (IPTR)"VirtIO Block Bus";
            data->ac_Bus = HIDD_StorageController_AddBus(vController, VirtIOBase->busClass, attrs);
            if (!data->ac_Bus) {
                OOP_DisposeObject(vController);
                return NULL;
            }

            data->ac_ResetHandler.is_Node.ln_Pri  = SD_PRI_DOS - 1;
            data->ac_ResetHandler.is_Node.ln_Name = "VirtIO Block Reset";
            data->ac_ResetHandler.is_Code         = (VOID_FUNC)VIRTIO_ResetHandler;
            data->ac_ResetHandler.is_Data         = dev;
            AddResetCallback(&data->ac_ResetHandler);
        }

        AddTail(&VirtIOBase->virtio_Controllers, &data->ac_Node);
    }

    return vController;
}

VOID VirtIO__Root__Dispose(OOP_Class *cl, OOP_Object *o, OOP_Msg msg)
{
    struct VirtIOBase *VirtIOBase = cl->UserData;
    struct virtio_Controller *data = OOP_INST_DATA(cl, o);
    device_t dev = data->ac_dev;

    /* The storage superclass cannot remove published buses. Do not free an
     * object still reachable by units, its IO task and the reset callback. */
    if (data->ac_Bus) {
        bug("[VIRTIO:Controller] published controller cannot be disposed\n");
        return;
    }
    if (dev) {
        dev->dev_Controller = NULL;
        if (data->ac_Activated && !virtio_hw_stop(dev)) {
            /* No bus was published; CPU users are gone. A non-acknowledged
             * device can still DMA, so keep its buffers/mappings and owner. */
            dev->dev_Quarantined = TRUE;
            bug("[VIRTIO:Controller] failed reset: retaining DMA and PCI ownership\n");
        } else {
            if (dev->dev_VQ) {
                virtio_free_queue(dev->dev_VQ);
                dev->dev_VQ = NULL;
            }
            virtio_hw_unmap(dev);
            if (data->ac_Activated)
                HIDD_PCIDevice_WriteConfigWord(dev->dev_Object, 4, data->ac_PCICommand);
        }
    }

    OOP_DoSuperMethod(cl, o, msg);
}

void VirtIO__Root__Get(OOP_Class *cl, OOP_Object *o, struct pRoot_Get *msg)
{
    struct VirtIOBase *VirtIOBase = (struct VirtIOBase *)cl->UserData;
    struct virtio_Controller *data = OOP_INST_DATA(cl, o);
    ULONG idx;

    HW_Switch(msg->attrID, idx) {
    case aoHW_Device:
        if (data->ac_dev)
            *msg->storage = (IPTR)data->ac_dev->dev_Object;
        return;
    default:
        break;
    }

    OOP_DoSuperMethod(cl, o, &msg->mID);
}

BOOL VirtIO__Hidd_StorageController__RemoveBus(OOP_Class *cl, OOP_Object *o,
                                               struct pHidd_StorageController_RemoveBus *Msg)
{
    return FALSE;
}

BOOL VirtIO__Hidd_StorageController__SetUpBus(OOP_Class *cl, OOP_Object *o,
                                              struct pHidd_StorageController_SetUpBus *Msg)
{
    struct VirtIOBase *VirtIOBase = cl->UserData;
    return Hidd_VirtIOBus_Start(Msg->busObject, VirtIOBase);
}

void VirtIO__Hidd_StorageController__CleanUpBus(OOP_Class *cl, OOP_Object *o,
                                                struct pHidd_StorageController_CleanUpBus *msg)
{
}