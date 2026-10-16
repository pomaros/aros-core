/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: AVRCP 1.6 over AVCTP for an A2DP source: as controller it sets the
          headset's absolute volume and follows its volume changes; as
          target it answers the headset's buttons (play, pause, skip,
          volume) and reports the playback status. Pure; the class owns the
          L2CAP channel (PSM 0x0017) and passes frames in and out.
*/
#ifndef BTA2DP_AVRCP_H
#define BTA2DP_AVRCP_H

#include <stdint.h>

#define AVRCP_PSM               0x0017

/* PASS THROUGH operation ids (AV/C panel subunit) */
#define AVRCP_OP_VOLUME_UP      0x41
#define AVRCP_OP_VOLUME_DOWN    0x42
#define AVRCP_OP_MUTE           0x43
#define AVRCP_OP_PLAY           0x44
#define AVRCP_OP_STOP           0x45
#define AVRCP_OP_PAUSE          0x46
#define AVRCP_OP_REWIND         0x48
#define AVRCP_OP_FAST_FORWARD   0x49
#define AVRCP_OP_FORWARD        0x4b
#define AVRCP_OP_BACKWARD       0x4c

#define AVRCP_VOLUME_MAX        0x7f

struct AVRCPSession {
    /* filled in by the class */
    int (*send)(void *context, const uint8_t *data, unsigned length);
    void *context;
    /* a button on the headset; pressed is 1 on press, 0 on release */
    void (*button)(void *context, uint8_t operation, int pressed);
    /* the headset changed its own volume (0..127) */
    void (*volume)(void *context, uint8_t volume);

    uint8_t label;              /* next transaction label for our commands */
    uint8_t absolute_volume;    /* the headset offered EVENT_VOLUME_CHANGED */
    uint8_t volume_label;       /* label of our pending volume registration */
    uint8_t volume_registered;
    uint8_t current_volume;     /* last volume set or reported */
    uint8_t playing;            /* playback status we report */
    uint8_t status_label;       /* the headset's playback-status registration */
    uint8_t status_registered;
    unsigned commands, responses, ignored;
};

/* Starts the controller side: asks the headset which events it reports. */
int AVRCPStart(struct AVRCPSession *s);
/* One AVCTP frame from the headset: our answers or its commands. */
void AVRCPReceive(struct AVRCPSession *s, const uint8_t *data, unsigned length);
/* Sets the headset's absolute volume (0..127); -1 if it has none. */
int AVRCPSetVolume(struct AVRCPSession *s, uint8_t volume);
/* Our playback started or stopped: tells a headset that asked. */
int AVRCPSetPlaying(struct AVRCPSession *s, int playing);

#endif
