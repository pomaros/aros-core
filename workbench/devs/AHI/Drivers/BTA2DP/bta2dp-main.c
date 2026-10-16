/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: bta2dp.audio - AHI driver for Bluetooth headsets. AHI mixes 16 bit
          stereo at 44.1 kHz; a slave process writes each buffer to
          bta2dp.device, whose completion paces the mixer at the headset's
          clock. Without a streaming headset the slave keeps time itself
          and the audio goes nowhere.
*/
#include <config.h>

#include <devices/ahi.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <libraries/ahi_sub.h>
#include <proto/ahi_sub.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include "library.h"
#include "DriverData.h"

#define dd ((struct BTA2DPData *) AudioCtrl->ahiac_DriverData)

#define MIXFREQ 44100   /* what bta2dp.class negotiates first */

void SlaveEntry(void);

#ifdef PROCGW
PROCGW(static, void, slaveentry, SlaveEntry);
#else
#define slaveentry SlaveEntry
#endif

ULONG
_AHIsub_AllocAudio(struct TagItem *taglist,
                   struct AHIAudioCtrlDrv *AudioCtrl,
                   struct DriverBase *AHIsubBase)
{
    struct BTA2DPAudioBase *BTA2DPAudioBase = (struct BTA2DPAudioBase *) AHIsubBase;

    AudioCtrl->ahiac_DriverData = AllocVec(sizeof(struct BTA2DPData), MEMF_CLEAR | MEMF_PUBLIC);
    if(dd == NULL) {
        return AHISF_ERROR;
    }
    dd->slavesignal = -1;
    dd->mastersignal = AllocSignal(-1);
    dd->mastertask = (struct Process *) FindTask(NULL);
    dd->ahisubbase = BTA2DPAudioBase;
    InitSemaphore(&dd->mixlock);
    if(dd->mastersignal == -1) {
        return AHISF_ERROR;
    }
    AudioCtrl->ahiac_MixFreq = MIXFREQ;
    return AHISF_KNOWSTEREO | AHISF_MIXING | AHISF_TIMING;
}

void
_AHIsub_FreeAudio(struct AHIAudioCtrlDrv *AudioCtrl,
                  struct DriverBase *AHIsubBase)
{
    if(AudioCtrl->ahiac_DriverData != NULL) {
        FreeSignal(dd->mastersignal);
        FreeVec(AudioCtrl->ahiac_DriverData);
        AudioCtrl->ahiac_DriverData = NULL;
    }
}

void
_AHIsub_Disable(struct AHIAudioCtrlDrv *AudioCtrl,
                struct DriverBase *AHIsubBase)
{
    ObtainSemaphore(&dd->mixlock);
}

void
_AHIsub_Enable(struct AHIAudioCtrlDrv *AudioCtrl,
               struct DriverBase *AHIsubBase)
{
    ReleaseSemaphore(&dd->mixlock);
}

ULONG
_AHIsub_Start(ULONG flags,
              struct AHIAudioCtrlDrv *AudioCtrl,
              struct DriverBase *AHIsubBase)
{
    struct BTA2DPAudioBase *BTA2DPAudioBase = (struct BTA2DPAudioBase *) AHIsubBase;

    AHIsub_Stop(flags, AudioCtrl);

    if(flags & AHISF_RECORD) {
        return AHIE_UNKNOWN;
    }
    if(flags & AHISF_PLAY) {
        struct TagItem proctags[] = {
            { NP_Entry, (IPTR) &slaveentry },
            { NP_Name, (IPTR) LibName },
            { NP_Priority, 30 },
            { TAG_DONE, 0 }
        };

        dd->mixbuffer = AllocVec(AudioCtrl->ahiac_BuffSize, MEMF_ANY | MEMF_PUBLIC | MEMF_CLEAR);
        if(dd->mixbuffer == NULL) {
            return AHIE_NOMEM;
        }
        /* the slave reads tc_UserData first thing: set it before it runs */
        Forbid();
        if((dd->slavetask = CreateNewProc(proctags)) != NULL) {
            dd->slavetask->pr_Task.tc_UserData = AudioCtrl;
        }
        Permit();
        if(dd->slavetask == NULL) {
            return AHIE_NOMEM;
        }
        Wait(1L << dd->mastersignal);
        if(dd->slavetask == NULL) {
            return AHIE_UNKNOWN;
        }
    }
    return AHIE_OK;
}

void
_AHIsub_Update(ULONG flags,
               struct AHIAudioCtrlDrv *AudioCtrl,
               struct DriverBase *AHIsubBase)
{
    /* the slave reads ahiac_BuffSamples for every buffer */
}

void
_AHIsub_Stop(ULONG flags,
             struct AHIAudioCtrlDrv *AudioCtrl,
             struct DriverBase *AHIsubBase)
{
    if(flags & AHISF_PLAY) {
        if(dd->slavetask != NULL) {
            if(dd->slavesignal != -1) {
                Signal((struct Task *) dd->slavetask, 1L << dd->slavesignal);
            }
            Wait(1L << dd->mastersignal);
        }
        FreeVec(dd->mixbuffer);
        dd->mixbuffer = NULL;
    }
}

IPTR
_AHIsub_GetAttr(ULONG attribute,
                LONG argument,
                IPTR def,
                struct TagItem *taglist,
                struct AHIAudioCtrlDrv *AudioCtrl,
                struct DriverBase *AHIsubBase)
{
    switch(attribute) {
    case AHIDB_Bits:
        return 16;
    case AHIDB_Frequencies:
        return 1;
    case AHIDB_Frequency:
        return MIXFREQ;
    case AHIDB_Index:
        return 0;
    case AHIDB_Author:
        return (IPTR) "John KNIPPER";
    case AHIDB_Copyright:
        return (IPTR) "(C) 2026 John KNIPPER, APL 1.1";
    case AHIDB_Version:
        return (IPTR) LibIDString;
    case AHIDB_Record:
        return FALSE;
    case AHIDB_Realtime:
        return TRUE;
    case AHIDB_Outputs:
        return 1;
    case AHIDB_Output:
        return (IPTR) "Bluetooth";
    default:
        return def;
    }
}

ULONG
_AHIsub_HardwareControl(ULONG attribute,
                        LONG argument,
                        struct AHIAudioCtrlDrv *AudioCtrl,
                        struct DriverBase *AHIsubBase)
{
    return 0;
}
