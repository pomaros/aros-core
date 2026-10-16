/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: Host test of the AVDTP source against a simulated A2DP sink that
          answers like a headset: an AAC endpoint listed before the SBC one,
          delay reporting, optional AVDTP 1.0 behaviour, sink-initiated setup.
*/
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "avdtp.h"

struct Sink {
    int old_version;            /* general-reject GET_ALL_CAPABILITIES */
    int aac_only;
    uint8_t last[64]; unsigned last_length;
    uint8_t config[4];
    unsigned commands, configured, opened, started;
    struct A2DPSource *src;
};

static void reply(struct Sink *k, const uint8_t *cmd, unsigned type, const uint8_t *body, unsigned n)
{
    uint8_t p[64];
    p[0] = (uint8_t)((cmd[0] & 0xf0) | type); p[1] = cmd[1];
    memcpy(p + 2, body, n);
    A2DPReceive(k->src, p, n + 2);
}

/* The source's packets reach the sink here; it answers synchronously. */
static int to_sink(void *context, const uint8_t *p, unsigned n)
{
    struct Sink *k = context;
    static const uint8_t discover[] = {1 << 2, 0x08, 2 << 2, 0x08, 3 << 2 | 2, 0x08, 4 << 2, 0x00};
    assert(n >= 2 && n <= sizeof(k->last));
    memcpy(k->last, p, n); k->last_length = n;
    if ((p[0] & 3) != 0) return 0;          /* our answers to the source's commands */
    k->commands++;
    switch (p[1]) {
    case AVDTP_DISCOVER: reply(k, p, 2, discover, sizeof(discover)); break;
    case AVDTP_GET_ALL_CAPABILITIES:
        if (k->old_version) { reply(k, p, 1, NULL, 0); break; }
        /* fall through */
    case AVDTP_GET_CAPABILITIES:
        if ((p[2] >> 2) == 1 || k->aac_only) {
            static const uint8_t aac[] = {1, 0, 7, 8, 0x00, 0x02, 0x80, 0x01, 0x8c, 0x84, 0xe2, 0x00};
            reply(k, p, 2, aac, sizeof(aac));
        } else {
            static const uint8_t sbc[] = {1, 0, 7, 6, 0x00, 0x00, 0x33, 0x15, 2, 53, 8, 0};
            reply(k, p, 2, sbc, p[1] == AVDTP_GET_ALL_CAPABILITIES ? sizeof(sbc) : sizeof(sbc) - 2);
        }
        break;
    case AVDTP_SET_CONFIGURATION:
        assert((p[2] >> 2) == 2 && (p[3] >> 2) == 1 && p[4] == 1 && p[6] == 7 && p[7] == 6);
        memcpy(k->config, p + 10, 4); k->configured++;
        reply(k, p, 2, NULL, 0); break;
    case AVDTP_OPEN: k->opened++; reply(k, p, 2, NULL, 0); break;
    case AVDTP_START: k->started++; reply(k, p, 2, NULL, 0); break;
    case AVDTP_SUSPEND: case AVDTP_CLOSE: reply(k, p, 2, NULL, 0); break;
    default: assert(!"unexpected command");
    }
    return 0;
}

static void command(struct A2DPSource *s, uint8_t label, uint8_t signal, const uint8_t *body, unsigned n)
{
    uint8_t p[32] = {(uint8_t)(label << 4), signal};
    memcpy(p + 2, body, n);
    A2DPReceive(s, p, n + 2);
}

