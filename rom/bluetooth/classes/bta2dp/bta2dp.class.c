/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: bta2dp.class - A2DP source for bluetooth.library. Binds to the
          Audio Sink service (0x110B) of a registered device; its task opens
          AVDTP signalling, configures SBC, opens the media channel on a
          cloned endpoint and streams PCM written to bta2dp.device.
          Shared state is guarded by nh_Lock (SMP: Forbid() would not).
*/
#include "bta2dp.h"
#include <devices/input.h>
#include <devices/inputevent.h>
#include <devices/rawkeycodes.h>
#include <dos/var.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>

static const STRPTR libname = MOD_NAME_STRING;

#define A2DP_PSM 0x0019
#define PUMP_US 5000            /* pump period while streaming */
#define LEAD_MS 30              /* packets may run this far ahead */

AROS_UFP0(void, GM_UNIQUENAME(bA2DPTask));
struct BTA2DPDevBase *bta2dp_CreateDevice(struct BTA2DPBase *nh);
void bta2dp_DeleteDevice(struct BTA2DPBase *nh);

/* /// "Lib Stuff" */
static int GM_UNIQUENAME(libInit)(LIBBASETYPEPTR nh)
{
    struct Library *BluetoothBase;
    nh->nh_UtilityBase = OpenLibrary("utility.library", 39);
    if(!nh->nh_UtilityBase) {
        return FALSE;
    }
    InitSemaphore(&nh->nh_Lock);
    {
        /* the volume a previous session left, else most of the way up */
        char text[8];
        LONG pct = GetVar("Sys/AudioVolume", text, sizeof(text), GVF_GLOBAL_ONLY) > 0 ? atol(text) : 80;
        nh->nh_Volume = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    }
    if(!(nh->nh_DevBase = bta2dp_CreateDevice(nh))) {
        CloseLibrary(nh->nh_UtilityBase);
        return FALSE;
    }
    /* headsets browse for an Audio Source before they accept us */
    if((BluetoothBase = OpenLibrary("bluetooth.library", 1))) {
        nh->nh_Record = btAddServiceRecord(BSRA_UUID16, 0x110a, BSRA_Protocol, BSVP_L2CAP,
                                           BSRA_PSM, A2DP_PSM, BSRA_ProtocolUUID16, 0x0019,
                                           BSRA_ProtocolVersion, 0x0103, BSRA_ProfileUUID16, 0x110d,
                                           BSRA_ProfileVersion, 0x0103, BSRA_SupportedFeatures, 0x0001,
                                           BSRA_Name, (IPTR) "Audio Source", TAG_END);
        /* AVRCP: target for the headset's buttons, controller for its
           absolute volume (category 1 player, category 2 amplifier) */
        nh->nh_RecordTG = btAddServiceRecord(BSRA_UUID16, 0x110c, BSRA_Protocol, BSVP_L2CAP,
                                             BSRA_PSM, AVRCP_PSM, BSRA_ProtocolUUID16, 0x0017,
                                             BSRA_ProtocolVersion, 0x0104, BSRA_ProfileUUID16, 0x110e,
                                             BSRA_ProfileVersion, 0x0106, BSRA_SupportedFeatures, 0x0001,
                                             BSRA_Name, (IPTR) "AVRCP Target", TAG_END);
        nh->nh_RecordCT = btAddServiceRecord(BSRA_UUID16, 0x110e, BSRA_Protocol, BSVP_L2CAP,
                                             BSRA_PSM, AVRCP_PSM, BSRA_ProtocolUUID16, 0x0017,
                                             BSRA_ProtocolVersion, 0x0104, BSRA_ProfileUUID16, 0x110e,
                                             BSRA_ProfileVersion, 0x0106, BSRA_SupportedFeatures, 0x0002,
                                             BSRA_Name, (IPTR) "AVRCP Controller", TAG_END);
        CloseLibrary(BluetoothBase);
    }
    return TRUE;
}

static int GM_UNIQUENAME(libExpunge)(LIBBASETYPEPTR nh)
{
    struct Library *BluetoothBase;
    ObtainSemaphore(&nh->nh_Lock);
    if(nh->nh_Unit || (nh->nh_DevBase && nh->nh_DevBase->nsd_Library.lib_OpenCnt)) {
        ReleaseSemaphore(&nh->nh_Lock);
        return FALSE;
    }
    nh->nh_Flags |= BTA2DPF_CLOSING;
    ReleaseSemaphore(&nh->nh_Lock);
    if(nh->nh_Record && (BluetoothBase = OpenLibrary("bluetooth.library", 1))) {
        btRemServiceRecord(nh->nh_Record);
        nh->nh_Record = NULL;
        if(nh->nh_RecordTG) btRemServiceRecord(nh->nh_RecordTG);
        if(nh->nh_RecordCT) btRemServiceRecord(nh->nh_RecordCT);
        nh->nh_RecordTG = nh->nh_RecordCT = NULL;
        CloseLibrary(BluetoothBase);
    }
    bta2dp_DeleteDevice(nh);
    CloseLibrary(nh->nh_UtilityBase);
    return TRUE;
}

ADD2INITLIB(GM_UNIQUENAME(libInit), 0)
ADD2EXPUNGELIB(GM_UNIQUENAME(libExpunge), 0)
/* \\\ */

/* /// "binding" */
static struct BTA2DPUnit *bind(struct BTA2DPBase *nh, struct BtService *bsv, BOOL force)
{
    struct Library *BluetoothBase;
    struct BTA2DPUnit *nau;
    struct BtDevice *bd = NULL;
    struct BtEndpoint *bep;
    IPTR uuid16 = 0, proto = 0;
    UBYTE *addr = NULL;
    BYTE bit;

