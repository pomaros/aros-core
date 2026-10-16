/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <proto/exec.h>
#define __NOLIBBASE__

#include <proto/kernel.h>
#include <proto/utility.h>

#include <aros/atomic.h>
#include <aros/symbolsets.h>
#include <exec/exec.h>
#include <exec/errors.h>
#include <exec/memory.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>

#include <hidd/pci.h>

#include <string.h>

#include "virtio_debug.h"
#include "virtio_intern.h"
#include "virtio_queue.h"

#include LC_LIBDEFS_FILE

/* Transfer ownership before publication: completion may run immediately. */
static BOOL virtio_blk_submit(struct IORequest *io, struct virtio_request *templ,
                              BOOL *pending)
{
    struct virtio_Unit *unit = (struct virtio_Unit *)io->io_Unit;
    UBYTE flags = io->io_Flags;
    ObtainSemaphore(&unit->au_Lock);
    Remove(&io->io_Message.mn_Node);
    ReleaseSemaphore(&unit->au_Lock);
    *pending = FALSE;
    io->io_Flags &= ~IOF_QUICK;
    if (virtio_submit_blk_request(unit->au_Bus->ab_Dev->dev_VQ, templ) < 0) {
        /* A rejected submission has not published or replied to the request. */
        io->io_Flags = flags;
        io->io_Error = IOERR_ABORTED;
        return TRUE;
    }
    /* Do not touch io: the reply recipient may already have freed it. */
    return FALSE;
}

static BOOL virtio_blk_rw(struct IORequest *io, UQUAD off64, BOOL is_write, BOOL *pending)
{
    struct IOExtTD *iotd = (struct IOExtTD *)io;
    struct virtio_Unit *unit = (struct virtio_Unit *)io->io_Unit;
    device_t dev = unit->au_Bus->ab_Dev;
    struct virtio_request templ;
    APTR data = iotd->iotd_Req.io_Data;
    ULONG len = iotd->iotd_Req.io_Length;

    IOStdReq(io)->io_Actual = 0;
    if (!dev || !dev->dev_VQ) {
        io->io_Error = IOERR_OPENFAIL;
        return TRUE;
    }

    if (is_write && dev->dev_ReadOnly) {
        io->io_Error = IOERR_NOCMD;
        return TRUE;
    }

    if (unit->au_SecShift < 9 || unit->au_SecShift >= 32) {
        io->io_Error = IOERR_BADLENGTH;
        return TRUE;
    }
    ULONG mask = (1UL << unit->au_SecShift) - 1;
    if (!data || (off64 & mask)) {
        io->io_Error = IOERR_BADADDRESS;
        return TRUE;
    }
    if (!len || (len & mask)) {
        io->io_Error = IOERR_BADLENGTH;
        return TRUE;
    }
    UQUAD first = off64 >> unit->au_SecShift;
    UQUAD count = len >> unit->au_SecShift;
    /* Subtraction avoids overflowing the end offset on large devices. */
    if (first >= unit->au_SecCnt || count > unit->au_SecCnt - first) {
        io->io_Error = IOERR_BADADDRESS;
        return TRUE;
    }

    memset(&templ, 0, sizeof(templ));
    templ.vr_IORequest  = io;
    templ.vr_DataAddr   = data;
    templ.vr_DataLength = len;
    templ.vr_IsWrite    = is_write ? 1 : 0;

    /*
     * virtio-blk header always uses 512-byte sectors regardless of the
     * device's logical block size.
     */
    templ.vr_Header.type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    templ.vr_Header.ioprio = 0;
    templ.vr_Header.sector = off64 >> 9;        /* 512-byte units */

    return virtio_blk_submit(io, &templ, pending);
}

static BOOL virtio_blk_flush(struct IORequest *io, BOOL *pending)
{
    struct virtio_Unit *unit = (struct virtio_Unit *)io->io_Unit;
    device_t dev = unit->au_Bus->ab_Dev;
    struct virtio_request templ;

    IOStdReq(io)->io_Actual = 0;
    if (!dev || !dev->dev_VQ) {
        io->io_Error = IOERR_OPENFAIL;
        return TRUE;
    }

    /* No FLUSH feature negotiated -> just succeed. */
    if (!(dev->dev_Features & ((UQUAD)1 << VIRTIO_BLK_F_FLUSH))) {
        return TRUE;
    }

    memset(&templ, 0, sizeof(templ));
    templ.vr_IORequest = io;
    templ.vr_DataAddr  = NULL;
    templ.vr_DataLength = 0;
    templ.vr_IsWrite    = 0;
    templ.vr_Header.type = VIRTIO_BLK_T_FLUSH;
    templ.vr_Header.sector = 0;

    return virtio_blk_submit(io, &templ, pending);
}

AROS_LH1(void, BeginIO,
         AROS_LHA(struct IORequest *, io, A1),
         struct VirtIOBase *, VirtIOBase, 5, virtio)
{
    AROS_LIBFUNC_INIT

    static const UWORD NSDSupported[] = {
        CMD_READ, CMD_WRITE, CMD_UPDATE, CMD_CLEAR,
        TD_CHANGESTATE, TD_SEEK, TD_FORMAT, TD_GETGEOMETRY, TD_MOTOR,
        TD_PROTSTATUS, TD_READ64, TD_WRITE64, TD_SEEK64, TD_FORMAT64,
        NSCMD_DEVICEQUERY, NSCMD_TD_READ64, NSCMD_TD_WRITE64,
        NSCMD_TD_SEEK64, NSCMD_TD_FORMAT64, 0
    };

