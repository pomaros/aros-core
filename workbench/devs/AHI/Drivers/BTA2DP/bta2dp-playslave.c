/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: bta2dp.audio slave: mix one buffer, hand it to bta2dp.device and
          wait for the device to take it. While no headset streams, a timer
          stands in for the device so AHI's clock keeps running.
*/
#include <config.h>

#include <devices/ahi.h>
#include <devices/bta2dp.h>
#include <devices/timer.h>
#include <exec/execbase.h>
#include <libraries/ahi_sub.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "DriverData.h"
#include "library.h"

#define dd ((struct BTA2DPData *) AudioCtrl->ahiac_DriverData)

#undef SysBase

static void Slave(struct ExecBase *SysBase);

#include <aros/asmcall.h>

AROS_UFH3(void, SlaveEntry,
          AROS_UFHA(STRPTR, argPtr, A0),
          AROS_UFHA(ULONG, argSize, D0),
          AROS_UFHA(struct ExecBase *, SysBase, A6))
{
    AROS_USERFUNC_INIT
    Slave(SysBase);
    AROS_USERFUNC_EXIT
}

static void
Slave(struct ExecBase *SysBase)
{
    struct AHIAudioCtrlDrv *AudioCtrl = (struct AHIAudioCtrlDrv *) FindTask(NULL)->tc_UserData;
    struct DriverBase *AHIsubBase = (struct DriverBase *) dd->ahisubbase;
    struct MsgPort *port = NULL, *timerport = NULL;
    struct IOStdReq *io = NULL;
    struct timerequest *timer = NULL;
    BOOL device = FALSE, clock = FALSE, running;
    ULONG stop = SIGBREAKF_CTRL_C, retry = 0;

    dd->slavesignal = AllocSignal(-1);
    if(dd->slavesignal != -1 &&
       (port = CreateMsgPort()) && (timerport = CreateMsgPort()) &&
       (io = (struct IOStdReq *) CreateIORequest(port, sizeof(*io))) &&
       (timer = (struct timerequest *) CreateIORequest(timerport, sizeof(*timer)))) {
        clock = !OpenDevice(TIMERNAME, UNIT_MICROHZ, &timer->tr_node, 0);
        stop |= 1L << dd->slavesignal;
    }
    running = clock;
    if(running) {
        Signal((struct Task *) dd->mastertask, 1L << dd->mastersignal);
    }
    while(running) {
        BOOL played = FALSE;
        int skip;

        /* bta2dp.class creates the device and may be loaded, expunged and
           reloaded while AHI runs: look for it about once a second */
        if(!device && retry-- == 0) {
            device = !OpenDevice("bta2dp.device", 0, (struct IORequest *) io, 0);
            retry = AudioCtrl->ahiac_MixFreq / (AudioCtrl->ahiac_BuffSamples ? AudioCtrl->ahiac_BuffSamples : 1);
        }

        ObtainSemaphore(&dd->mixlock);
        skip = CallHookPkt(AudioCtrl->ahiac_PreTimerFunc, (Object *) AudioCtrl, 0);
        CallHookPkt(AudioCtrl->ahiac_PlayerFunc, AudioCtrl, NULL);
        if(!skip) {
            CallHookPkt(AudioCtrl->ahiac_MixerFunc, AudioCtrl, dd->mixbuffer);
        }
        CallHookPkt(AudioCtrl->ahiac_PostTimerFunc, (Object *) AudioCtrl, 0);
        ReleaseSemaphore(&dd->mixlock);

        if(device) {
            /* completes once the stream's ring holds it: the headset's pace */
            io->io_Command = CMD_WRITE;
            io->io_Data = dd->mixbuffer;
            io->io_Length = AudioCtrl->ahiac_BuffSamples * 2 * sizeof(WORD);
            io->io_Flags = 0;
            SendIO((struct IORequest *) io);
            while(!CheckIO((struct IORequest *) io)) {
                if(Wait(stop | (1L << port->mp_SigBit)) & stop) {
                    AbortIO((struct IORequest *) io);
                    running = FALSE;
                }
            }
            WaitIO((struct IORequest *) io);
            if(io->io_Error == 0) {
                dd->written++;
                played = TRUE;
            } else {
                dd->nostream++;
            }
        }
        if(running && !played) {
            /* no headset streaming: keep AHI's time with the timer */
            ULONG us = (ULONG) ((UQUAD) AudioCtrl->ahiac_BuffSamples * 1000000 / AudioCtrl->ahiac_MixFreq);
            timer->tr_node.io_Command = TR_ADDREQUEST;
            timer->tr_time.tv_secs = us / 1000000;
            timer->tr_time.tv_micro = us % 1000000;
            SendIO(&timer->tr_node);
            while(!CheckIO(&timer->tr_node)) {
                if(Wait(stop | (1L << timerport->mp_SigBit)) & stop) {
                    AbortIO(&timer->tr_node);
                    running = FALSE;
                }
            }
            WaitIO(&timer->tr_node);
        }
    }
    if(device) {
        CloseDevice((struct IORequest *) io);
    }
    if(clock) {
        CloseDevice(&timer->tr_node);
    }
    DeleteIORequest((struct IORequest *) timer);
    DeleteIORequest((struct IORequest *) io);
    DeleteMsgPort(timerport);
    DeleteMsgPort(port);
    FreeSignal(dd->slavesignal);
    dd->slavesignal = -1;

    /* the master frees our code's library only after this signal; nothing
       of ours runs once Forbid() lets it go and we are gone */
    Forbid();
    dd->slavetask = NULL;
    Signal((struct Task *) dd->mastertask, 1L << dd->mastersignal);
}