    if(!(BluetoothBase = OpenLibrary("bluetooth.library", 1))) {
        return NULL;
    }
    btGetAttrs(BGA_SERVICE, bsv, BSVA_UUID16, &uuid16, BSVA_Protocol, &proto, BSVA_Device, &bd, TAG_END);
    bep = btFindEndpoint(bsv, NULL, BEA_PSM, A2DP_PSM, TAG_END);
    if((!force && uuid16 != 0x110b) || !bep || !bd) {
        CloseLibrary(BluetoothBase);
        return NULL;
    }
    btGetAttrs(BGA_DEVICE, bd, BDA_Address, &addr, TAG_END);
    ObtainSemaphore(&nh->nh_Lock);
    if(nh->nh_Unit) {
        /* one headset at a time */
        ReleaseSemaphore(&nh->nh_Lock);
        CloseLibrary(BluetoothBase);
        return NULL;
    }
    ReleaseSemaphore(&nh->nh_Lock);
    if(!(nau = AllocVec(sizeof(*nau), MEMF_PUBLIC | MEMF_CLEAR)) || (bit = AllocSignal(-1)) < 0) {
        FreeVec(nau);
        CloseLibrary(BluetoothBase);
        return NULL;
    }
    nau->nau_Base = nh;
    nau->nau_Device = bd;
    nau->nau_Service = bsv;
    nau->nau_SigEndpoint = bep;
    if(addr) {
        CopyMem(addr, nau->nau_Address, 6);
    }
    nau->nau_Waiter = FindTask(NULL);
    nau->nau_WaiterMask = 1UL << bit;
    SetSignal(0, nau->nau_WaiterMask);
    if(btSpawnSubTask("bta2dp.class", (APTR) GM_UNIQUENAME(bA2DPTask), nau)) {
        while(!nau->nau_Task && !nau->nau_Status) {
            Wait(nau->nau_WaiterMask);
        }
    } else {
        nau->nau_Status = -1;
    }
    FreeSignal(bit);
    if(!nau->nau_Task) {
        FreeVec(nau);
        CloseLibrary(BluetoothBase);
        return NULL;
    }
    ObtainSemaphore(&nh->nh_Lock);
    nh->nh_Unit = nau;
    ReleaseSemaphore(&nh->nh_Lock);
    btAddErrorMsg(RETURN_OK, (STRPTR) libname, "A2DP audio sink bound (bta2dp.device unit 0).");
    CloseLibrary(BluetoothBase);
    return nau;
}

static void release(struct BTA2DPBase *nh, struct BTA2DPUnit *nau)
{
    BYTE bit = AllocSignal(-1);
    if(bit < 0) {
        bit = SIGB_SINGLE;
    }
    ObtainSemaphore(&nh->nh_Lock);
    if(nh->nh_Unit == nau) {
        nh->nh_Unit = NULL;
    }
    ReleaseSemaphore(&nh->nh_Lock);
    nau->nau_Waiter = FindTask(NULL);
    nau->nau_WaiterMask = 1UL << bit;
    SetSignal(0, nau->nau_WaiterMask);
    Signal(nau->nau_Task, SIGBREAKF_CTRL_C);
    while(nau->nau_Task) {
        Wait(nau->nau_WaiterMask);
    }
    if(bit != SIGB_SINGLE) {
        FreeSignal(bit);
    }
    FreeVec(nau);
}
/* \\\ */

/* /// "btcGetAttrsA()" */
AROS_LH3(LONG, btcGetAttrsA,
         AROS_LHA(ULONG, type, D0),
         AROS_LHA(APTR, btstruct, A0),
         AROS_LHA(struct TagItem *, tags, A1),
         LIBBASETYPEPTR, nh, 5, bta2dp)
{
    AROS_LIBFUNC_INIT
    struct TagItem *ti;
    LONG count = 0;
    (void) btstruct;
    if(type == BCGA_CLASS) {
        if((ti = FindTagItem(BCCA_Priority, tags))) {
            *((SIPTR *) ti->ti_Data) = 0;
            count++;
        }
        if((ti = FindTagItem(BCCA_Description, tags))) {
            *((STRPTR *) ti->ti_Data) = "A2DP audio source (SBC) for headsets";
            count++;
        }
        if((ti = FindTagItem(BCCA_HasClassCfgGUI, tags))) {
            *((IPTR *) ti->ti_Data) = FALSE;
            count++;
        }
        if((ti = FindTagItem(BCCA_HasBindingCfgGUI, tags))) {
            *((IPTR *) ti->ti_Data) = FALSE;
            count++;
        }
    }
    return count;
    AROS_LIBFUNC_EXIT
}
/* \\\ */

/* /// "btcSetAttrsA()" */
AROS_LH3(LONG, btcSetAttrsA,
         AROS_LHA(ULONG, type, D0),
         AROS_LHA(APTR, btstruct, A0),
         AROS_LHA(struct TagItem *, tags, A1),
         LIBBASETYPEPTR, nh, 6, bta2dp)
{
    AROS_LIBFUNC_INIT
    (void) type; (void) btstruct; (void) tags;
    return 0;
    AROS_LIBFUNC_EXIT
}
/* \\\ */

/* /// "btcDoMethodA()" */
AROS_LH2(SIPTR, btcDoMethodA,
         AROS_LHA(ULONG, methodid, D0),
         AROS_LHA(IPTR *, methoddata, A1),
         LIBBASETYPEPTR, nh, 7, bta2dp)
{
    AROS_LIBFUNC_INIT
    switch(methodid) {
    case BCM_AttemptServiceBinding:
        return (SIPTR) bind(nh, (struct BtService *) methoddata[0], FALSE);
    case BCM_ForceServiceBinding:
        return (SIPTR) bind(nh, (struct BtService *) methoddata[0], TRUE);
    case BCM_ReleaseServiceBinding:
        release(nh, (struct BTA2DPUnit *) methoddata[0]);
        return TRUE;
    case BCM_AttemptDeviceBinding:
    case BCM_ForceDeviceBinding:
        return 0;
    default:
        break;
    }
    return 0;
    AROS_LIBFUNC_EXIT
}
/* \\\ */

