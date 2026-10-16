/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: AVDTP 1.3 source endpoint and A2DP SBC media packets. Pure protocol:
          the class supplies the L2CAP channels through callbacks.
*/
#ifndef BTA2DP_AVDTP_H
#define BTA2DP_AVDTP_H

#include <stdint.h>
#include "sbc.h"

#define AVDTP_DISCOVER          0x01
#define AVDTP_GET_CAPABILITIES  0x02
#define AVDTP_SET_CONFIGURATION 0x03
#define AVDTP_GET_CONFIGURATION 0x04
#define AVDTP_RECONFIGURE       0x05
#define AVDTP_OPEN              0x06
#define AVDTP_START             0x07
#define AVDTP_CLOSE             0x08
#define AVDTP_SUSPEND           0x09
#define AVDTP_ABORT             0x0a
#define AVDTP_GET_ALL_CAPABILITIES 0x0c
#define AVDTP_DELAYREPORT       0x0d

enum A2DPState {
    A2DP_IDLE,          /* signalling channel open, nothing sent */
    A2DP_DISCOVERING,
    A2DP_CAPABILITIES,
    A2DP_CONFIGURING,
    A2DP_OPENING,
    A2DP_MEDIA,         /* OPEN accepted: the class opens the media channel */
    A2DP_STARTING,
    A2DP_STREAMING,
    A2DP_SUSPENDED,
    A2DP_CLOSED,
    A2DP_FAILED
};

struct A2DPSource {
    enum A2DPState state;
    uint8_t local_seid, remote_seid, label;
    uint8_t pending_signal, pending_label;
    uint8_t candidates[8], ncandidates, candidate; /* free audio sink SEIDs */
    uint8_t sink_caps[4];           /* the sink's SBC codec information */
    unsigned get_all;               /* sink accepted GET_ALL_CAPABILITIES */
    unsigned delay_reports;         /* sink offered delay reporting */
    unsigned delay;                 /* last reported sink delay, 1/10 ms */
    unsigned error;                 /* AVDTP error code of a rejection */
    unsigned resuming;              /* START sent from SUSPENDED */
    struct SBCConfig config;
    uint16_t sequence;
    uint32_t timestamp;
    /* Sends one signalling packet; nonzero means it was not sent. */
    int (*send)(void *context, const uint8_t *data, unsigned length);
    void *context;
};

/* Starts discovery over an open signalling channel. */
int A2DPStart(struct A2DPSource *s, int (*send)(void *, const uint8_t *, unsigned), void *context);
/* Handles one signalling packet from the sink: our command's answer or the
 * sink's own command. Returns the new state. */
enum A2DPState A2DPReceive(struct A2DPSource *s, const uint8_t *data, unsigned length);
/* The media channel opened after OPEN: request START. */
int A2DPMediaOpen(struct A2DPSource *s);
/* A suspended stream has audio again: request START. */
int A2DPResume(struct A2DPSource *s);
/* Suspends or closes the stream from our side. */
int A2DPSuspend(struct A2DPSource *s);
int A2DPClose(struct A2DPSource *s);
/* Chooses the best SBC configuration the sink's codec information allows:
 * 44.1 then 48 kHz, joint stereo, 16 blocks, 8 subbands, loudness, bitpool
 * 53 (51 at 48 kHz) clamped to the sink's range. Zero on success. */
int A2DPChooseConfig(const uint8_t caps[4], struct SBCConfig *out);
/* Builds one media packet: RTP header, SBC header and as many encoded frames
 * as fit in mtu (at most 15). frames points to n frames of frame_length
 * bytes; out holds at least mtu bytes. Returns the packet length and stores
 * the frames used. */
unsigned A2DPPacket(struct A2DPSource *s, const uint8_t *frames, unsigned n,
    unsigned frame_length, unsigned mtu, uint8_t *out, unsigned *used);

#endif
