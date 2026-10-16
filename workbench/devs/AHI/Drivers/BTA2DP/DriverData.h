/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: bta2dp.audio - AHI driver feeding bta2dp.device (Bluetooth A2DP).
*/
#ifndef AHI_Drivers_BTA2DP_DriverData_h
#define AHI_Drivers_BTA2DP_DriverData_h

#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "DriverBase.h"

struct BTA2DPAudioBase {
    struct DriverBase driverbase;
    struct DosLibrary *dosbase;
};

#define DRIVERBASE_SIZEOF (sizeof (struct BTA2DPAudioBase))

#define DOSBase (*(struct DosLibrary **) &BTA2DPAudioBase->dosbase)

struct BTA2DPData {
    struct DriverData driverdata;
    BYTE mastersignal;
    BYTE slavesignal;
    struct Process *mastertask;
    struct Process *slavetask;
    struct BTA2DPAudioBase *ahisubbase;
    APTR mixbuffer;
    /* AHIsub_Disable() must hold off the mixer on every CPU: Forbid() only
       stops this one, so the slave mixes under this semaphore instead. */
    struct SignalSemaphore mixlock;
    ULONG written, nostream;
};

#endif
