/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AFS_PROBE_DEVICE_H
#define AFS_PROBE_DEVICE_H
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <proto/dos.h>
#include <string.h>

static int afsprobe_bound_device(const struct DevProc *device, IPTR unit, CONST_STRPTR name)
{
    const struct FileSysStartupMsg *startup;
    struct DosList *list,*node;
    size_t length;
    int okay=0;
    if(device==NULL || device->dvp_DevNode==NULL || device->dvp_Port==NULL
       || (device->dvp_DevNode->dol_Type!=DLT_DEVICE && device->dvp_DevNode->dol_Type!=DLT_VOLUME)
       || device->dvp_Lock!=BNULL || (device->dvp_Flags&DVPF_ASSIGN)) return 0;
    /* A mounted label can equal the device name: GetDeviceProc searches
     * all lists and can return the volume, whose union has no startup.
     * Bind the resolved port to the explicit named device under its list lock. */
    list=LockDosList(LDF_DEVICES|LDF_READ);
    node=FindDosEntry(list,name,LDF_DEVICES);
    if(node==NULL || node->dol_Type!=DLT_DEVICE || node->dol_Task!=device->dvp_Port
       || node->dol_misc.dol_handler.dol_Startup==BNULL) goto done;
    startup=(const struct FileSysStartupMsg *)BADDR(node->dol_misc.dol_handler.dol_Startup);
    if(startup==NULL || startup->fssm_Unit!=unit || startup->fssm_Device==BNULL)goto done;
#ifdef AROS_FAST_BSTR
    length=strlen((const char *)AROS_BSTR_ADDR(startup->fssm_Device));
#else
    length=AROS_BSTR_strlen(startup->fssm_Device);
#endif
    /* Classic Mount includes the final NUL in its stored BSTR length. */
    if(length==12 && AROS_BSTR_ADDR(startup->fssm_Device)[11]==0)length=11;
    okay=length==11 && !memcmp(AROS_BSTR_ADDR(startup->fssm_Device),"fdsk.device",11);
done:
    UnLockDosList(LDF_DEVICES|LDF_READ);
    return okay;
}

#endif
