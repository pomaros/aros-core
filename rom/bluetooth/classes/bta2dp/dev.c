/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: bta2dp.device - PCM in, for the stream bta2dp.class runs. A write
          that does not fit the stream's ring waits in nsd_Writes and is
          completed by the unit task's pump as the ring drains, so the
          writer is paced by the headset. Everything shared with the unit
          task is taken under nh_Lock.
*/
#include "bta2dp.h"

#define DEVNAME "bta2dp.device"

AROS_UFP3(struct BTA2DPDevBase *, devInit,
          AROS_UFPA(struct BTA2DPDevBase *, base, D0),
          AROS_UFPA(BPTR, seglist, A0),
          AROS_UFPA(struct ExecBase *, SysBase, A6));

/* /// "queue" */
/* Moves what fits of one write into the ring; TRUE once all of it is in. */
static BOOL feed(struct BTA2DPUnit *nau, struct IOStdReq *io)
{
    struct A2DPStream *st = &nau->nau_Stream;
    ULONG bytes = st->channels * sizeof(int16_t);
    ULONG frames = io->io_Length / bytes, done = io->io_Actual / bytes;
    if(done < frames) {
        done += A2DPStreamWrite(st, (const int16_t *) io->io_Data + done * st->channels, frames - done);
    }
    io->io_Actual = done * bytes;
    return done == frames;
}

void bta2dp_DeviceRetryWrites(struct BTA2DPBase *nh)
{
    struct BTA2DPDevBase *base = nh->nh_DevBase;
    struct IOStdReq *io;
    ObtainSemaphore(&nh->nh_Lock);
    while((io = (struct IOStdReq *) base->nsd_Writes.mlh_Head)->io_Message.mn_Node.ln_Succ) {
        if(!nh->nh_Unit || !nh->nh_Unit->nau_Streaming || !feed(nh->nh_Unit, io)) {
            break;
        }
        Remove((struct Node *) io);
        io->io_Error = 0;
        ReplyMsg(&io->io_Message);
    }
    ReleaseSemaphore(&nh->nh_Lock);
}

void bta2dp_DeviceFailWrites(struct BTA2DPBase *nh, BYTE error)
{
    struct BTA2DPDevBase *base = nh->nh_DevBase;
    struct IOStdReq *io;
    ObtainSemaphore(&nh->nh_Lock);
    while((io = (struct IOStdReq *) RemHead((struct List *) &base->nsd_Writes))) {
        io->io_Error = error;
        ReplyMsg(&io->io_Message);
    }
    ReleaseSemaphore(&nh->nh_Lock);
}
/* \\\ */

/* /// "entry points" */
AROS_UFH3(struct BTA2DPDevBase *, devInit,
          AROS_UFHA(struct BTA2DPDevBase *, base, D0),
          AROS_UFHA(BPTR, seglist, A0),
          AROS_UFHA(struct ExecBase *, SysBase, A6))
{
    AROS_USERFUNC_INIT
    base->nsd_Library.lib_Node.ln_Type = NT_DEVICE;
    base->nsd_Library.lib_Node.ln_Name = DEVNAME;
    base->nsd_Library.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->nsd_Library.lib_Version = VERSION_NUMBER;
    base->nsd_Library.lib_Revision = REVISION_NUMBER;
    base->nsd_Library.lib_IdString = VERSION_STRING;
    base->nsd_SegList = seglist;
    NEWLIST((struct List *) &base->nsd_Writes);
    return base;
    AROS_USERFUNC_EXIT
}

