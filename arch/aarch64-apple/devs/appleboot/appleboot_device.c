/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Read-only block device for the loader-retained Apple boot image.
 */

#include <aros/apple/startup.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>

#include <devices/newstyle.h>
#include <devices/trackdisk.h>
#include <dos/filehandler.h>
#include <dos/filesystemids.h>
#include <exec/errors.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/expansion.h>
#define KernelBase base->kernel_base
#include <proto/kernel.h>

#include <stdint.h>

#include "appleboot_device.h"
#include LC_LIBDEFS_FILE

#define APPLEBOOT_SECTOR_SIZE UINT32_C(512)
#define APPLEBOOT_BOOT_PRIORITY 10

static const UWORD supported_commands[] = {
    CMD_CLEAR,
    CMD_FLUSH,
    CMD_READ,
    CMD_UPDATE,
    TD_ADDCHANGEINT,
    TD_CHANGENUM,
    TD_CHANGESTATE,
    TD_GETDRIVETYPE,
    TD_GETGEOMETRY,
    TD_GETNUMTRACKS,
    TD_MOTOR,
    TD_PROTSTATUS,
    TD_REMCHANGEINT,
    NSCMD_DEVICEQUERY,
    0
};

static UWORD load_le16(const UBYTE *bytes)
{
    return (UWORD)bytes[0] | (UWORD)((UWORD)bytes[1] << 8);
}

static ULONG load_le32(const UBYTE *bytes)
{
    return (ULONG)bytes[0] | ((ULONG)bytes[1] << 8) |
        ((ULONG)bytes[2] << 16) | ((ULONG)bytes[3] << 24);
}

