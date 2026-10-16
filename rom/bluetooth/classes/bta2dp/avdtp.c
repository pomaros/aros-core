/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: AVDTP 1.3 source endpoint (signalling procedures, section 8) and the
          A2DP SBC media payload (A2DP 1.3, 4.3), written from the
          specifications. One stream endpoint: SEID 1, audio, source, SBC.
*/
#include <string.h>

#include "avdtp.h"

#define MSG_COMMAND 0
#define MSG_GENERAL_REJECT 1
#define MSG_ACCEPT 2
#define MSG_REJECT 3

#define CAT_MEDIA_TRANSPORT 0x01
#define CAT_MEDIA_CODEC 0x07
#define CAT_DELAY_REPORTING 0x08

#define ERR_BAD_ACP_SEID 0x12
#define ERR_SEP_IN_USE 0x13
#define ERR_BAD_STATE 0x31
#define ERR_INVALID_CAPABILITIES 0x29

/* Our codec capabilities: every rate, mode, block length, subband count
 * and allocation, bitpool 2..53. */
static const uint8_t own_caps[4] = {0xff, 0xff, 2, 53};

static int send_command(struct A2DPSource *s, uint8_t signal, const uint8_t *body, unsigned length)
{
    uint8_t packet[32];
    if (length + 2 > sizeof(packet)) return -1;
    s->label = (s->label + 1) & 15;
    packet[0] = (uint8_t)(s->label << 4 | MSG_COMMAND);
    packet[1] = signal;
    if (length) memcpy(packet + 2, body, length);
    s->pending_signal = signal;
    s->pending_label = s->label;
    return s->send(s->context, packet, length + 2);
}

static void respond(struct A2DPSource *s, uint8_t label, uint8_t type, uint8_t signal,
    const uint8_t *body, unsigned length)
{
    uint8_t packet[32];
    if (length + 2 > sizeof(packet)) return;
    packet[0] = (uint8_t)(label << 4 | type);
    packet[1] = signal;
    if (length) memcpy(packet + 2, body, length);
    s->send(s->context, packet, length + 2);
}

static void reject(struct A2DPSource *s, uint8_t label, uint8_t signal, uint8_t error)
{
    respond(s, label, MSG_REJECT, signal, &error, 1);
}

static enum A2DPState fail(struct A2DPSource *s, unsigned error)
{
    s->error = error;
    return s->state = A2DP_FAILED;
}

int A2DPChooseConfig(const uint8_t caps[4], struct SBCConfig *c)
{
    static const struct { uint8_t bit; unsigned hz, pool; } rates[] = {
        {0x20, 44100, 53}, {0x10, 48000, 51}, {0x40, 32000, 53}, {0x80, 16000, 53}};
    static const struct { uint8_t bit; unsigned mode; } modes[] = {
        {0x01, SBC_JOINT}, {0x02, SBC_STEREO}, {0x04, SBC_DUAL}, {0x08, SBC_MONO}};
    static const struct { uint8_t bit; unsigned blocks; } blocks[] = {
        {0x10, 16}, {0x20, 12}, {0x40, 8}, {0x80, 4}};
    unsigned i, pool;
    memset(c, 0, sizeof(*c));
    for (i = 0; i < 4 && !(caps[0] & rates[i].bit); i++) {}
    if (i == 4) return -1;
    c->frequency = rates[i].hz; pool = rates[i].pool;
    for (i = 0; i < 4 && !(caps[0] & modes[i].bit); i++) {}
    if (i == 4) return -1;
    c->mode = modes[i].mode;
    for (i = 0; i < 4 && !(caps[1] & blocks[i].bit); i++) {}
    if (i == 4) return -1;
    c->blocks = blocks[i].blocks;
    if (caps[1] & 0x04) c->subbands = 8;
    else if (caps[1] & 0x08) c->subbands = 4;
    else return -1;
    if (caps[1] & 0x01) c->allocation = SBC_LOUDNESS;
    else if (caps[1] & 0x02) c->allocation = SBC_SNR;
    else return -1;
    if (caps[2] < 2 || caps[3] < caps[2]) return -1;
    if (c->mode == SBC_MONO || c->mode == SBC_DUAL) pool = pool * 6 / 10;
    if (pool > caps[3]) pool = caps[3];
    if (pool < caps[2]) pool = caps[2];
    c->bitpool = pool;
    return SBCFrameLength(c) ? 0 : -1;
}