/* /// "unit task" */
static uint64_t now_us(struct BTA2DPUnit *nau)
{
    uint64_t ticks, hz;
    (void) nau;
    __asm__ volatile("isb; mrs %0,cntvct_el0; mrs %1,cntfrq_el0" : "=r"(ticks), "=r"(hz));
    return hz ? ticks / hz * 1000000 + (ticks % hz) * 1000000 / hz : 0;
}

#define BluetoothBase (nau->nau_BluetoothBase)

/* AVDTP signalling goes out synchronously on the write channel. */
static uint64_t now_us(struct BTA2DPUnit *nau);

static int send_signal(void *context, const uint8_t *data, unsigned length)
{
    struct BTA2DPUnit *nau = context;
    nau->nau_SignalSentAt = now_us(nau);
    btChannelSetup(nau->nau_SigWrite, BTPR_WRITE, 0, 0);
    return btDoChannel(nau->nau_SigWrite, (APTR) data, length) ? -1 : 0;
}

static int send_media(void *context, const uint8_t *data, unsigned length)
{
    struct BTA2DPUnit *nau = context;
    btChannelSetup(nau->nau_MediaWrite, BTPR_WRITE, 0, 0);
    if(btDoChannel(nau->nau_MediaWrite, (APTR) data, length)) {
        nau->nau_SendErrors++;
        return -1;
    }
    nau->nau_Packets++;
    return 0;
}

static void post_signal_read(struct BTA2DPUnit *nau)
{
    btChannelSetup(nau->nau_SigRead, BTPR_READ, 0, 0);
    btSendChannel(nau->nau_SigRead, nau->nau_SigBuffer, sizeof(nau->nau_SigBuffer));
    nau->nau_SigReadPending = TRUE;
}

/* After OPEN: a NOWAIT read on a cloned endpoint opens the media channel
   and returns as soon as it is open, carrying the peer MTU with it. */
static BOOL open_media(struct BTA2DPUnit *nau)
{
    /* one clone serves every session: the stack keeps it with the service */
    if(!nau->nau_MediaEndpoint) {
        nau->nau_MediaEndpoint = btCloneEndpoint(nau->nau_SigEndpoint);
    }
    if(!nau->nau_MediaEndpoint ||
       !(nau->nau_MediaRead = btAllocChannel(nau->nau_Device, nau->nau_Port, nau->nau_MediaEndpoint)) ||
       !(nau->nau_MediaWrite = btAllocChannel(nau->nau_Device, nau->nau_Port, nau->nau_MediaEndpoint))) {
        return FALSE;
    }
    btSetAttrs(BGA_CHANNEL, nau->nau_MediaRead, BCHA_NoWait, TRUE, BCHA_AutoConnect, TRUE, TAG_END);
    btSetAttrs(BGA_CHANNEL, nau->nau_MediaWrite, BCHA_AutoConnect, TRUE, TAG_END);
    btChannelSetup(nau->nau_MediaRead, BTPR_READ, 0, 0);
    btSendChannel(nau->nau_MediaRead, nau->nau_MediaProbe, sizeof(nau->nau_MediaProbe));
    nau->nau_MediaOpening = TRUE;
    return TRUE;
}

static void arm_timer(struct BTA2DPUnit *nau)
{
    if(nau->nau_TimerPending) {
        return;
    }
    nau->nau_Timer->tr_node.io_Command = TR_ADDREQUEST;
    nau->nau_Timer->tr_time.tv_secs = 0;
    nau->nau_Timer->tr_time.tv_micro = PUMP_US;
    SendIO(&nau->nau_Timer->tr_node);
    nau->nau_TimerPending = TRUE;
}

/* /// "volume and AVRCP" */
static void save_volume(ULONG pct)
{
    char text[8];
    int n = snprintf(text, sizeof(text), "%lu", (unsigned long) pct);
    SetVar("Sys/AudioVolume", text, n, GVF_GLOBAL_ONLY);
}

/* The headset sets the level itself when it offers absolute volume;
   otherwise the PCM is scaled (squared for an even perceived step). */
static void apply_volume(struct BTA2DPUnit *nau, BOOL force)
{
    struct BTA2DPBase *nh = nau->nau_Base;
    ULONG pct, gain;
    ObtainSemaphore(&nh->nh_Lock);
    pct = nh->nh_Volume;
    gain = nau->nau_Avrcp.absolute_volume ? 65536 : pct * pct * 65536 / 10000;
    A2DPStreamSetGain(&nau->nau_Stream, gain);
    ReleaseSemaphore(&nh->nh_Lock);
    if(nau->nau_Avrcp.absolute_volume && (force || pct != nau->nau_AppliedVolume)) {
        AVRCPSetVolume(&nau->nau_Avrcp, (UBYTE) ((pct * AVRCP_VOLUME_MAX + 50) / 100));
    }
    nau->nau_AppliedVolume = pct;
}

static void set_volume(struct BTA2DPUnit *nau, LONG pct)
{
    struct BTA2DPBase *nh = nau->nau_Base;
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    ObtainSemaphore(&nh->nh_Lock);
    nh->nh_Volume = pct;
    ReleaseSemaphore(&nh->nh_Lock);
    save_volume(pct);
    apply_volume(nau, FALSE);
}

static int send_avrcp(void *context, const uint8_t *data, unsigned length)
{
    struct BTA2DPUnit *nau = context;
    if(!nau->nau_AvWrite) {
        return -1;
    }
    btChannelSetup(nau->nau_AvWrite, BTPR_WRITE, 0, 0);
    return btDoChannel(nau->nau_AvWrite, (APTR) data, length) ? -1 : 0;
}

