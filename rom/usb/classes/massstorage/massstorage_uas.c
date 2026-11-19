/*
 *----------------------------------------------------------------------------
 *                         massstorage class for poseidon
 *----------------------------------------------------------------------------
 *                   By Chris Hodges <chrisly@platon42.de>
 */

#include "debug.h"

#include "massstorage.class.h"

extern const STRPTR GM_UNIQUENAME(libname);

#undef  ps
#define ps ncm->ncm_Base

static void nUasFillLun(UBYTE *lun, UWORD lunnum)
{
    memset(lun, 0, 8);
    lun[0] = lunnum > 255 ? 0x40 | (lunnum >> 8) : 0;
    lun[1] = (UBYTE)lunnum;
}

static LONG nUasDoDataTransfer(struct NepClassMS *ncm, UBYTE *data, ULONG data_len,
                               BOOL read, ULONG *actual)
{
    struct PsdPipe *pp;
    LONG ioerr;
    LONG actual_len = 0;

    if(ncm->ncm_UasStreamId)
    {
        struct PsdPipeStream *stream = read ? ncm->ncm_EPInStream : ncm->ncm_EPOutStream;

        if(stream)
        {
            actual_len = read ? psdStreamRead(stream, data, data_len)
                              : psdStreamWrite(stream, data, data_len);
            ioerr = psdGetStreamError(stream);
            if(actual)
            {
                *actual = (ULONG) actual_len;
            }
            if(read && ioerr == UHIOERR_RUNTPACKET)
            {
                ioerr = 0;
            }
            return ioerr;
        }
    }

    pp = read ? ncm->ncm_EPInPipe : ncm->ncm_EPOutPipe;
    ioerr = psdDoPipe(pp, data, data_len);
    if(actual)
    {
        *actual = psdGetPipeActual(pp);
    }
    if(read && ioerr == UHIOERR_RUNTPACKET)
    {
        ioerr = 0;
    }
    return ioerr;
}

static LONG nUasDoCommand(struct NepClassMS *ncm, const UBYTE *cdb, UWORD cdb_len,
                           UBYTE *data, ULONG data_len, BOOL read,
                           ULONG *actual, UBYTE *status, UBYTE *iu_id,
                           UBYTE *sense_data, ULONG sense_len, UWORD *sense_actual)
{
    struct UasCommandIU cmdiu;
    UBYTE statusbuf[64];
    struct UasStatusIU *completion = (struct UasStatusIU *)statusbuf;
    ULONG actual_len;
    UWORD tag;
    LONG ioerr;
    BOOL data_pending = data_len != 0;

    if(!cdb || !cdb_len || cdb_len > sizeof(cmdiu.iu_Cdb) || (data_pending && !data))
        return UHIOERR_BADPARAMS;
    if(actual)
    {
        *actual = 0;
    }
    if(status)
    {
        *status = SCSI_GOOD;
    }
    if(iu_id)
    {
        *iu_id = 0;
    }
    if(sense_actual)
    {
        *sense_actual = 0;
    }

    memset(&cmdiu, 0, sizeof(cmdiu));
    cmdiu.iu_Id = UAS_IU_ID_COMMAND;
    cmdiu.iu_TaskAttr = 0;
    if(ncm->ncm_UasStreamId)
    {
        tag = ncm->ncm_UasStreamId;
    } else {
        tag = (UWORD)++ncm->ncm_TagCount;
        if(!tag)
            tag = (UWORD)++ncm->ncm_TagCount;
    }
    cmdiu.iu_Tag = AROS_WORD2BE(tag);
    nUasFillLun(cmdiu.iu_Lun, ncm->ncm_UnitLUN);
    CopyMem(cdb, cmdiu.iu_Cdb, cdb_len);

    ioerr = psdDoPipe(ncm->ncm_EPCmdPipe, &cmdiu, sizeof(cmdiu));
    if(ioerr)
    {
        return ioerr;
    }

    /* USB 2 UAS signals data readiness on the status pipe. A command may
     * also finish with CHECK CONDITION without ever entering its data phase. */
    if(data_pending && ncm->ncm_UasStreamId)
    {
        ioerr = nUasDoDataTransfer(ncm, data, data_len, read, actual);
        if(ioerr)
            return ioerr;
        data_pending = FALSE;
    }
    for(;;)
    {
        ioerr = psdDoPipe(ncm->ncm_EPStatusPipe, statusbuf, sizeof(statusbuf));
        if(ioerr && ioerr != UHIOERR_RUNTPACKET)
            return ioerr;
        actual_len = psdGetPipeActual(ncm->ncm_EPStatusPipe);
        if(actual_len < 4 || completion->iu_Tag != cmdiu.iu_Tag)
            return UHIOERR_HOSTERROR;
        if(completion->iu_Id == UAS_IU_ID_STATUS)
            break;
        if(!data_pending || completion->iu_Id !=
           (read ? UAS_IU_ID_READ_READY : UAS_IU_ID_WRITE_READY))
            return UHIOERR_HOSTERROR;
        ioerr = nUasDoDataTransfer(ncm, data, data_len, read, actual);
        if(ioerr)
            return ioerr;
        data_pending = FALSE;
    }
    if(actual_len < sizeof(*completion))
        return UHIOERR_HOSTERROR;
    if(iu_id)
        *iu_id = completion->iu_Id;
    if(status)
        *status = completion->iu_Status;
    if(sense_data && sense_actual)
    {
        ULONG available = actual_len - sizeof(*completion);
        ULONG length = AROS_BE2WORD(completion->iu_SenseLength);
        if(length > available)
            length = available;
        if(length > sense_len)
            length = sense_len;
        CopyMem(statusbuf + sizeof(*completion), sense_data, length);
        *sense_actual = length;
    }
    return 0;
}