static int power_of_two(UBYTE value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

static int validate_fat_image(struct AppleBootBase *base)
{
    const UBYTE *boot = (const UBYTE *)base->image_start;
    uint64_t bytes;
    ULONG total_sectors;
    UWORD total16;

    if ((base->image_start & (APPLEBOOT_SECTOR_SIZE - 1U)) != 0 ||
        base->image_size < APPLEBOOT_SECTOR_SIZE ||
        base->image_size > UINT32_MAX ||
        base->image_start > UINTPTR_MAX - (uintptr_t)base->image_size ||
        load_le16(&boot[11]) != APPLEBOOT_SECTOR_SIZE ||
        !power_of_two(boot[13]) || load_le16(&boot[14]) == 0 ||
        boot[16] == 0 || load_le16(&boot[17]) == 0 ||
        boot[21] == 0 || load_le16(&boot[22]) == 0 ||
        load_le16(&boot[24]) == 0 || load_le16(&boot[26]) == 0 ||
        boot[510] != 0x55 || boot[511] != 0xaa)
        return 0;

    total16 = load_le16(&boot[19]);
    total_sectors = total16 != 0 ? total16 : load_le32(&boot[32]);
    bytes = (uint64_t)total_sectors * APPLEBOOT_SECTOR_SIZE;
    if (total_sectors == 0 || bytes > base->image_size)
        return 0;
    base->sector_count = total_sectors;
    base->sectors_per_track = load_le16(&boot[24]);
    base->heads = load_le16(&boot[26]);
    return 1;
}

static int add_boot_node(struct AppleBootBase *base)
{
    struct Library *ExpansionBase;
    struct DeviceNode *device_node = NULL;
    IPTR parameters[4 + DE_BOOTBLOCKS + 1] = {0};
    uint64_t cylinder_sectors;
    uint64_t cylinder_count;
    int result = 0;

    cylinder_sectors = (uint64_t)base->heads * base->sectors_per_track;
    if (cylinder_sectors == 0 ||
        base->sector_count % cylinder_sectors != 0)
        return 0;
    cylinder_count = base->sector_count / cylinder_sectors;
    if (cylinder_count == 0 || cylinder_count - 1U > UINT32_MAX)
        return 0;

    parameters[0] = (IPTR)"SYS0";
    parameters[1] = (IPTR)"appleboot.device";
    parameters[2] = 0;
    parameters[3] = 0;
    parameters[4 + DE_TABLESIZE] = DE_BOOTBLOCKS;
    parameters[4 + DE_SIZEBLOCK] = APPLEBOOT_SECTOR_SIZE / sizeof(ULONG);
    parameters[4 + DE_NUMHEADS] = base->heads;
    parameters[4 + DE_SECSPERBLOCK] = 1;
    parameters[4 + DE_BLKSPERTRACK] = base->sectors_per_track;
    parameters[4 + DE_RESERVEDBLKS] = 0;
    parameters[4 + DE_LOWCYL] = 0;
    parameters[4 + DE_HIGHCYL] = (IPTR)(cylinder_count - 1U);
    parameters[4 + DE_NUMBUFFERS] = 16;
    parameters[4 + DE_BUFMEMTYPE] = MEMF_PUBLIC;
    parameters[4 + DE_MAXTRANSFER] = UINT32_C(0x7fffffff);
    parameters[4 + DE_MASK] = UINT32_C(0xfffffffe);
    parameters[4 + DE_BOOTPRI] = APPLEBOOT_BOOT_PRIORITY;
    parameters[4 + DE_DOSTYPE] = ID_FAT12_DISK;
    parameters[4 + DE_BOOTBLOCKS] = 0;

    ExpansionBase = OpenLibrary("expansion.library", 0);
    if (ExpansionBase != NULL) {
        device_node = MakeDosNode(parameters);
        if (device_node != NULL && AddBootNode(APPLEBOOT_BOOT_PRIORITY, 0,
                device_node, NULL))
            result = 1;
        else if (device_node != NULL)
            FreeVec(device_node);
        CloseLibrary(ExpansionBase);
    }
    return result;
}

static int AppleBoot_Init(LIBBASETYPEPTR base)
{
    intptr_t image_start;
    intptr_t image_size;

    NEWLIST(&base->unit.change_requests);
    base->kernel_base = OpenResource("kernel.resource");
    if (base->kernel_base == NULL)
        return FALSE;
    image_start = KrnGetSystemAttr(AROS_APPLE_KATTR_BOOT_IMAGE_START);
    image_size = KrnGetSystemAttr(AROS_APPLE_KATTR_BOOT_IMAGE_SIZE);
    if (image_start <= 0 || image_size <= 0)
        return FALSE;
    base->image_start = (uintptr_t)image_start;
    base->image_size = (uint64_t)image_size;
    if (!validate_fat_image(base) || !add_boot_node(base))
        return FALSE;
    base->ready = TRUE;
    return TRUE;
}

static int AppleBoot_Open(
    LIBBASETYPEPTR base, struct IOExtTD *request, ULONG unit, ULONG flags)
{
    (void)flags;
    if (!base->ready || unit != 0 ||
        request->iotd_Req.io_Message.mn_Length < sizeof(*request)) {
        request->iotd_Req.io_Error = IOERR_OPENFAIL;
        return FALSE;
    }
    request->iotd_Req.io_Unit = &base->unit.unit;
    request->iotd_Req.io_Error = 0;
    return TRUE;
}

static int AppleBoot_Close(
    LIBBASETYPEPTR base, struct IOExtTD *request)
{
    (void)base;
    request->iotd_Req.io_Unit = NULL;
    return TRUE;
}

ADD2INITLIB(AppleBoot_Init, 0)
ADD2OPENDEV(AppleBoot_Open, 0)
ADD2CLOSEDEV(AppleBoot_Close, 0)

static void finish_request(struct IORequest *request)
{
    request->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    if ((request->io_Flags & IOF_QUICK) == 0)
        ReplyMsg(&request->io_Message);
}

AROS_LH1(void, AppleBoot_BeginIO,
    AROS_LHA(struct IOExtTD *, request, A1),
    struct AppleBootBase *, base, 5, AppleBoot)
{
    struct IOStdReq *io = &request->iotd_Req;
    int reply = 1;

    AROS_LIBFUNC_INIT
    io->io_Error = 0;
    io->io_Actual = 0;
    switch (io->io_Command) {
    case CMD_READ:
        if (io->io_Length != 0 && io->io_Data == NULL) {
            io->io_Error = IOERR_BADADDRESS;
        } else if ((uint64_t)io->io_Offset > base->image_size ||
                (uint64_t)io->io_Length >
                    base->image_size - (uint64_t)io->io_Offset) {
            io->io_Error = IOERR_BADADDRESS;
        } else {
            CopyMem((const UBYTE *)base->image_start + io->io_Offset,
                io->io_Data, io->io_Length);
            io->io_Actual = io->io_Length;
        }
        break;
    case CMD_WRITE:
    case TD_FORMAT:
        io->io_Error = TDERR_WriteProt;
        break;
    case TD_PROTSTATUS:
        io->io_Actual = 1;
        break;
    case TD_CHANGENUM:
    case TD_CHANGESTATE:
        io->io_Actual = 0;
        break;
    case TD_GETDRIVETYPE:
        io->io_Actual = DRIVE_NEWSTYLE;
        break;
    case TD_GETNUMTRACKS:
        io->io_Actual = base->sector_count / base->sectors_per_track;
        break;
    case TD_GETGEOMETRY:
        if (io->io_Data == NULL || io->io_Length < sizeof(struct DriveGeometry)) {
            io->io_Error = IOERR_BADLENGTH;
        } else {
            struct DriveGeometry *geometry = io->io_Data;

            geometry->dg_SectorSize = APPLEBOOT_SECTOR_SIZE;
            geometry->dg_TotalSectors = base->sector_count;
            geometry->dg_Cylinders = base->sector_count /
                ((ULONG)base->heads * base->sectors_per_track);
            geometry->dg_CylSectors =
                (ULONG)base->heads * base->sectors_per_track;
            geometry->dg_Heads = base->heads;
            geometry->dg_TrackSectors = base->sectors_per_track;
            geometry->dg_BufMemType = MEMF_PUBLIC;
            geometry->dg_DeviceType = DG_DIRECT_ACCESS;
            geometry->dg_Flags = 0;
            io->io_Actual = sizeof(*geometry);
        }
        break;
    case NSCMD_DEVICEQUERY:
        if (io->io_Data == NULL ||
            io->io_Length < sizeof(struct NSDeviceQueryResult)) {
            io->io_Error = IOERR_BADLENGTH;
        } else {
            struct NSDeviceQueryResult *query = io->io_Data;

            query->DevQueryFormat = 0;
            query->SizeAvailable = sizeof(*query);
            query->DeviceType = NSDEVTYPE_TRACKDISK;
            query->DeviceSubType = 0;
            query->SupportedCommands = (UWORD *)supported_commands;
            io->io_Actual = sizeof(*query);
        }
        break;
    case TD_ADDCHANGEINT:
        io->io_Flags &= ~IOF_QUICK;
        Forbid();
        AddTail(&base->unit.change_requests, (struct Node *)request);
        Permit();
        reply = 0;
        break;
    case TD_REMCHANGEINT:
        Forbid();
        Remove((struct Node *)request);
        Permit();
        break;
    case CMD_CLEAR:
    case CMD_FLUSH:
    case CMD_UPDATE:
    case TD_MOTOR:
        break;
    default:
        io->io_Error = IOERR_NOCMD;
        break;
    }
    if (reply)
        finish_request((struct IORequest *)io);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, AppleBoot_AbortIO,
    AROS_LHA(struct IORequest *, request, A1),
    struct AppleBootBase *, base, 6, AppleBoot)
{
    AROS_LIBFUNC_INIT
    (void)base;
    if (request->io_Command != TD_ADDCHANGEINT)
        return IOERR_NOCMD;
    Forbid();
    Remove((struct Node *)request);
    Permit();
    request->io_Error = IOERR_ABORTED;
    finish_request(request);
    return 0;
    AROS_LIBFUNC_EXIT
}
