/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: BTA2DPTest - checks bta2dp.device and the bta2dp.audio AHI mode.
      BTA2DPTest QUERY            stream state, then one 10 ms write
      BTA2DPTest AHI <seconds>    441 Hz sine through AHI mode 0x00460001;
                                  counts PlayerFunc calls against the clock
      BTA2DPTest WAV <file>       plays a 16-bit stereo 44.1 kHz WAV once
                                  through the same AHI mode
      BTA2DPTest VOLUME [percent] reads or sets the output volume
*/
#include <devices/ahi.h>
#include <devices/bta2dp.h>
#include <devices/timer.h>
#include <exec/memory.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/ahi.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MODE 0x00460001
#define PLAYERFREQ 50

struct Library *AHIBase;
struct Device *TimerBase;
static volatile ULONG ticks;

static ULONG player(struct Hook *hook, APTR actrl, APTR unused)
{
    (void) hook; (void) actrl; (void) unused;
    ticks++;
    return 0;
}

static double now(void)
{
    struct timeval tv;
    GetSysTime(&tv);
    return tv.tv_secs + tv.tv_micro / 1e6;
}

static int query(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *io = port ? (struct IOStdReq *) CreateIORequest(port, sizeof(*io)) : NULL;
    static WORD pcm[441 * 2];
    int rc = 20;
    if(!io || OpenDevice("bta2dp.device", 0, (struct IORequest *) io, 0)) {
        printf("[a2dptest] open=fail\n");
    } else {
        io->io_Command = BTA2DPCMD_QUERY;
        DoIO((struct IORequest *) io);
        printf("[a2dptest] query error=%d rate=%lu channels=%lu queued=%lu\n", io->io_Error,
               (unsigned long) io->io_Actual, (unsigned long) io->io_Offset, (unsigned long) io->io_Length);
        io->io_Command = CMD_WRITE;
        io->io_Data = pcm;
        io->io_Length = sizeof(pcm);
        DoIO((struct IORequest *) io);
        printf("[a2dptest] write error=%d actual=%lu\n", io->io_Error, (unsigned long) io->io_Actual);
        rc = io->io_Error == 0 || io->io_Error == BTA2DPERR_NOSTREAM ? 0 : 10;
        CloseDevice((struct IORequest *) io);
    }
    if(io) DeleteIORequest((struct IORequest *) io);
    if(port) DeleteMsgPort(port);
    return rc;
}

static int volume(int argc, char **argv)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *io = port ? (struct IOStdReq *) CreateIORequest(port, sizeof(*io)) : NULL;
    int rc = 20;
    if(!io || OpenDevice("bta2dp.device", 0, (struct IORequest *) io, 0)) {
        printf("[a2dptest] open=fail (bta2dp.class not loaded)\n");
    } else {
        if(argc == 3) {
            char text[8];
            ULONG pct = strtoul(argv[2], NULL, 10);
            io->io_Command = BTA2DPCMD_SETVOLUME;
            io->io_Offset = pct;
            DoIO((struct IORequest *) io);
            /* the class saves it only while a headset is bound */
            snprintf(text, sizeof(text), "%lu", (unsigned long) (pct > 100 ? 100 : pct));
            SetVar("Sys/AudioVolume", text, -1, GVF_GLOBAL_ONLY);
        }
        io->io_Command = BTA2DPCMD_GETVOLUME;
        DoIO((struct IORequest *) io);
        printf("[a2dptest] volume=%lu%% headset-absolute=%lu\n", (unsigned long) io->io_Offset,
               (unsigned long) io->io_Actual);
        rc = io->io_Error ? 10 : 0;
        CloseDevice((struct IORequest *) io);
    }
    if(io) DeleteIORequest((struct IORequest *) io);
    if(port) DeleteMsgPort(port);
    return rc;
}