static void encode_config(const struct SBCConfig *c, uint8_t minpool, uint8_t out[4])
{
    out[0] = (uint8_t)((c->frequency == 16000 ? 0x80 : c->frequency == 32000 ? 0x40 :
              c->frequency == 44100 ? 0x20 : 0x10) |
             (c->mode == SBC_MONO ? 0x08 : c->mode == SBC_DUAL ? 0x04 : c->mode == SBC_STEREO ? 0x02 : 0x01));
    out[1] = (uint8_t)((c->blocks == 4 ? 0x80 : c->blocks == 8 ? 0x40 : c->blocks == 12 ? 0x20 : 0x10) |
             (c->subbands == 4 ? 0x08 : 0x04) | (c->allocation == SBC_SNR ? 0x02 : 0x01));
    out[2] = minpool;
    out[3] = (uint8_t)c->bitpool;
}

/* Exactly one bit per field: a configuration, not a capability set. */
static int decode_config(const uint8_t in[4], struct SBCConfig *c)
{
    uint8_t one[4] = {in[0], in[1], in[2], in[3]};
    if (__builtin_popcount(in[0] & 0xf0) != 1 || __builtin_popcount(in[0] & 0x0f) != 1 ||
        __builtin_popcount(in[1] & 0xf0) != 1 || __builtin_popcount(in[1] & 0x0c) != 1 ||
        __builtin_popcount(in[1] & 0x03) != 1) return -1;
    one[2] = one[3] = in[3];
    if (A2DPChooseConfig(one, c)) return -1;
    c->bitpool = in[3];
    return SBCFrameLength(c) ? 0 : -1;
}

static unsigned own_capabilities(uint8_t *out, int all)
{
    unsigned n = 0;
    out[n++] = CAT_MEDIA_TRANSPORT; out[n++] = 0;
    out[n++] = CAT_MEDIA_CODEC; out[n++] = 6; out[n++] = 0x00; out[n++] = 0x00;
    memcpy(out + n, own_caps, 4); n += 4;
    if (all) { out[n++] = CAT_DELAY_REPORTING; out[n++] = 0; }
    return n;
}

static int request_capabilities(struct A2DPSource *s)
{
    uint8_t seid = (uint8_t)(s->remote_seid << 2);
    s->state = A2DP_CAPABILITIES;
    return send_command(s, s->get_all ? AVDTP_GET_ALL_CAPABILITIES : AVDTP_GET_CAPABILITIES, &seid, 1);
}

int A2DPStart(struct A2DPSource *s, int (*send)(void *, const uint8_t *, unsigned), void *context)
{
    memset(s, 0, sizeof(*s));
    s->send = send; s->context = context;
    s->local_seid = 1;
    s->get_all = 1;
    s->state = A2DP_DISCOVERING;
    return send_command(s, AVDTP_DISCOVER, NULL, 0);
}

/* The sink's answer to our pending command. */
static enum A2DPState answer(struct A2DPSource *s, uint8_t signal, uint8_t type, const uint8_t *p, unsigned n)
{
    if (type != MSG_ACCEPT) {
        /* A 1.0-1.2 sink does not know GET_ALL_CAPABILITIES. */
        if (signal == AVDTP_GET_ALL_CAPABILITIES) {
            s->get_all = 0;
            return request_capabilities(s) ? fail(s, 0) : s->state;
        }
        /* A refused resume leaves the stream suspended, not broken. */
        if (signal == AVDTP_START && s->resuming) {
            s->resuming = 0;
            s->error = n > 2 ? p[n - 1] : 0;
            return s->state = A2DP_SUSPENDED;
        }
        return fail(s, n > 2 ? p[n - 1] : 0);
    }
    switch (signal) {
    case AVDTP_DISCOVER: {
        /* Every free audio sink endpoint is a candidate; the first with
         * SBC capabilities is configured (a sink may list AAC first). */
        s->ncandidates = 0;
        for (unsigned i = 2; i + 1 < n && s->ncandidates < 8; i += 2) {
            uint8_t seid = p[i] >> 2, used = p[i] & 2, media = p[i + 1] >> 4, sink = p[i + 1] & 8;
            if (seid && !used && media == 0 && sink) s->candidates[s->ncandidates++] = seid;
        }
        if (!s->ncandidates) return fail(s, ERR_SEP_IN_USE);
        s->candidate = 0;
        s->remote_seid = s->candidates[0];
        return request_capabilities(s) ? fail(s, 0) : s->state;
    }
    case AVDTP_GET_CAPABILITIES:
    case AVDTP_GET_ALL_CAPABILITIES: {
        unsigned i = 2, sbc = 0;
        uint8_t body[16];
        while (i + 1 < n) {
            uint8_t cat = p[i], len = p[i + 1];
            if (i + 2 + len > n) return fail(s, ERR_INVALID_CAPABILITIES);
            if (cat == CAT_MEDIA_CODEC && len >= 6 && (p[i + 2] >> 4) == 0 && p[i + 3] == 0) {
                memcpy(s->sink_caps, p + i + 4, 4); sbc = 1;
            }
            if (cat == CAT_DELAY_REPORTING) s->delay_reports = 1;
            i += 2 + len;
        }
        if (!sbc || A2DPChooseConfig(s->sink_caps, &s->config)) {
            if (++s->candidate >= s->ncandidates) return fail(s, ERR_INVALID_CAPABILITIES);
            s->remote_seid = s->candidates[s->candidate];
            s->delay_reports = 0;
            return request_capabilities(s) ? fail(s, 0) : s->state;
        }
        body[0] = (uint8_t)(s->remote_seid << 2);
        body[1] = (uint8_t)(s->local_seid << 2);
        body[2] = CAT_MEDIA_TRANSPORT; body[3] = 0;
        body[4] = CAT_MEDIA_CODEC; body[5] = 6; body[6] = 0x00; body[7] = 0x00;
        encode_config(&s->config, s->sink_caps[2], body + 8);
        n = 12;
        if (s->delay_reports) { body[n++] = CAT_DELAY_REPORTING; body[n++] = 0; }
        s->state = A2DP_CONFIGURING;
        return send_command(s, AVDTP_SET_CONFIGURATION, body, n) ? fail(s, 0) : s->state;
    }
    case AVDTP_SET_CONFIGURATION: {
        uint8_t seid = (uint8_t)(s->remote_seid << 2);
        s->state = A2DP_OPENING;
        return send_command(s, AVDTP_OPEN, &seid, 1) ? fail(s, 0) : s->state;
    }
    case AVDTP_OPEN: return s->state = A2DP_MEDIA;
    case AVDTP_START: s->resuming = 0; return s->state = A2DP_STREAMING;
    case AVDTP_SUSPEND: return s->state = A2DP_SUSPENDED;
    case AVDTP_CLOSE:
    case AVDTP_ABORT: return s->state = A2DP_CLOSED;
    }
    return s->state;
}

