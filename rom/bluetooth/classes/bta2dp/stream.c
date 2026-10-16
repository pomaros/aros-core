/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: A2DP media pump. Packets leave when the audio they carry is due,
          lead_us ahead of the sink's clock, so its jitter buffer neither
          starves nor overflows; the PCM ring's back-pressure paces AHI.
*/
#include <string.h>

#include "stream.h"

int A2DPStreamInit(struct A2DPStream *st, struct A2DPSource *source, unsigned mtu, unsigned lead_ms)
{
    memset(st, 0, sizeof(*st));
    if (!source || SBCInit(&st->encoder, &source->config)) return -1;
    st->source = source;
    st->channels = source->config.mode == SBC_MONO ? 1 : 2;
    st->frame_length = SBCFrameLength(&source->config);
    st->frame_samples = SBCFrameSamples(&source->config);
    if (st->frame_length > 520 || mtu < 13 + st->frame_length) return -1;
    st->mtu = mtu;
    st->lead_us = lead_ms * 1000;
    st->gain = 65536;
    return 0;
}

void A2DPStreamSetGain(struct A2DPStream *st, unsigned gain)
{
    st->gain = gain;
}

unsigned A2DPStreamWrite(struct A2DPStream *st, const int16_t *pcm, unsigned frames)
{
    unsigned room = A2DP_RING_FRAMES - st->count, done = 0;
    if (frames > room) frames = room;
    while (done < frames) {
        unsigned tail = (st->head + st->count) % A2DP_RING_FRAMES;
        unsigned run = A2DP_RING_FRAMES - tail;
        if (run > frames - done) run = frames - done;
        if (st->gain == 65536) {
            memcpy(st->ring + tail * st->channels, pcm + done * st->channels, run * st->channels * sizeof(int16_t));
        } else {
            const int16_t *in = pcm + done * st->channels;
            int16_t *out = st->ring + tail * st->channels;
            for (unsigned i = 0; i < run * st->channels; i++) {
                int32_t v = (int32_t)(((int64_t)in[i] * st->gain) >> 16);
                out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
        }
        st->count += run; done += run;
    }
    return done;
}

/* Encodes one SBC frame from the ring into frames[]. */
static int encode_one(struct A2DPStream *st)
{
    int16_t block[16 * 8 * 2];
    unsigned n = st->frame_samples;
    if (st->count < n || st->pending >= 15) return 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned at = (st->head + i) % A2DP_RING_FRAMES;
        for (unsigned ch = 0; ch < st->channels; ch++) block[i * st->channels + ch] = st->ring[at * st->channels + ch];
    }
    st->head = (st->head + n) % A2DP_RING_FRAMES;
    st->count -= n;
    if (SBCEncode(&st->encoder, block, st->frames + st->pending * st->frame_length, st->frame_length) != st->frame_length)
        return -1;
    st->pending++;
    return 1;
}

static uint64_t due_samples(const struct A2DPStream *st, uint64_t now_us)
{
    uint64_t rate = st->source->config.frequency;
    return (now_us - st->start_us + st->lead_us) * rate / 1000000;
}

int A2DPStreamPump(struct A2DPStream *st, uint64_t now_us,
    int (*send)(void *, const uint8_t *, unsigned), void *context)
{
    uint8_t packet[2048];
    unsigned per_packet = (st->mtu - 13) / st->frame_length, sent = 0;
    if (per_packet > 15) per_packet = 15;
    if (st->mtu > sizeof(packet)) per_packet = (sizeof(packet) - 13) / st->frame_length;
    if (!st->started) {
        /* The clock starts with the first full packet of audio. */
        if (st->count < per_packet * st->frame_samples) return 0;
        st->started = 1;
        st->start_us = now_us;
    }
    while (st->sent_samples + (uint64_t)per_packet * st->frame_samples <= due_samples(st, now_us)) {
        unsigned used, length;
        int rc;
        while (st->pending < per_packet && (rc = encode_one(st)) > 0) {}
        if (st->pending < per_packet) {
            /* Not enough audio: count it and restart the clock on refill. */
            st->underruns++;
            st->started = 0;
            st->sent_samples = 0;
            break;
        }
        length = A2DPPacket(st->source, st->frames, st->pending, st->frame_length, st->mtu, packet, &used);
        if (!length) return -1;
        if ((rc = send(context, packet, length))) return rc;
        memmove(st->frames, st->frames + used * st->frame_length, (st->pending - used) * st->frame_length);
        st->pending -= used;
        st->sent_samples += (uint64_t)used * st->frame_samples;
        st->packets++;
        sent++;
    }
    return (int)sent;
}

uint64_t A2DPStreamNextDue(const struct A2DPStream *st, uint64_t now_us)
{
    uint64_t rate = st->source->config.frequency, per_packet = (st->mtu - 13) / st->frame_length;
    uint64_t need, at;
    if (per_packet > 15) per_packet = 15;
    if (!st->started) return 0;
    need = st->sent_samples + per_packet * st->frame_samples;
    at = st->start_us + need * 1000000 / rate;
    at = at > st->lead_us ? at - st->lead_us : 0;
    return at > now_us ? at - now_us : 0;
}