static void media_key(struct BTA2DPUnit *nau, UWORD code, int pressed)
{
    struct InputEvent ie;
    if(!nau->nau_Input) {
        return;
    }
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = code | (pressed ? 0 : IECODE_UP_PREFIX);
    nau->nau_Input->io_Command = IND_WRITEEVENT;
    nau->nau_Input->io_Data = &ie;
    nau->nau_Input->io_Length = sizeof(ie);
    DoIO((struct IORequest *) nau->nau_Input);
}

/* A headset button: transport keys reach applications as the media keys
   of a multimedia keyboard; volume keys step our volume. */
static void avrcp_button(void *context, uint8_t op, int pressed)
{
    struct BTA2DPUnit *nau = context;
    struct BTA2DPBase *nh = nau->nau_Base;
    UWORD key = 0;
    btAddErrorMsg(RETURN_OK, (STRPTR) libname, "Headset button 0x%02lx %s.", (IPTR) op,
                  (IPTR) (pressed ? "pressed" : "released"));
    switch(op) {
    case AVRCP_OP_PLAY:
    case AVRCP_OP_PAUSE:    key = RAWKEY_MEDIA2; break;
    case AVRCP_OP_STOP:     key = RAWKEY_MEDIA1; break;
    case AVRCP_OP_BACKWARD: key = RAWKEY_MEDIA3; break;
    case AVRCP_OP_FORWARD:  key = RAWKEY_MEDIA4; break;
    case AVRCP_OP_REWIND:   key = RAWKEY_MEDIA5; break;
    case AVRCP_OP_FAST_FORWARD: key = RAWKEY_MEDIA6; break;
    case AVRCP_OP_VOLUME_UP:
    case AVRCP_OP_VOLUME_DOWN:
        if(pressed) {
            LONG pct;
            ObtainSemaphore(&nh->nh_Lock);
            pct = nh->nh_Volume;
            ReleaseSemaphore(&nh->nh_Lock);
            set_volume(nau, pct + (op == AVRCP_OP_VOLUME_UP ? 10 : -10));
        }
        return;
    default:
        return;
    }
    media_key(nau, key, pressed);
}

/* The headset changed its volume (its own controls). */
static void avrcp_volume(void *context, uint8_t volume)
{
    struct BTA2DPUnit *nau = context;
    struct BTA2DPBase *nh = nau->nau_Base;
    ULONG pct = (volume * 100U + AVRCP_VOLUME_MAX / 2) / AVRCP_VOLUME_MAX;
    ObtainSemaphore(&nh->nh_Lock);
    nh->nh_Volume = pct;
    ReleaseSemaphore(&nh->nh_Lock);
    nau->nau_AppliedVolume = pct;
    save_volume(pct);
    btAddErrorMsg(RETURN_OK, (STRPTR) libname, "Headset volume %ld%%.", (IPTR) pct);
}

static void post_av_read(struct BTA2DPUnit *nau)
{
    btChannelSetup(nau->nau_AvRead, BTPR_READ, 0, 0);
    btSendChannel(nau->nau_AvRead, nau->nau_AvBuffer, sizeof(nau->nau_AvBuffer));
}

/* The headset's AVRCP target: one AVCTP channel for both directions. */
static void open_avrcp(struct BTA2DPUnit *nau)
{
    struct BtService *bsv;
    if(nau->nau_AvOpen) {
        return;
    }
    if(!nau->nau_AvEndpoint) {
        bsv = btFindService(nau->nau_Device, NULL, BSVA_UUID16, 0x110c, TAG_END);
        if(bsv) {
            nau->nau_AvEndpoint = btFindEndpoint(bsv, NULL, BEA_PSM, AVRCP_PSM, TAG_END);
        }
    }
    if(!nau->nau_AvEndpoint ||
       !(nau->nau_AvRead = btAllocChannel(nau->nau_Device, nau->nau_Port, nau->nau_AvEndpoint)) ||
       !(nau->nau_AvWrite = btAllocChannel(nau->nau_Device, nau->nau_Port, nau->nau_AvEndpoint))) {
        btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "Headset offers no AVRCP target; no remote control or absolute volume.");
        return;
    }
    btSetAttrs(BGA_CHANNEL, nau->nau_AvRead, BCHA_AutoConnect, TRUE, TAG_END);
    btSetAttrs(BGA_CHANNEL, nau->nau_AvWrite, BCHA_AutoConnect, TRUE, TAG_END);
    memset(&nau->nau_Avrcp, 0, sizeof(nau->nau_Avrcp));
    nau->nau_Avrcp.send = send_avrcp;
    nau->nau_Avrcp.button = avrcp_button;
    nau->nau_Avrcp.volume = avrcp_volume;
    nau->nau_Avrcp.context = nau;
    nau->nau_AvOpen = TRUE;
    post_av_read(nau);
    if(AVRCPStart(&nau->nau_Avrcp)) {
        btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "AVRCP could not be started.");
    }
}

static void free_channel(struct BTA2DPUnit *nau, struct BtChannel **pp);

static void close_avrcp(struct BTA2DPUnit *nau)
{
    free_channel(nau, &nau->nau_AvRead);
    free_channel(nau, &nau->nau_AvWrite);
    nau->nau_AvOpen = FALSE;
    nau->nau_AbsVolumeSet = FALSE;
    nau->nau_Avrcp.absolute_volume = 0;
}
/* \\ */