/* A command from the sink to our stream endpoint. */
static enum A2DPState command(struct A2DPSource *s, uint8_t label, uint8_t signal, const uint8_t *p, unsigned n)
{
    uint8_t body[16], seid = n > 2 ? p[2] >> 2 : 0;
    switch (signal) {
    case AVDTP_DISCOVER:
        body[0] = (uint8_t)(s->local_seid << 2 | (s->state >= A2DP_CONFIGURING && s->state < A2DP_CLOSED ? 2 : 0));
        body[1] = 0x00;                     /* audio, source */
        respond(s, label, MSG_ACCEPT, signal, body, 2);
        break;
    case AVDTP_GET_CAPABILITIES:
    case AVDTP_GET_ALL_CAPABILITIES:
        if (seid != s->local_seid) { reject(s, label, signal, ERR_BAD_ACP_SEID); break; }
        respond(s, label, MSG_ACCEPT, signal, body, own_capabilities(body, signal == AVDTP_GET_ALL_CAPABILITIES));
        break;
    case AVDTP_SET_CONFIGURATION: {
        /* A sink may configure us itself, typically when it reconnects. */
        unsigned i = 4, ok = 0;
        uint8_t error[2] = {0, ERR_BAD_STATE};
        if (seid != s->local_seid) { reject(s, label, signal, ERR_BAD_ACP_SEID); break; }
        if (s->state >= A2DP_CONFIGURING && s->state < A2DP_CLOSED) {
            error[1] = ERR_SEP_IN_USE; respond(s, label, MSG_REJECT, signal, error, 2); break;
        }
        while (i + 1 < n) {
            uint8_t cat = p[i], len = p[i + 1];
            if (i + 2 + len > n) break;
            if (cat == CAT_MEDIA_CODEC && len == 6 && p[i + 3] == 0 && !decode_config(p + i + 4, &s->config)) ok = 1;
            if (cat == CAT_DELAY_REPORTING) s->delay_reports = 1;
            i += 2 + len;
        }
        if (!ok) { error[0] = CAT_MEDIA_CODEC; error[1] = ERR_INVALID_CAPABILITIES; respond(s, label, MSG_REJECT, signal, error, 2); break; }
        s->remote_seid = p[3] >> 2;
        s->state = A2DP_OPENING;
        respond(s, label, MSG_ACCEPT, signal, NULL, 0);
        break;
    }
    case AVDTP_GET_CONFIGURATION:
        if (seid != s->local_seid || s->state < A2DP_OPENING || s->state >= A2DP_CLOSED) {
            reject(s, label, signal, seid != s->local_seid ? ERR_BAD_ACP_SEID : ERR_BAD_STATE); break;
        }
        body[0] = CAT_MEDIA_TRANSPORT; body[1] = 0;
        body[2] = CAT_MEDIA_CODEC; body[3] = 6; body[4] = 0; body[5] = 0;
        encode_config(&s->config, 2, body + 6);
        respond(s, label, MSG_ACCEPT, signal, body, 10);
        break;
    case AVDTP_OPEN:
        if (s->state != A2DP_OPENING) { reject(s, label, signal, ERR_BAD_STATE); break; }
        s->state = A2DP_MEDIA;
        respond(s, label, MSG_ACCEPT, signal, NULL, 0);
        break;
    case AVDTP_START:
        if (s->state != A2DP_MEDIA && s->state != A2DP_SUSPENDED) {
            body[0] = p[2]; body[1] = ERR_BAD_STATE; respond(s, label, MSG_REJECT, signal, body, 2); break;
        }
        s->state = A2DP_STREAMING;
        respond(s, label, MSG_ACCEPT, signal, NULL, 0);
        break;
    case AVDTP_SUSPEND:
        if (s->state == A2DP_STREAMING) s->state = A2DP_SUSPENDED;
        respond(s, label, MSG_ACCEPT, signal, NULL, 0);
        break;
    case AVDTP_CLOSE:
    case AVDTP_ABORT:
        s->state = A2DP_CLOSED;
        respond(s, label, MSG_ACCEPT, signal, NULL, 0);
        break;
    case AVDTP_DELAYREPORT:
        if (n >= 5) s->delay = (unsigned)p[3] << 8 | p[4];
        respond(s, label, MSG_ACCEPT, signal, NULL, 0);
        break;
    default:
        respond(s, label, MSG_GENERAL_REJECT, signal, NULL, 0);
        break;
    }
    return s->state;
}

