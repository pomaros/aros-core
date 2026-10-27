/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <exec/errors.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

static void serial(const char *text) { while (*text) RawPutChar(*text++); }
static int fail(unsigned stage, LONG error)
{
    char text[100];
    snprintf(text, sizeof(text), "[HOSTDISK-BARRIER] FAIL stage=%u error=%ld\n", stage, (long)error);
    serial(text); return 20;
}
int main(int argc, char **argv)
{
    struct MsgPort *port = NULL;
    struct IOExtTD *write_request = NULL, *barrier = NULL;
    struct NSDeviceQueryResult query = {0};
    struct Device *device;
    UBYTE bytes[4096];
    LONG error = 0;
    int opened = 0, result = 20, legacy, known = 0, pending, overtook;
    unsigned index;
    serial("[HOSTDISK-BARRIER] BEGIN\n");
    if (argc != 3 ||
        strlen(argv[1]) < 12 || strcmp(argv[1] + strlen(argv[1]) - 12, "/Barrier.img"))
        return fail(1, 0);
    legacy = !strcmp(argv[2], "reject-old-device");
    if (!legacy && strcmp(argv[2], "write-disposable-image")) return fail(2, 0);
    port = CreateMsgPort();
    if (!port) return fail(3, 0);
    write_request = (struct IOExtTD *)CreateIORequest(port, sizeof(*write_request));
    barrier = (struct IOExtTD *)CreateIORequest(port, sizeof(*barrier));
    if (!write_request || !barrier) { result = fail(4, 0); goto out; }
    error = OpenDevice("hostdisk.device", (IPTR)argv[1], (struct IORequest *)write_request, 0);
    if (error) { result = fail(5, error); goto out; }
    opened = 1;
    device = write_request->iotd_Req.io_Device;
    if (device->dd_Library.lib_Version < 1 ||
        (device->dd_Library.lib_Version == 1 && device->dd_Library.lib_Revision < 1)) {
        if (!legacy) { result = fail(6, 0); goto out; }
        serial("[HOSTDISK-BARRIER] PASS legacy-version-rejected no-write=1\n");
        result = 0; goto out;
    }
    if (legacy) { result = fail(7, 0); goto out; }
    write_request->iotd_Req.io_Command = NSCMD_DEVICEQUERY;
    write_request->iotd_Req.io_Data = &query;
    write_request->iotd_Req.io_Length = sizeof(query);
    if (DoIO((struct IORequest *)write_request) || write_request->iotd_Req.io_Error ||
        query.DevQueryFormat || query.SizeAvailable < sizeof(query) ||
        write_request->iotd_Req.io_Actual < sizeof(query) || !query.SupportedCommands) {
        result = fail(8, write_request->iotd_Req.io_Error); goto out;
    }
    for (index = 0; index < 256 && query.SupportedCommands[index]; ++index)
        if (query.SupportedCommands[index] == CMD_UPDATE) known = 1;
    if (!known) { result = fail(9, 0); goto out; }
    serial("[HOSTDISK-BARRIER] PASS version=1.1-or-newer nsd-update=1\n");
    for (index = 0; index < sizeof(bytes); ++index) bytes[index] = (UBYTE)(index * 17 + 3);
    barrier->iotd_Req.io_Device = device;
    barrier->iotd_Req.io_Unit = write_request->iotd_Req.io_Unit;
    barrier->iotd_Req.io_Command = CMD_UPDATE;
    barrier->iotd_Req.io_Actual = 0x7654;
    write_request->iotd_Req.io_Command = CMD_WRITE;
    write_request->iotd_Req.io_Data = bytes;
    write_request->iotd_Req.io_Length = sizeof(bytes);
    write_request->iotd_Req.io_Offset = 0;
    write_request->iotd_Req.io_Flags = 0;
    Forbid();
    SendIO((struct IORequest *)write_request);
    SendIO((struct IORequest *)barrier);
    pending = CheckIO((struct IORequest *)write_request) == NULL;
    overtook = pending && CheckIO((struct IORequest *)barrier) != NULL;
    Permit();
    WaitIO((struct IORequest *)write_request);
    WaitIO((struct IORequest *)barrier);
    if (!pending || overtook || write_request->iotd_Req.io_Error ||
        barrier->iotd_Req.io_Error || write_request->iotd_Req.io_Actual != sizeof(bytes) ||
        barrier->iotd_Req.io_Actual != 0) {
        result = fail(10, barrier->iotd_Req.io_Error); goto out;
    }
    serial("[HOSTDISK-BARRIER] PASS write-pending=1 update-overtook=0 bytes=4096 actual=0\n");
    result = 0;
out:
    if (opened) CloseDevice((struct IORequest *)write_request);
    if (barrier) DeleteIORequest((struct IORequest *)barrier);
    if (write_request) DeleteIORequest((struct IORequest *)write_request);
    DeleteMsgPort(port);
    if (!result) serial("[HOSTDISK-BARRIER] END\n");
    return result;
}
