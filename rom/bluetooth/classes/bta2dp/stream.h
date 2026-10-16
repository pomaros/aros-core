/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: A2DP media pump: PCM ring -> SBC frames -> RTP packets, released at
          the audio clock with a fixed lead. Pure; time is passed in.
*/
#ifndef BTA2DP_STREAM_H
#define BTA2DP_STREAM_H

#include <stdint.h>
#include "avdtp.h"

#define A2DP_RING_FRAMES 8192           /* PCM frames held: ~186 ms at 44.1 kHz */

struct A2DPStream {
    struct A2DPSource *source;
    struct SBCEncoder encoder;
    unsigned channels, frame_length, frame_samples, mtu, lead_us;
    int16_t ring[A2DP_RING_FRAMES * 2];
    unsigned head, count;               /* ring read index and fill, in PCM frames */
    uint8_t frames[15 * 520];           /* encoded frames awaiting a packet */
    unsigned pending;                   /* frames in frames[] */
    uint64_t start_us, sent_samples;
    int started;
    unsigned packets, underruns;
    unsigned gain;                      /* Q16 PCM gain, 65536 = unity */
};

/* Prepares the pump for the configured stream; mtu is the media channel's
 * peer MTU, lead_ms how far ahead of real time packets may go. */
int A2DPStreamInit(struct A2DPStream *st, struct A2DPSource *source, unsigned mtu, unsigned lead_ms);
/* Queues interleaved 16-bit PCM in the stream's channel count; returns the
 * frames accepted (fewer when the ring is full: the caller waits). */
unsigned A2DPStreamWrite(struct A2DPStream *st, const int16_t *pcm, unsigned frames);
/* Scales PCM written from now on: Q16, 65536 is unity, 0 is silence. */
void A2DPStreamSetGain(struct A2DPStream *st, unsigned gain);
/* Sends every packet due at now_us; returns packets sent or a negative
 * error from send. A late writer causes an underrun, counted, not filled. */
int A2DPStreamPump(struct A2DPStream *st, uint64_t now_us,
    int (*send)(void *, const uint8_t *, unsigned), void *context);
/* Microseconds until the next packet is due (0 if one is due now). */
uint64_t A2DPStreamNextDue(const struct A2DPStream *st, uint64_t now_us);

#endif