enum A2DPState A2DPReceive(struct A2DPSource *s, const uint8_t *p, unsigned n)
{
    uint8_t label, type, signal;
    if (!s || !p || n < 2) return s ? s->state : A2DP_FAILED;
    label = p[0] >> 4; type = p[0] & 3; signal = p[1] & 0x3f;
    if ((p[0] >> 2) & 3) return s->state;   /* fragmented signalling: unused by our messages */
    if (type == MSG_COMMAND) return command(s, label, signal, p, n);
    if (label != s->pending_label || signal != s->pending_signal) return s->state;
    s->pending_signal = 0;
    return answer(s, signal, type, p, n);
}

int A2DPMediaOpen(struct A2DPSource *s)
{
    uint8_t seid = (uint8_t)(s->remote_seid << 2);
    if (s->state != A2DP_MEDIA) return -1;
    s->state = A2DP_STARTING;
    return send_command(s, AVDTP_START, &seid, 1);
}

int A2DPResume(struct A2DPSource *s)
{
    uint8_t seid = (uint8_t)(s->remote_seid << 2);
    if (s->state != A2DP_SUSPENDED) return -1;
    s->state = A2DP_STARTING;
    s->resuming = 1;
    return send_command(s, AVDTP_START, &seid, 1);
}

int A2DPSuspend(struct A2DPSource *s)
{
    uint8_t seid = (uint8_t)(s->remote_seid << 2);
    if (s->state != A2DP_STREAMING) return -1;
    return send_command(s, AVDTP_SUSPEND, &seid, 1);
}

int A2DPClose(struct A2DPSource *s)
{
    uint8_t seid = (uint8_t)(s->remote_seid << 2);
    if (s->state < A2DP_OPENING || s->state >= A2DP_CLOSED) return -1;
    return send_command(s, AVDTP_CLOSE, &seid, 1);
}

unsigned A2DPPacket(struct A2DPSource *s, const uint8_t *frames, unsigned n,
    unsigned frame_length, unsigned mtu, uint8_t *out, unsigned *used)
{
    unsigned k;
    *used = 0;
    if (!frame_length || mtu < 13 + frame_length) return 0;
    k = (mtu - 13) / frame_length;
    if (k > 15) k = 15;
    if (k > n) k = n;
    if (!k) return 0;
    out[0] = 0x80;                          /* RTP version 2 */
    out[1] = 0x60;                          /* dynamic payload type 96 */
    out[2] = (uint8_t)(s->sequence >> 8); out[3] = (uint8_t)s->sequence;
    out[4] = (uint8_t)(s->timestamp >> 24); out[5] = (uint8_t)(s->timestamp >> 16);
    out[6] = (uint8_t)(s->timestamp >> 8); out[7] = (uint8_t)s->timestamp;
    out[8] = 0; out[9] = 0; out[10] = 0; out[11] = 1;     /* SSRC */
    out[12] = (uint8_t)k;                   /* not fragmented, k frames */
    memcpy(out + 13, frames, k * frame_length);
    s->sequence++;
    s->timestamp += k * SBCFrameSamples(&s->config);
    *used = k;
    return 13 + k * frame_length;
}