static void start_stream(struct BTA2DPUnit *nau)
{
    struct BTA2DPBase *nh = nau->nau_Base;
    IPTR mtu = 0;
    btGetAttrs(BGA_CHANNEL, nau->nau_MediaWrite, BCHA_RemoteMTU, &mtu, TAG_END);
    if(!mtu) {
        btGetAttrs(BGA_CHANNEL, nau->nau_MediaRead, BCHA_RemoteMTU, &mtu, TAG_END);
    }
    nau->nau_MediaMTU = mtu ? mtu : 672;
    ObtainSemaphore(&nh->nh_Lock);
    if(!A2DPStreamInit(&nau->nau_Stream, &nau->nau_Source, nau->nau_MediaMTU, LEAD_MS)) {
        nau->nau_Configured = TRUE;
        nau->nau_Streaming = TRUE;
        nau->nau_ResumeWanted = FALSE;
    }
    ReleaseSemaphore(&nh->nh_Lock);
    if(!nau->nau_Streaming) {
        btAddErrorMsg(RETURN_WARN, (STRPTR) libname,
                      "A2DP pump not started: SBC %ld Hz mode %ld alloc %ld blocks %ld subbands %ld bitpool %ld, frame %ld, MTU %ld.",
                      (IPTR) nau->nau_Source.config.frequency, (IPTR) nau->nau_Source.config.mode,
                      (IPTR) nau->nau_Source.config.allocation,
                      (IPTR) nau->nau_Source.config.blocks, (IPTR) nau->nau_Source.config.subbands,
                      (IPTR) nau->nau_Source.config.bitpool, (IPTR) nau->nau_Stream.frame_length,
                      (IPTR) nau->nau_MediaMTU);
    }
    if(nau->nau_Streaming) {
        btAddErrorMsg(RETURN_OK, (STRPTR) libname,
                      "A2DP streaming: SBC %ld Hz, mode %ld, bitpool %ld, media MTU %ld.",
                      (IPTR) nau->nau_Source.config.frequency, (IPTR) nau->nau_Source.config.mode,
                      (IPTR) nau->nau_Source.config.bitpool, (IPTR) nau->nau_MediaMTU);
        arm_timer(nau);
        apply_volume(nau, TRUE);
        open_avrcp(nau);
        AVRCPSetPlaying(&nau->nau_Avrcp, 1);
        /* writes parked while the stream was suspended go in now */
        bta2dp_DeviceRetryWrites(nh);
    }
}

/* The stream is gone for good (closed, failed, released): writes fail. */
static void stop_stream(struct BTA2DPUnit *nau, BYTE error)
{
    struct BTA2DPBase *nh = nau->nau_Base;
    ObtainSemaphore(&nh->nh_Lock);
    nau->nau_Streaming = FALSE;
    nau->nau_Configured = FALSE;
    nau->nau_ResumeWanted = FALSE;
    ReleaseSemaphore(&nh->nh_Lock);
    bta2dp_DeviceFailWrites(nh, error);
}

/* The headset suspended the stream: writes wait for a resume. */
static void suspend_stream(struct BTA2DPUnit *nau)
{
    struct BTA2DPBase *nh = nau->nau_Base;
    ObtainSemaphore(&nh->nh_Lock);
    nau->nau_Streaming = FALSE;
    ReleaseSemaphore(&nh->nh_Lock);
    AVRCPSetPlaying(&nau->nau_Avrcp, 0);
}

#define RESUME_US 3000000

/* A write waits on a suspended stream: ask the headset to START again. A
   resume that does not come in time fails the waiting writes, so AHI falls
   back to its own clock instead of blocking. */
static void resume_stream(struct BTA2DPUnit *nau)
{
    struct BTA2DPBase *nh = nau->nau_Base;
    BOOL wanted;
    ObtainSemaphore(&nh->nh_Lock);
    wanted = nau->nau_ResumeWanted && !nau->nau_Streaming;
    ReleaseSemaphore(&nh->nh_Lock);
    if(!wanted || nau->nau_Source.state != A2DP_SUSPENDED) {
        return;
    }
    if(A2DPResume(&nau->nau_Source)) {
        btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "A2DP resume could not be sent.");
        return;
    }
    btAddErrorMsg(RETURN_OK, (STRPTR) libname, "A2DP stream resumed for new audio.");
    nau->nau_ResumeDeadline = now_us(nau) + RESUME_US;
    arm_timer(nau);
}

static void check_resume(struct BTA2DPUnit *nau)
{
    struct BTA2DPBase *nh = nau->nau_Base;
    BOOL expired;
    ObtainSemaphore(&nh->nh_Lock);
    expired = nau->nau_ResumeWanted && !nau->nau_Streaming && now_us(nau) > nau->nau_ResumeDeadline;
    if(expired) {
        nau->nau_ResumeWanted = FALSE;
    }
    ReleaseSemaphore(&nh->nh_Lock);
    if(expired) {
        btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "A2DP stream did not resume; waiting writes failed.");
        bta2dp_DeviceFailWrites(nh, BTA2DPERR_NOSTREAM);
    }
}

static void pump(struct BTA2DPUnit *nau)
{
    struct BTA2DPBase *nh = nau->nau_Base;
    int rc;
    ObtainSemaphore(&nh->nh_Lock);
    rc = nau->nau_Streaming ? A2DPStreamPump(&nau->nau_Stream, now_us(nau), send_media, nau) : 0;
    ReleaseSemaphore(&nh->nh_Lock);
    if(rc < 0) {
        btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "A2DP media send failed; stream stopped.");
        stop_stream(nau, IOERR_ABORTED);
        return;
    }
    /* room in the ring: parked PCM writes can go in */
    bta2dp_DeviceRetryWrites(nh);
}

static const char *const state_names[] = {
    "idle", "discovering", "capabilities", "configuring", "opening", "media",
    "starting", "streaming", "suspended", "closed", "failed"
};

#define SIGNAL_TIMEOUT_US 5000000

static void note_state(struct BTA2DPUnit *nau)
{
    UBYTE state = (UBYTE) nau->nau_Source.state;
    if(state != nau->nau_LastState && state <= A2DP_FAILED) {
        btAddErrorMsg(RETURN_OK, (STRPTR) libname, "AVDTP %s -> %s (remote SEID %ld).",
                      (IPTR) state_names[nau->nau_LastState], (IPTR) state_names[state],
                      (IPTR) nau->nau_Source.remote_seid);
        nau->nau_LastState = state;
    }
}

