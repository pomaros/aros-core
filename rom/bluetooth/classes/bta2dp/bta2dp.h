/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: bta2dp.class - A2DP source. Binds to a headset's Audio Sink
          service, runs AVDTP and streams SBC; bta2dp.device takes the PCM
          (AHI's bta2dp.audio driver writes to it).
*/
#ifndef BTA2DP_H
#define BTA2DP_H

#include LC_LIBDEFS_FILE

#include <aros/libcall.h>
#include <aros/asmcall.h>
#include <aros/symbolsets.h>
#include <exec/types.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/devices.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/errors.h>
#include <exec/semaphores.h>
#include <devices/timer.h>
#include <utility/utility.h>
#include <dos/dos.h>
#include <libraries/bluetooth.h>
#include <libraries/btclass.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/bluetooth.h>
#include <string.h>

#include "stream.h"
#include "avrcp.h"
#include <devices/bta2dp.h>

#define NewList NEWLIST

struct BTA2DPUnit;

#define BTA2DPF_CLOSING 0x0001  /* expunging: bta2dp.device refuses new opens */

struct BTA2DPBase
{
    struct Library      nh_Library;
    UWORD               nh_Flags;
    struct Library     *nh_UtilityBase;
    struct BTA2DPDevBase *nh_DevBase;     /* bta2dp.device */
    APTR                nh_Record;        /* our Audio Source SDP record */
    APTR                nh_RecordTG;      /* AVRCP target: the headset's buttons */
    APTR                nh_RecordCT;      /* AVRCP controller: absolute volume */
    ULONG               nh_Volume;        /* output volume in percent, under nh_Lock */
    /* The bound unit and the device's view of it; taken by the binding
       code, the unit task and BeginIO/AbortIO callers on any CPU. */
    struct SignalSemaphore nh_Lock;
    struct BTA2DPUnit  *nh_Unit;
};

struct BTA2DPDevBase
{
    struct Library      nsd_Library;
    UWORD               nsd_Flags;
    BPTR                nsd_SegList;
    struct BTA2DPBase  *nsd_ClsBase;
    struct Unit         nsd_Unit;         /* the one device unit (requests queue here) */
    struct MinList      nsd_Writes;       /* CMD_WRITEs waiting for ring room */
};

struct BTA2DPUnit
{
    struct BTA2DPBase  *nau_Base;
    struct Library     *nau_BluetoothBase; /* opened by the unit task */
    struct BtDevice    *nau_Device;
    struct BtService   *nau_Service;
    struct BtEndpoint  *nau_SigEndpoint;
    struct BtEndpoint  *nau_MediaEndpoint; /* clone: its own L2CAP channel */
    struct Task        *nau_Task;
    struct Task        *nau_Waiter;       /* binder/releaser waiting for the task */
    ULONG               nau_WaiterMask;
    LONG                nau_Status;       /* start result for the binder */
    struct MsgPort     *nau_Port;         /* channel completions */
    struct MsgPort     *nau_TimerPort;
    struct MsgPort     *nau_EventPort;    /* link up/down notes from the stack */
    APTR                nau_EventHandler;
    ULONG               nau_Sessions;     /* AVDTP sessions started on this binding */
    /* AVRCP over AVCTP to the headset's remote-control target */
    struct BtEndpoint  *nau_AvEndpoint;
    struct BtChannel   *nau_AvRead, *nau_AvWrite;
    UBYTE               nau_AvBuffer[512];
    BOOL                nau_AvOpen;
    BOOL                nau_AbsVolumeSet; /* our volume given to the headset this session */
    struct AVRCPSession nau_Avrcp;
    struct MsgPort     *nau_InputPort;    /* input.device: headset buttons as media keys */
    struct IOStdReq    *nau_Input;
    ULONG               nau_AppliedVolume; /* last volume pushed to the headset or the gain */
        BOOL                nau_LinkUp;       /* as far as the stack's events tell */
    UBYTE               nau_Retries;      /* failed sessions since the link came up */
    uint64_t            nau_RetryAt;      /* restart a failed session then, 0 = none */
    struct timerequest *nau_Timer;
    BOOL                nau_TimerPending;
    struct BtChannel   *nau_SigRead, *nau_SigWrite, *nau_MediaRead, *nau_MediaWrite;
    UBYTE               nau_SigBuffer[1024];
    UBYTE               nau_MediaProbe[4];
    BOOL                nau_SigReadPending, nau_MediaOpening, nau_MediaOpen;
    ULONG               nau_MediaMTU;
    UBYTE               nau_Address[6];
    struct A2DPSource   nau_Source;
    struct A2DPStream   nau_Stream;       /* guarded by nh_Lock: the device writes PCM */
    BOOL                nau_Configured;   /* media open: rate and channels known */
    BOOL                nau_Streaming;
    BOOL                nau_ResumeWanted; /* a write waits for a suspended stream */
    uint64_t            nau_ResumeDeadline;
    uint64_t            nau_SignalSentAt; /* last AVDTP command, for its answer timeout */
    UBYTE               nau_LastState;
    ULONG               nau_Packets, nau_SendErrors;
};

void bta2dp_DeviceRetryWrites(struct BTA2DPBase *nh);
void bta2dp_DeviceFailWrites(struct BTA2DPBase *nh, BYTE error);

#endif