    struct IOExtTD *iotd = (struct IOExtTD *)io;
    struct virtio_Unit *unit = (struct virtio_Unit *)io->io_Unit;
    APTR data = iotd->iotd_Req.io_Data;
    ULONG len = iotd->iotd_Req.io_Length;
    UQUAD off64;
    struct DriveGeometry *geom;
    struct NSDeviceQueryResult *nsqr;
    BOOL done = TRUE;
    BOOL pending = TRUE;

    io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    io->io_Error = 0;

    ObtainSemaphore(&unit->au_Lock);
    AddHead(&unit->au_IOs, &io->io_Message.mn_Node);
    ReleaseSemaphore(&unit->au_Lock);

    switch (io->io_Command) {
    case CMD_READ:
        off64 = iotd->iotd_Req.io_Offset;
        done = virtio_blk_rw(io, off64, FALSE, &pending);
        break;
    case TD_READ64:
    case NSCMD_TD_READ64:
        off64  = iotd->iotd_Req.io_Offset;
        off64 |= ((UQUAD)iotd->iotd_Req.io_Actual) << 32;
        done = virtio_blk_rw(io, off64, FALSE, &pending);
        break;

    case CMD_WRITE:
        off64 = iotd->iotd_Req.io_Offset;
        done = virtio_blk_rw(io, off64, TRUE, &pending);
        break;
    case TD_WRITE64:
    case NSCMD_TD_WRITE64:
        off64  = iotd->iotd_Req.io_Offset;
        off64 |= ((UQUAD)iotd->iotd_Req.io_Actual) << 32;
        done = virtio_blk_rw(io, off64, TRUE, &pending);
        break;

    case TD_FORMAT:
        off64 = iotd->iotd_Req.io_Offset;
        done = virtio_blk_rw(io, off64, TRUE, &pending);
        break;
    case TD_FORMAT64:
    case NSCMD_TD_FORMAT64:
        off64  = iotd->iotd_Req.io_Offset;
        off64 |= ((UQUAD)iotd->iotd_Req.io_Actual) << 32;
        done = virtio_blk_rw(io, off64, TRUE, &pending);
        break;

    case TD_SEEK:
    case TD_SEEK64:
    case NSCMD_TD_SEEK64:
        IOStdReq(io)->io_Actual = 0;
        done = TRUE;
        break;

    case TD_CHANGESTATE:
    case TD_PROTSTATUS:
        IOStdReq(io)->io_Actual = 0;
        done = TRUE;
        break;

    case NSCMD_DEVICEQUERY:
        if (len < sizeof(*nsqr)) goto bad_length;
        nsqr = data;
        nsqr->DevQueryFormat = 0;
        nsqr->SizeAvailable  = sizeof(struct NSDeviceQueryResult);
        nsqr->DeviceType     = NSDEVTYPE_TRACKDISK;
        nsqr->DeviceSubType  = 0;
        nsqr->SupportedCommands = (UWORD *)NSDSupported;
        IOStdReq(io)->io_Actual = sizeof(*nsqr);
        done = TRUE;
        break;

    case TD_GETDRIVETYPE:
        IOStdReq(io)->io_Actual = DRIVE_NEWSTYLE;
        done = TRUE;
        break;

    case TD_GETGEOMETRY:
        if (len < sizeof(*geom)) goto bad_length;
        geom = data;
        SetMem(geom, 0, len);
        geom->dg_Heads        = unit->nu_Heads;
        geom->dg_SectorSize   = 1 << unit->au_SecShift;
        if (unit->au_SecCnt >> 32) geom->dg_TotalSectors = 0xFFFFFFFF;
        else                       geom->dg_TotalSectors = unit->au_SecCnt;
        geom->dg_Cylinders    = unit->nu_Cyl;
        geom->dg_CylSectors   = 63 * unit->nu_Heads;
        geom->dg_TrackSectors = 63;
        geom->dg_BufMemType   = MEMF_PUBLIC;
        geom->dg_DeviceType   = DG_DIRECT_ACCESS;
        geom->dg_Flags        = 0;
        IOStdReq(io)->io_Actual = sizeof(*geom);
        done = TRUE;
        break;

    case TD_ADDCHANGEINT:
        if (io->io_Flags & IOF_QUICK) goto bad_cmd;
        done = FALSE;
        break;
    case TD_REMCHANGEINT:
        if (io->io_Flags & IOF_QUICK) goto bad_cmd;
        done = TRUE;
        break;

    case TD_MOTOR:
        IOStdReq(io)->io_Actual = 1;
        done = TRUE;
        break;

    case CMD_CLEAR:
        done = TRUE;
        break;
    case CMD_UPDATE:
        done = virtio_blk_flush(io, &pending);
        break;

    default:
bad_cmd:
        io->io_Error = IOERR_NOCMD;
        done = TRUE;
        break;
bad_length:
        io->io_Error = IOERR_BADLENGTH;
        done = TRUE;
        break;
    }

    if (done && pending) {
        ObtainSemaphore(&unit->au_Lock);
        Remove(&io->io_Message.mn_Node);
        ReleaseSemaphore(&unit->au_Lock);
    }

    if (done && !(io->io_Flags & IOF_QUICK))
        ReplyMsg(&io->io_Message);

    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, AbortIO,
         AROS_LHA(struct IORequest *, io, A1),
         struct VirtIOBase *, VirtIOBase, 6, virtio)
{
    AROS_LIBFUNC_INIT

    /* Aborting in-flight virtio-blk IOs is not supported. */
    return 0;

    AROS_LIBFUNC_EXIT
}