static BOOL negotiating(struct BTA2DPUnit *nau)
{
    return nau->nau_Source.pending_signal != 0;
}

/* AVDTP answers within seconds; one that never comes would leave the
   binding stuck with no error, so a silent sink fails the stream. */
static void check_signal(struct BTA2DPUnit *nau)
{
    if(negotiating(nau) && now_us(nau) - nau->nau_SignalSentAt > SIGNAL_TIMEOUT_US) {
        btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "AVDTP signal 0x%02lx unanswered in state %s.",
                      (IPTR) nau->nau_Source.pending_signal, (IPTR) state_names[nau->nau_Source.state]);
        nau->nau_Source.pending_signal = 0;
        if(nau->nau_Source.resuming) {
            nau->nau_Source.resuming = 0;
            nau->nau_Source.state = A2DP_SUSPENDED;
        } else {
            nau->nau_Source.state = A2DP_FAILED;
            stop_stream(nau, BTA2DPERR_NOSTREAM);
        }
        note_state(nau);
    }
}

static void handle_channel(struct BTA2DPUnit *nau, struct BtChannel *bch)
{
    LONG err = btGetChannelError(bch);
    if(bch == nau->nau_SigRead) {
        nau->nau_SigReadPending = FALSE;
        if(err) {
            btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "AVDTP signalling channel error %ld; waiting for the headset.", (IPTR) err);
            nau->nau_Source.state = A2DP_FAILED;
            nau->nau_Source.pending_signal = 0;
            note_state(nau);
            stop_stream(nau, BTA2DPERR_NOSTREAM);
            return;
        }
        A2DPReceive(&nau->nau_Source, nau->nau_SigBuffer, btGetChannelActual(bch));
        note_state(nau);
        if(nau->nau_Source.state == A2DP_MEDIA && !nau->nau_MediaOpening && !nau->nau_MediaOpen &&
           !open_media(nau)) {
            nau->nau_Source.state = A2DP_FAILED;
        }
        if(nau->nau_Source.state == A2DP_STREAMING && !nau->nau_Streaming) {
            if(nau->nau_MediaOpen) {
                start_stream(nau);
            } else {
                btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "AVDTP streaming before the media channel opened.");
            }
        }
        if(nau->nau_Source.state == A2DP_SUSPENDED && nau->nau_Streaming) {
            btAddErrorMsg(RETURN_OK, (STRPTR) libname, "A2DP stream suspended after %ld media packets.",
                          (IPTR) nau->nau_Packets);
            suspend_stream(nau);
        }
        if(nau->nau_Source.state == A2DP_SUSPENDED && nau->nau_Source.error) {
            btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "A2DP resume refused (AVDTP error %ld).",
                          (IPTR) nau->nau_Source.error);
            nau->nau_Source.error = 0;
        }
        if(nau->nau_Source.state == A2DP_CLOSED && nau->nau_Configured) {
            btAddErrorMsg(RETURN_OK, (STRPTR) libname, "A2DP stream closed by the headset.");
            stop_stream(nau, BTA2DPERR_NOSTREAM);
        }
        if(nau->nau_Source.state == A2DP_FAILED) {
            btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "AVDTP failed (error %ld).", (IPTR) nau->nau_Source.error);
            stop_stream(nau, BTA2DPERR_NOSTREAM);
        }
        if(nau->nau_Source.state != A2DP_FAILED && nau->nau_Source.state != A2DP_CLOSED) {
            post_signal_read(nau);
        }
    } else if(bch == nau->nau_AvRead) {
        if(err) {
            btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "AVRCP channel closed (%ld).", (IPTR) err);
            close_avrcp(nau);
            return;
        }
        AVRCPReceive(&nau->nau_Avrcp, nau->nau_AvBuffer, btGetChannelActual(bch));
        if(nau->nau_AvRead) {
            post_av_read(nau);
        }
        /* the headset just offered absolute volume: give it ours */
        if(nau->nau_Avrcp.absolute_volume && !nau->nau_AbsVolumeSet) {
            nau->nau_AbsVolumeSet = TRUE;
            btAddErrorMsg(RETURN_OK, (STRPTR) libname, "Headset takes AVRCP absolute volume.");
            apply_volume(nau, TRUE);
        }
    } else if(bch == nau->nau_MediaRead) {
        nau->nau_MediaOpening = FALSE;
        if(err) {
            btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "AVDTP media channel failed (%ld).", (IPTR) err);
            nau->nau_Source.state = A2DP_FAILED;
            return;
        }
        nau->nau_MediaOpen = TRUE;
        A2DPMediaOpen(&nau->nau_Source);
        note_state(nau);
        if(nau->nau_Source.state == A2DP_STREAMING) {
            start_stream(nau);
        }
    }
}

/* /// "sessions" */
static void free_channel(struct BTA2DPUnit *nau, struct BtChannel **pp)
{
    if(*pp) {
        btAbortChannel(*pp);
        btWaitChannel(*pp);
        btFreeChannel(*pp);
        *pp = NULL;
    }
}

/* Ends the AVDTP session: writes fail, every channel goes. The binding,
   its endpoints and the stream ring stay for the next session. */
static void end_session(struct BTA2DPUnit *nau)
{
    stop_stream(nau, BTA2DPERR_NOSTREAM);
    close_avrcp(nau);
    free_channel(nau, &nau->nau_MediaRead);
    free_channel(nau, &nau->nau_MediaWrite);
    free_channel(nau, &nau->nau_SigRead);
    free_channel(nau, &nau->nau_SigWrite);
    nau->nau_SigReadPending = nau->nau_MediaOpening = nau->nau_MediaOpen = FALSE;
    nau->nau_Source.state = A2DP_IDLE;
    nau->nau_Source.pending_signal = 0;
    nau->nau_LastState = A2DP_IDLE;
}