static int ahi(ULONG seconds)
{
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req = port ? (struct AHIRequest *) CreateIORequest(port, sizeof(*req)) : NULL;
    struct Hook hook = { { NULL, NULL }, (HOOKFUNC) player, NULL, NULL };
    struct AHIAudioCtrl *actrl = NULL;
    struct AHISampleInfo sample;
    WORD *sine = AllocVec(100 * 2 * sizeof(WORD), MEMF_PUBLIC);
    double began, elapsed;
    int rc = 20;
    char name[64] = "";

    if(!req || !sine) {
        printf("[a2dptest] FAIL memory\n");
        goto out;
    }
    req->ahir_Version = 4;
    if(OpenDevice(AHINAME, AHI_NO_UNIT, (struct IORequest *) req, 0)) {
        printf("[a2dptest] FAIL ahi.device\n");
        req->ahir_Std.io_Device = NULL;
        goto out;
    }
    AHIBase = (struct Library *) req->ahir_Std.io_Device;
    if(!AHI_GetAudioAttrs(MODE, NULL, AHIDB_Name, (IPTR) name, AHIDB_BufferLen, sizeof(name), TAG_DONE)) {
        printf("[a2dptest] FAIL mode 0x%08x absent (AddAudioModes the BTA2DP mode file)\n", MODE);
        goto out;
    }
    printf("[a2dptest] mode 0x%08x %s\n", MODE, name);
    for(int i = 0; i < 100; i++) {
        WORD v = (WORD) (16000 * sin(2 * M_PI * i / 100.0));
        sine[i * 2] = sine[i * 2 + 1] = v;
    }
    actrl = AHI_AllocAudio(AHIA_AudioID, MODE, AHIA_MixFreq, 44100, AHIA_Channels, 1, AHIA_Sounds, 1,
                           AHIA_PlayerFunc, (IPTR) &hook, AHIA_PlayerFreq, PLAYERFREQ << 16, TAG_DONE);
    if(!actrl) {
        printf("[a2dptest] FAIL AHI_AllocAudio\n");
        goto out;
    }
    sample.ahisi_Type = AHIST_S16S;
    sample.ahisi_Address = sine;
    sample.ahisi_Length = 100;
    if(AHI_LoadSound(0, AHIST_SAMPLE, &sample, actrl) != AHIE_OK) {
        printf("[a2dptest] FAIL AHI_LoadSound\n");
        goto out;
    }
    AHI_Play(actrl, AHIP_BeginChannel, 0, AHIP_Sound, 0, AHIP_Freq, 44100, AHIP_Vol, 0x10000,
             AHIP_Pan, 0x8000, AHIP_EndChannel, 0, TAG_DONE);
    ticks = 0;
    began = now();
    if(AHI_ControlAudio(actrl, AHIC_Play, TRUE, TAG_DONE) != AHIE_OK) {
        printf("[a2dptest] FAIL AHIC_Play\n");
        goto out;
    }
    Delay(seconds * 50);
    AHI_ControlAudio(actrl, AHIC_Play, FALSE, TAG_DONE);
    elapsed = now() - began;
    {
        double expected = elapsed * PLAYERFREQ, ratio = expected > 0 ? ticks / expected : 0;
        printf("[a2dptest] played %.2f s player=%lu expected=%.0f ratio=%.3f\n", elapsed,
               (unsigned long) ticks, expected, ratio);
        rc = ratio > 0.9 && ratio < 1.1 ? 0 : 10;
        printf("[a2dptest] %s\n", rc ? "FAIL pacing" : "PASS");
    }
out:
    if(actrl) AHI_FreeAudio(actrl);
    if(req && req->ahir_Std.io_Device) CloseDevice((struct IORequest *) req);
    if(req) DeleteIORequest((struct IORequest *) req);
    if(port) DeleteMsgPort(port);
    FreeVec(sine);
    return rc;
}

static ULONG le32(const UBYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (ULONG) p[3] << 24; }
static UWORD le16(const UBYTE *p) { return p[0] | p[1] << 8; }

/* Loads the PCM of a canonical WAV; NULL unless 16-bit stereo 44.1 kHz. */
static UBYTE *load_wav(const char *path, ULONG *frames)
{
    BPTR f = Open(path, MODE_OLDFILE);
    UBYTE hdr[12], ck[8], fmt[16];
    UBYTE *pcm = NULL;
    BOOL format = FALSE;
    *frames = 0;
    if(!f) {
        printf("[a2dptest] FAIL open %s\n", path);
        return NULL;
    }
    if(Read(f, hdr, 12) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        printf("[a2dptest] FAIL not a WAV file\n");
        Close(f);
        return NULL;
    }
    while(Read(f, ck, 8) == 8) {
        ULONG len = le32(ck + 4);
        if(!memcmp(ck, "fmt ", 4) && len >= 16) {
            Read(f, fmt, 16);
            if(len > 16) Seek(f, len - 16 + (len & 1), OFFSET_CURRENT);
            format = le16(fmt) == 1 && le16(fmt + 2) == 2 && le32(fmt + 4) == 44100 && le16(fmt + 14) == 16;
        } else if(!memcmp(ck, "data", 4)) {
            if(!format) {
                printf("[a2dptest] FAIL need PCM 16-bit stereo 44100 Hz\n");
                break;
            }
            if((pcm = AllocVec(len, MEMF_PUBLIC)) && Read(f, pcm, len) == (LONG) len) {
                *frames = len / 4;
            } else {
                printf("[a2dptest] FAIL reading %lu bytes\n", (unsigned long) len);
                FreeVec(pcm);
                pcm = NULL;
            }
            break;
        } else {
            Seek(f, len + (len & 1), OFFSET_CURRENT);
        }
    }
    Close(f);
    return pcm;
}