/* /// "nScsiDirectUAS()" */
LONG nScsiDirectUAS(struct NepClassMS *ncm, struct SCSICmd *scsicmd)
{
    LONG ioerr;
    LONG rioerr = 0;
    ULONG datalen;
    UBYTE status = SCSI_GOOD;
    UBYTE iu_id = 0;

    datalen = scsicmd->scsi_Length;
    scsicmd->scsi_Status = SCSI_GOOD;
    scsicmd->scsi_Actual = 0;
    scsicmd->scsi_CmdActual = 0;
    scsicmd->scsi_SenseActual = 0;

    nLockXFer(ncm);
    do
    {
        if(ncm->ncm_DenyRequests)
        {
            rioerr = HFERR_Phase;
            break;
        }

        ioerr = nUasDoCommand(ncm, scsicmd->scsi_Command, scsicmd->scsi_CmdLength,
                              (UBYTE *) scsicmd->scsi_Data, datalen,
                              (scsicmd->scsi_Flags & SCSIF_READ) != 0,
                              &scsicmd->scsi_Actual, &status, &iu_id,
                              (scsicmd->scsi_Flags & SCSIF_AUTOSENSE) ? scsicmd->scsi_SenseData : NULL,
                              scsicmd->scsi_SenseLength, &scsicmd->scsi_SenseActual);
        if(ioerr)
        {
            psdAddErrorMsg(RETURN_ERROR, (STRPTR) GM_UNIQUENAME(libname),
                           "UAS command transfer failed: %s (%ld)",
                           psdNumToStr(NTS_IOERR, ioerr, "unknown"), ioerr);
            scsicmd->scsi_Status = SCSI_CHECK_CONDITION;
            rioerr = HFERR_Phase;
            break;
        }

        scsicmd->scsi_CmdActual = scsicmd->scsi_CmdLength;
        scsicmd->scsi_Status = status;
        if(status)
        {
            rioerr = HFERR_BadStatus;
            if((scsicmd->scsi_Flags & SCSIF_AUTOSENSE) && (!scsicmd->scsi_SenseActual))
            {
                UBYTE sensecmd[6];
                UBYTE sense_status = SCSI_GOOD;
                ULONG sense_actual = 0;

                sensecmd[0] = SCSI_REQUEST_SENSE;
                sensecmd[1] = 0;
                sensecmd[2] = 0;
                sensecmd[3] = 0;
                sensecmd[4] = (UBYTE) scsicmd->scsi_SenseLength;
                sensecmd[5] = 0;

                ioerr = nUasDoCommand(ncm, sensecmd, 6,
                                      scsicmd->scsi_SenseData, scsicmd->scsi_SenseLength,
                                      TRUE, &sense_actual, &sense_status, NULL,
                                      NULL, 0, NULL);
                if(!ioerr)
                {
                    scsicmd->scsi_SenseActual = (UWORD) sense_actual;
                } else {
                    psdAddErrorMsg(RETURN_WARN, (STRPTR) GM_UNIQUENAME(libname),
                                   "UAS request sense failed: %s (%ld)",
                                   psdNumToStr(NTS_IOERR, ioerr, "unknown"), ioerr);
                }
            }
        }

    } while(FALSE);
    nUnlockXFer(ncm);
    return(rioerr);
}
/* \\ */