/* Opens signalling (connecting the headset if needed) and runs discovery,
   configuration, OPEN and START again. */
static BOOL start_session(struct BTA2DPUnit *nau)
{
    struct A2DPSource *src = &nau->nau_Source;
    if(!nau->nau_SigRead) {
        nau->nau_SigRead = btAllocChannel(nau->nau_Device, nau->nau_Port, nau->nau_SigEndpoint);
    }
    if(!nau->nau_SigWrite) {
        nau->nau_SigWrite = btAllocChannel(nau->nau_Device, nau->nau_Port, nau->nau_SigEndpoint);
    }
    if(!nau->nau_SigRead || !nau->nau_SigWrite) {
        return FALSE;
    }
    btSetAttrs(BGA_CHANNEL, nau->nau_SigRead, BCHA_AutoConnect, TRUE, TAG_END);
    btSetAttrs(BGA_CHANNEL, nau->nau_SigWrite, BCHA_AutoConnect, TRUE, TAG_END);
    memset(src, 0, sizeof(*src));
    nau->nau_LastState = A2DP_IDLE;
    nau->nau_Sessions++;
    /* the read is posted first so the sink's answers are never missed */
    post_signal_read(nau);
    if(A2DPStart(src, send_signal, nau)) {
        src->state = A2DP_FAILED;
    }
    note_state(nau);
    arm_timer(nau);
    return src->state != A2DP_FAILED;
}

static BOOL session_idle(struct BTA2DPUnit *nau)
{
    enum A2DPState st = nau->nau_Source.state;
    return st == A2DP_IDLE || st == A2DP_FAILED || st == A2DP_CLOSED;
}

/* The stack reports our headset's ACL link: down ends the session, up
   (the headset came back, or bonding reconnected it) starts a new one. */
static void handle_events(struct BTA2DPUnit *nau)
{
    struct Message *msg;
    while((msg = GetMsg(nau->nau_EventPort))) {
        IPTR ev = 0;
        APTR bd = NULL;
        btGetAttrs(BGA_EVENTNOTE, msg, BENA_EventID, &ev, BENA_Param1, &bd, TAG_END);
        ReplyMsg(msg);
        if(bd != nau->nau_Device) {
            continue;
        }
        if(ev == BEHMB_DEVICEDISCONNECTED) {
            nau->nau_LinkUp = FALSE;
            nau->nau_Retries = 0;
            nau->nau_RetryAt = 0;
            btAddErrorMsg(RETURN_OK, (STRPTR) libname, "Headset link down after %ld media packets; A2DP session ended.",
                          (IPTR) nau->nau_Packets);
            end_session(nau);
        } else if(ev == BEHMB_DEVICECONNECTED && session_idle(nau)) {
            nau->nau_LinkUp = TRUE;
            nau->nau_Retries = 0;
            nau->nau_RetryAt = 0;
            btAddErrorMsg(RETURN_OK, (STRPTR) libname, "Headset link up; starting A2DP session %ld.",
                          (IPTR) nau->nau_Sessions + 1);
            end_session(nau);
            if(!start_session(nau)) {
                btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "A2DP session could not start.");
            }
        }
    }
}
/* \\ */

#define RETRY_US 3000000
#define RETRY_MAX 5

/* Right after a reconnection the headset and the stack are busy too (SDP,
   the headset's own signalling): a failed session is tried again while the
   link stays up. */
static void schedule_retry(struct BTA2DPUnit *nau)
{
    if(nau->nau_Source.state == A2DP_FAILED && nau->nau_LinkUp && !nau->nau_RetryAt) {
        if(nau->nau_Retries < RETRY_MAX) {
            nau->nau_RetryAt = now_us(nau) + RETRY_US;
            arm_timer(nau);
        } else if(nau->nau_Retries == RETRY_MAX) {
            nau->nau_Retries++;
            btAddErrorMsg(RETURN_WARN, (STRPTR) libname, "A2DP gave up after %ld attempts; waiting for the headset to reconnect.",
                          (IPTR) RETRY_MAX);
        }
    }
}

static void check_retry(struct BTA2DPUnit *nau)
{
    if(nau->nau_RetryAt && now_us(nau) >= nau->nau_RetryAt) {
        nau->nau_RetryAt = 0;
        if(nau->nau_LinkUp && session_idle(nau)) {
            nau->nau_Retries++;
            btAddErrorMsg(RETURN_OK, (STRPTR) libname, "A2DP session retry %ld.", (IPTR) nau->nau_Retries);
            end_session(nau);
            start_session(nau);
        }
    }
}