static int wav(const char *path)
{
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req = port ? (struct AHIRequest *) CreateIORequest(port, sizeof(*req)) : NULL;
    struct AHIAudioCtrl *actrl = NULL;
    struct AHISampleInfo sample;
    ULONG frames;
    UBYTE *pcm = load_wav(path, &frames);
    double began;
    int rc = 20;

    if(!pcm || !req) goto out;
    req->ahir_Version = 4;
    if(OpenDevice(AHINAME, AHI_NO_UNIT, (struct IORequest *) req, 0)) {
        printf("[a2dptest] FAIL ahi.device\n");
        req->ahir_Std.io_Device = NULL;
        goto out;
    }
    AHIBase = (struct Library *) req->ahir_Std.io_Device;
    actrl = AHI_AllocAudio(AHIA_AudioID, MODE, AHIA_MixFreq, 44100, AHIA_Channels, 1, AHIA_Sounds, 1, TAG_DONE);
    if(!actrl) {
        printf("[a2dptest] FAIL AHI_AllocAudio\n");
        goto out;
    }
    sample.ahisi_Type = AHIST_S16S;
    sample.ahisi_Address = pcm;
    sample.ahisi_Length = frames;
    if(AHI_LoadSound(0, AHIST_SAMPLE, &sample, actrl) != AHIE_OK) {
        printf("[a2dptest] FAIL AHI_LoadSound\n");
        goto out;
    }
    AHI_Play(actrl, AHIP_BeginChannel, 0, AHIP_Sound, 0, AHIP_LoopSound, AHI_NOSOUND, AHIP_Freq, 44100,
             AHIP_Vol, 0x10000, AHIP_Pan, 0x8000, AHIP_EndChannel, 0, TAG_DONE);
    printf("[a2dptest] playing %s: %lu frames, %lu s\n", path, (unsigned long) frames, (unsigned long) (frames / 44100));
    fflush(stdout);
    began = now();
    if(AHI_ControlAudio(actrl, AHIC_Play, TRUE, TAG_DONE) != AHIE_OK) {
        printf("[a2dptest] FAIL AHIC_Play\n");
        goto out;
    }
    Delay(frames / 882 + 25);
    AHI_ControlAudio(actrl, AHIC_Play, FALSE, TAG_DONE);
    printf("[a2dptest] PASS played %.2f s\n", now() - began);
    rc = 0;
out:
    if(actrl) AHI_FreeAudio(actrl);
    if(req && req->ahir_Std.io_Device) CloseDevice((struct IORequest *) req);
    if(req) DeleteIORequest((struct IORequest *) req);
    if(port) DeleteMsgPort(port);
    FreeVec(pcm);
    return rc;
}

int main(int argc, char **argv)
{
    struct timerequest tr;
    int rc;
    memset(&tr, 0, sizeof(tr));
    if(OpenDevice(TIMERNAME, UNIT_MICROHZ, &tr.tr_node, 0)) return 20;
    TimerBase = tr.tr_node.io_Device;
    if(argc == 2 && !strcmp(argv[1], "QUERY")) rc = query();
    else if(argc == 3 && !strcmp(argv[1], "AHI")) rc = ahi(strtoul(argv[2], NULL, 10));
    else if(argc == 3 && !strcmp(argv[1], "WAV")) rc = wav(argv[2]);
    else if((argc == 2 || argc == 3) && !strcmp(argv[1], "VOLUME")) rc = volume(argc, argv);
    else { puts("usage: BTA2DPTest QUERY | AHI <seconds> | WAV <file> | VOLUME [percent]"); rc = 10; }
    CloseDevice(&tr.tr_node);
    return rc;
}