AROS_LH3(struct BTA2DPDevBase *, devOpen,
         AROS_LHA(struct IOStdReq *, io, A1),
         AROS_LHA(ULONG, unit, D0),
         AROS_LHA(ULONG, flags, D1),
         struct BTA2DPDevBase *, base, 1, dev)
{
    AROS_LIBFUNC_INIT
    struct BTA2DPBase *nh = base->nsd_ClsBase;
    (void) flags;
    io->io_Unit = NULL;
    if(io->io_Message.mn_Length && io->io_Message.mn_Length < sizeof(struct IOStdReq)) {
        io->io_Error = IOERR_BADLENGTH;
    } else if(unit) {
        io->io_Error = IOERR_OPENFAIL;
    } else {
        ObtainSemaphore(&nh->nh_Lock);
        if(nh->nh_Flags & BTA2DPF_CLOSING) {
            io->io_Error = IOERR_OPENFAIL;
        } else {
            base->nsd_Library.lib_OpenCnt++;
            base->nsd_Library.lib_Flags &= ~LIBF_DELEXP;
            base->nsd_Unit.unit_OpenCnt++;
            io->io_Unit = &base->nsd_Unit;
            io->io_Error = 0;
            io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
        }
        ReleaseSemaphore(&nh->nh_Lock);
        if(io->io_Unit) {
            return base;
        }
    }
    io->io_Unit = (APTR) -1;
    io->io_Device = (APTR) -1;
    return NULL;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(BPTR, devClose,
         AROS_LHA(struct IOStdReq *, io, A1),
         struct BTA2DPDevBase *, base, 2, dev)
{
    AROS_LIBFUNC_INIT
    struct BTA2DPBase *nh = base->nsd_ClsBase;
    ObtainSemaphore(&nh->nh_Lock);
    base->nsd_Unit.unit_OpenCnt--;
    base->nsd_Library.lib_OpenCnt--;
    ReleaseSemaphore(&nh->nh_Lock);
    io->io_Unit = (APTR) -1;
    io->io_Device = (APTR) -1;
    /* the class removes the device when it is expunged itself */
    return BNULL;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(BPTR, devExpunge,
         AROS_LHA(struct BTA2DPDevBase *, extralh, D0),
         struct BTA2DPDevBase *, base, 3, dev)
{
    AROS_LIBFUNC_INIT
    (void) extralh;
    if(base->nsd_Library.lib_OpenCnt) {
        base->nsd_Library.lib_Flags |= LIBF_DELEXP;
        return BNULL;
    }
    /* RemDevice() only calls this vector: unlink here, then free. */
    Remove(&base->nsd_Library.lib_Node);
    FreeMem((char *) base - base->nsd_Library.lib_NegSize,
            base->nsd_Library.lib_NegSize + base->nsd_Library.lib_PosSize);
    return BNULL;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(APTR, devReserved,
         struct BTA2DPDevBase *, base, 4, dev)
{
    AROS_LIBFUNC_INIT
    (void) base;
    return NULL;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, devBeginIO,
         AROS_LHA(struct IOStdReq *, io, A1),
         struct BTA2DPDevBase *, base, 5, dev)
{
    AROS_LIBFUNC_INIT
    struct BTA2DPBase *nh = base->nsd_ClsBase;
    struct BTA2DPUnit *nau;
    io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    io->io_Error = 0;
    ObtainSemaphore(&nh->nh_Lock);
    nau = nh->nh_Unit;
    switch(io->io_Command) {
    case CMD_WRITE:
        io->io_Actual = 0;
        if(!nau || !nau->nau_Configured) {
            io->io_Error = BTA2DPERR_NOSTREAM;
        } else if(io->io_Length % (nau->nau_Stream.channels * sizeof(int16_t))) {
            io->io_Error = IOERR_BADLENGTH;
        } else if(!nau->nau_Streaming || base->nsd_Writes.mlh_Head->mln_Succ || !feed(nau, io)) {
            /* earlier writes go first; this one completes from the pump,
               after the unit task resumes a suspended stream */
            io->io_Flags &= ~IOF_QUICK;
            AddTail((struct List *) &base->nsd_Writes, (struct Node *) io);
            if(!nau->nau_Streaming && !nau->nau_ResumeWanted) {
                nau->nau_ResumeWanted = TRUE;
                Signal(nau->nau_Task, SIGBREAKF_CTRL_E);
            }
            ReleaseSemaphore(&nh->nh_Lock);
            return;
        }
        break;
    case BTA2DPCMD_QUERY:
        if(nau && nau->nau_Configured) {
            io->io_Actual = nau->nau_Source.config.frequency;
            io->io_Offset = nau->nau_Stream.channels;
            io->io_Length = nau->nau_Stream.count;
        } else {
            io->io_Actual = 0;
            io->io_Offset = 0;
            io->io_Length = 0;
        }
        break;
    case BTA2DPCMD_SETVOLUME:
        nh->nh_Volume = io->io_Offset > 100 ? 100 : io->io_Offset;
        if(nau && nau->nau_Task) {
            /* the unit task saves it and tells the headset */
            Signal(nau->nau_Task, SIGBREAKF_CTRL_D);
        }
        break;
    case BTA2DPCMD_GETVOLUME:
        io->io_Offset = nh->nh_Volume;
        io->io_Actual = nau && nau->nau_Avrcp.absolute_volume;
        break;
        case CMD_RESET:
    case CMD_FLUSH:
        ReleaseSemaphore(&nh->nh_Lock);
        bta2dp_DeviceFailWrites(nh, IOERR_ABORTED);
        ObtainSemaphore(&nh->nh_Lock);
        break;
    default:
        io->io_Error = IOERR_NOCMD;
        break;
    }
    ReleaseSemaphore(&nh->nh_Lock);
    if(!(io->io_Flags & IOF_QUICK)) {
        ReplyMsg(&io->io_Message);
    }
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, devAbortIO,
         AROS_LHA(struct IOStdReq *, io, A1),
         struct BTA2DPDevBase *, base, 6, dev)
{
    AROS_LIBFUNC_INIT
    struct BTA2DPBase *nh = base->nsd_ClsBase;
    struct MinNode *n;
    LONG result = -1;
    ObtainSemaphore(&nh->nh_Lock);
    for(n = base->nsd_Writes.mlh_Head; n->mln_Succ; n = n->mln_Succ) {
        if(n == (struct MinNode *) io) {
            Remove((struct Node *) io);
            io->io_Error = IOERR_ABORTED;
            ReplyMsg(&io->io_Message);
            result = 0;
            break;
        }
    }
    ReleaseSemaphore(&nh->nh_Lock);
    return result;
    AROS_LIBFUNC_EXIT
}

static const APTR DevFuncTable[] = {
    &AROS_SLIB_ENTRY(devOpen, dev, 1),
    &AROS_SLIB_ENTRY(devClose, dev, 2),
    &AROS_SLIB_ENTRY(devExpunge, dev, 3),
    &AROS_SLIB_ENTRY(devReserved, dev, 4),
    &AROS_SLIB_ENTRY(devBeginIO, dev, 5),
    &AROS_SLIB_ENTRY(devAbortIO, dev, 6),
    (APTR) -1,
};
/* \\\ */

struct BTA2DPDevBase *bta2dp_CreateDevice(struct BTA2DPBase *nh)
{
    struct BTA2DPDevBase *base;
    if((base = (struct BTA2DPDevBase *) MakeLibrary((APTR) DevFuncTable, NULL, (APTR) devInit,
                                                    sizeof(struct BTA2DPDevBase), BNULL))) {
        base->nsd_ClsBase = nh;
        AddDevice((struct Device *) base);
    }
    return base;
}

/* The class checked that nobody has it open and set BTA2DPF_CLOSING. */
void bta2dp_DeleteDevice(struct BTA2DPBase *nh)
{
    if(nh->nh_DevBase) {
        RemDevice((struct Device *) nh->nh_DevBase);
        nh->nh_DevBase = NULL;
    }
}