AROS_UFH0(void, GM_UNIQUENAME(bA2DPTask))
{
    AROS_USERFUNC_INIT
    struct Task *thistask = FindTask(NULL);
    struct BTA2DPUnit *nau = thistask->tc_UserData;
    struct BtChannel *bch;
    ULONG sigs, mask;
    BOOL ok = FALSE;

    if((nau->nau_BluetoothBase = OpenLibrary("bluetooth.library", 1)) &&
       (nau->nau_Port = CreateMsgPort()) && (nau->nau_TimerPort = CreateMsgPort()) &&
       (nau->nau_Timer = (struct timerequest *) CreateIORequest(nau->nau_TimerPort, sizeof(struct timerequest))) &&
       !OpenDevice(TIMERNAME, UNIT_MICROHZ, &nau->nau_Timer->tr_node, 0) &&
       (nau->nau_EventPort = CreateMsgPort()) &&
       (nau->nau_EventHandler = btAddEventHandler(nau->nau_EventPort,
                                                  BEHMF_DEVICECONNECTED | BEHMF_DEVICEDISCONNECTED))) {
        nau->nau_SigRead = btAllocChannel(nau->nau_Device, nau->nau_Port, nau->nau_SigEndpoint);
        nau->nau_SigWrite = btAllocChannel(nau->nau_Device, nau->nau_Port, nau->nau_SigEndpoint);
        ok = nau->nau_SigRead && nau->nau_SigWrite;
    }
    if(ok && (nau->nau_InputPort = CreateMsgPort()) &&
       (nau->nau_Input = (struct IOStdReq *) CreateIORequest(nau->nau_InputPort, sizeof(struct IOStdReq))) &&
       OpenDevice("input.device", 0, (struct IORequest *) nau->nau_Input, 0)) {
        DeleteIORequest((struct IORequest *) nau->nau_Input);
        nau->nau_Input = NULL;
    }
    {
        /* tell the binder; it frees nau if we failed: publish last */
        struct Task *waiter = nau->nau_Waiter;
        ULONG wmask = nau->nau_WaiterMask;
        if(ok) {
            nau->nau_Task = thistask;
        } else {
            nau->nau_Status = -1;
        }
        Signal(waiter, wmask);
    }
    if(ok) {
        nau->nau_LinkUp = TRUE; /* binding happens on a connected device */
        start_session(nau);
        mask = (1UL << nau->nau_Port->mp_SigBit) | (1UL << nau->nau_TimerPort->mp_SigBit) |
               (1UL << nau->nau_EventPort->mp_SigBit) | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_E;
        for(;;) {
            sigs = Wait(mask);
            while((bch = (struct BtChannel *) GetMsg(nau->nau_Port))) {
                handle_channel(nau, bch);
            }
            handle_events(nau);
            schedule_retry(nau);
            if(sigs & SIGBREAKF_CTRL_E) {
                resume_stream(nau);
            }
            if(sigs & SIGBREAKF_CTRL_D) {
                /* bta2dp.device took a new volume */
                ULONG pct;
                ObtainSemaphore(&nau->nau_Base->nh_Lock);
                pct = nau->nau_Base->nh_Volume;
                ReleaseSemaphore(&nau->nau_Base->nh_Lock);
                save_volume(pct);
                apply_volume(nau, FALSE);
            }
            if(GetMsg(nau->nau_TimerPort)) {
                nau->nau_TimerPending = FALSE;
                if(nau->nau_Streaming) {
                    pump(nau);
                } else {
                    check_resume(nau);
                }
                check_signal(nau);
                check_retry(nau);
                schedule_retry(nau);
            }
            if(!nau->nau_TimerPending && (nau->nau_Streaming || nau->nau_ResumeWanted || negotiating(nau) ||
                                          nau->nau_RetryAt)) {
                arm_timer(nau);
            }
            if(sigs & SIGBREAKF_CTRL_C) {
                break;
            }
        }
        if(nau->nau_Source.state >= A2DP_OPENING && nau->nau_Source.state < A2DP_CLOSED &&
           !A2DPClose(&nau->nau_Source)) {
            /* let the sink answer CLOSE so its endpoint is free for the
               next binding; give up after a second */
            uint64_t until = now_us(nau) + 1000000;
            while(nau->nau_Source.state != A2DP_CLOSED && nau->nau_SigReadPending && now_us(nau) < until) {
                if((bch = (struct BtChannel *) GetMsg(nau->nau_Port))) {
                    handle_channel(nau, bch);
                } else {
                    btDelayMS(20);
                }
            }
            note_state(nau);
        }
        stop_stream(nau, IOERR_ABORTED);
        if(nau->nau_TimerPending) {
            AbortIO(&nau->nau_Timer->tr_node);
            WaitIO(&nau->nau_Timer->tr_node);
        }
        btAddErrorMsg(RETURN_OK, (STRPTR) libname, "A2DP sink released after %ld media packets (%ld send errors, %ld sessions).",
                      (IPTR) nau->nau_Packets, (IPTR) nau->nau_SendErrors, (IPTR) nau->nau_Sessions);
    }
    if(nau->nau_EventHandler) {
        struct Message *msg;
        btRemEventHandler(nau->nau_EventHandler);
        while((msg = GetMsg(nau->nau_EventPort))) {
            ReplyMsg(msg);
        }
    }
    if(nau->nau_EventPort) {
        DeleteMsgPort(nau->nau_EventPort);
    }
    close_avrcp(nau);
    if(nau->nau_Input) {
        CloseDevice((struct IORequest *) nau->nau_Input);
        DeleteIORequest((struct IORequest *) nau->nau_Input);
    }
    if(nau->nau_InputPort) {
        DeleteMsgPort(nau->nau_InputPort);
    }
        /* channels: abort what is pending, then free */
    {
        struct BtChannel *chans[4] = {nau->nau_SigRead, nau->nau_SigWrite, nau->nau_MediaRead, nau->nau_MediaWrite};
        for(int i = 0; i < 4; i++) {
            if(chans[i]) {
                btAbortChannel(chans[i]);
                btWaitChannel(chans[i]);
                btFreeChannel(chans[i]);
            }
        }
    }
    if(nau->nau_Timer) {
        if(nau->nau_Timer->tr_node.io_Device) {
            CloseDevice(&nau->nau_Timer->tr_node);
        }
        DeleteIORequest(&nau->nau_Timer->tr_node);
    }
    if(nau->nau_TimerPort) {
        DeleteMsgPort(nau->nau_TimerPort);
    }
    if(nau->nau_Port) {
        DeleteMsgPort(nau->nau_Port);
    }
    if(nau->nau_BluetoothBase) {
        CloseLibrary(nau->nau_BluetoothBase);
    }
    Forbid();
    {
        struct Task *waiter = nau->nau_Waiter;
        ULONG wmask = nau->nau_WaiterMask;
        if(ok) {
            nau->nau_Task = NULL;
            Signal(waiter, wmask);
        }
    }
    AROS_USERFUNC_EXIT
}
/* \\\ */