int main(void)
{
    struct A2DPSource s;
    struct Sink k = {0};
    struct SBCConfig c;
    uint8_t frames[15 * 119], packet[2048];
    unsigned used, length;

    /* Source-initiated: AAC endpoint skipped, the in-use one ignored. */
    k.src = &s;
    assert(!A2DPStart(&s, to_sink, &k));
    assert(s.state == A2DP_MEDIA && s.remote_seid == 2 && s.delay_reports);
    assert(k.configured == 1 && k.opened == 1);
    assert(k.config[0] == 0x21 && k.config[1] == 0x15 && k.config[2] == 2 && k.config[3] == 53);
    assert(s.config.frequency == 44100 && s.config.mode == SBC_JOINT && s.config.bitpool == 53);
    assert(!A2DPMediaOpen(&s) && s.state == A2DP_STREAMING && k.started == 1);

    /* Media packets fill the MTU, at most 15 frames, RTP sequence/timestamp. */
    memset(frames, 0x9c, sizeof(frames));
    length = A2DPPacket(&s, frames, 15, 119, 895, packet, &used);
    assert(used == 7 && length == 13 + 7 * 119 && packet[0] == 0x80 && packet[1] == 0x60 && packet[12] == 7);
    length = A2DPPacket(&s, frames, 15, 119, 2048, packet, &used);
    assert(used == 15 && packet[3] == 1 && packet[7] == (uint8_t)(7 * 128) && packet[6] == (7 * 128) >> 8);
    assert(!A2DPPacket(&s, frames, 15, 119, 100, packet, &used) && !used);

    /* Sink commands while streaming. */
    command(&s, 3, AVDTP_DELAYREPORT, (const uint8_t[]){1 << 2, 0x05, 0xdc}, 3);
    assert(s.delay == 1500 && (k.last[0] & 3) == 2);
    command(&s, 4, 0x3e, NULL, 0);
    assert((k.last[0] & 3) == 1 && k.last[1] == 0x3e);
    command(&s, 5, AVDTP_GET_CAPABILITIES, (const uint8_t[]){7 << 2}, 1);
    assert((k.last[0] & 3) == 3 && k.last[2] == 0x12);
    command(&s, 6, AVDTP_SUSPEND, (const uint8_t[]){1 << 2}, 1);
    assert(s.state == A2DP_SUSPENDED);
    command(&s, 7, AVDTP_START, (const uint8_t[]){1 << 2}, 1);
    assert(s.state == A2DP_STREAMING);
    assert(!A2DPSuspend(&s) && s.state == A2DP_SUSPENDED);
    assert(!A2DPClose(&s) && s.state == A2DP_CLOSED);

    /* An AVDTP 1.0 sink rejects GET_ALL_CAPABILITIES: fall back. */
    memset(&k, 0, sizeof(k)); k.src = &s; k.old_version = 1;
    assert(!A2DPStart(&s, to_sink, &k));
    assert(s.state == A2DP_MEDIA && !s.get_all && !s.delay_reports);

    /* No SBC anywhere: a clear failure, not a guess. */
    memset(&k, 0, sizeof(k)); k.src = &s; k.aac_only = 1;
    A2DPStart(&s, to_sink, &k);
    assert(s.state == A2DP_FAILED && !k.configured);

    /* Sink-initiated setup, as a reconnecting headset does. */
    memset(&k, 0, sizeof(k)); k.src = &s;
    memset(&s, 0, sizeof(s)); s.send = to_sink; s.context = &k; s.local_seid = 1;
    command(&s, 1, AVDTP_DISCOVER, NULL, 0);
    assert((k.last[0] & 3) == 2 && k.last[2] == (1 << 2) && k.last[3] == 0x00);
    command(&s, 2, AVDTP_GET_ALL_CAPABILITIES, (const uint8_t[]){1 << 2}, 1);
    assert((k.last[0] & 3) == 2 && k.last[4] == 7 && k.last[11] == 53 && k.last_length == 2 + 12);
    command(&s, 3, AVDTP_SET_CONFIGURATION, (const uint8_t[]){1 << 2, 5 << 2, 1, 0, 7, 6, 0, 0, 0x11, 0x15, 2, 51}, 12);
    assert((k.last[0] & 3) == 2 && s.state == A2DP_OPENING && s.remote_seid == 5);
    assert(s.config.frequency == 48000 && s.config.bitpool == 51);
    command(&s, 4, AVDTP_SET_CONFIGURATION, (const uint8_t[]){1 << 2, 5 << 2, 1, 0, 7, 6, 0, 0, 0x11, 0x15, 2, 51}, 12);
    assert((k.last[0] & 3) == 3 && k.last[3] == 0x13);
    command(&s, 5, AVDTP_START, (const uint8_t[]){1 << 2}, 1);
    assert((k.last[0] & 3) == 3 && s.state == A2DP_OPENING);
    command(&s, 6, AVDTP_OPEN, (const uint8_t[]){1 << 2}, 1);
    command(&s, 7, AVDTP_START, (const uint8_t[]){1 << 2}, 1);
    assert(s.state == A2DP_STREAMING);
    /* A capability set is not a configuration. */
    memset(&s, 0, sizeof(s)); s.send = to_sink; s.context = &k; s.local_seid = 1;
    command(&s, 8, AVDTP_SET_CONFIGURATION, (const uint8_t[]){1 << 2, 5 << 2, 1, 0, 7, 6, 0, 0, 0xff, 0xff, 2, 53}, 12);
    assert((k.last[0] & 3) == 3 && k.last[2] == 7 && k.last[3] == 0x29 && s.state == A2DP_IDLE);

    assert(!A2DPChooseConfig((const uint8_t[]){0x18, 0x15, 2, 35}, &c) && c.frequency == 48000 &&
        c.mode == SBC_MONO && c.bitpool == 30);
    assert(A2DPChooseConfig((const uint8_t[]){0x00, 0x15, 2, 53}, &c));
    puts("A2DP source signalling: PASS (simulated sink)");
    return 0;
}
