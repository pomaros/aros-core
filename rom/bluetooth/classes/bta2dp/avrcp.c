/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: AVRCP over AVCTP, written from the Bluetooth SIG AVCTP 1.4 and
          AVRCP 1.6 specifications. One L2CAP channel carries both roles:
          frames we send as controller and the headset's commands to us as
          target, told apart by the AVCTP C/R bit.
*/
#include <string.h>
#include "avrcp.h"

/* AVCTP single-packet header: label(4) | type(2) | C/R(1) | IPID(1), PID */
#define AVCTP_COMMAND   0
#define AVCTP_RESPONSE  2
#define AVCTP_IPID      1
#define PID_HI          0x11
#define PID_LO          0x0e

/* AV/C ctype and response codes */
#define CT_CONTROL      0x00
#define CT_STATUS       0x01
#define CT_NOTIFY       0x03
#define RSP_NOT_IMPL    0x08
#define RSP_ACCEPTED    0x09
#define RSP_REJECTED    0x0a
#define RSP_STABLE      0x0c
#define RSP_CHANGED     0x0d
#define RSP_INTERIM     0x0f

#define SUBUNIT_PANEL   0x48    /* type 9 (panel) << 3, id 0 */
#define SUBUNIT_UNIT    0xff
#define OP_VENDOR       0x00
#define OP_UNIT_INFO    0x30
#define OP_SUBUNIT_INFO 0x31
#define OP_PASS_THROUGH 0x7c

/* Vendor-dependent PDUs (Bluetooth SIG company id 0x001958) */
#define PDU_GET_CAPABILITIES      0x10
#define PDU_REGISTER_NOTIFICATION 0x31
#define PDU_SET_ABSOLUTE_VOLUME   0x50
#define CAP_EVENTS                0x03
#define EVENT_PLAYBACK_STATUS     0x01
#define EVENT_VOLUME_CHANGED      0x0d
#define STATUS_INVALID_COMMAND    0x00
#define STATUS_INVALID_PARAMETER  0x01
#define PLAY_STOPPED              0x00
#define PLAY_PLAYING              0x01
#define PLAY_PAUSED               0x02

static unsigned header(uint8_t *out, uint8_t label, int response, uint8_t ctype, uint8_t subunit, uint8_t opcode)
{
    out[0] = (uint8_t)((label & 15) << 4 | (response ? AVCTP_RESPONSE : AVCTP_COMMAND));
    out[1] = PID_HI;
    out[2] = PID_LO;
    out[3] = ctype;
    out[4] = subunit;
    out[5] = opcode;
    return 6;
}

static unsigned vendor(uint8_t *out, uint8_t label, int response, uint8_t ctype, uint8_t pdu,
    const uint8_t *params, unsigned length)
{
    unsigned n = header(out, label, response, ctype, SUBUNIT_PANEL, OP_VENDOR);
    out[n++] = 0x00; out[n++] = 0x19; out[n++] = 0x58;
    out[n++] = pdu;
    out[n++] = 0x00;                    /* single packet */
    out[n++] = (uint8_t)(length >> 8);
    out[n++] = (uint8_t)length;
    if (length) memcpy(out + n, params, length);
    return n + length;
}

static int command(struct AVRCPSession *s, uint8_t ctype, uint8_t pdu, const uint8_t *params,
    unsigned length, uint8_t *label)
{
    uint8_t frame[32];
    unsigned n;
    *label = s->label;
    s->label = (uint8_t)((s->label + 1) & 15);
    n = vendor(frame, *label, 0, ctype, pdu, params, length);
    s->commands++;
    return s->send(s->context, frame, n);
}

static int register_volume(struct AVRCPSession *s)
{
    const uint8_t params[5] = {EVENT_VOLUME_CHANGED, 0, 0, 0, 0};
    s->volume_registered = 1;
    return command(s, CT_NOTIFY, PDU_REGISTER_NOTIFICATION, params, 5, &s->volume_label);
}

int AVRCPStart(struct AVRCPSession *s)
{
    const uint8_t cap = CAP_EVENTS;
    uint8_t label;
    if (!s || !s->send) return -1;
    return command(s, CT_STATUS, PDU_GET_CAPABILITIES, &cap, 1, &label);
}

int AVRCPSetVolume(struct AVRCPSession *s, uint8_t volume)
{
    uint8_t label;
    if (!s || !s->absolute_volume) return -1;
    if (volume > AVRCP_VOLUME_MAX) volume = AVRCP_VOLUME_MAX;
    s->current_volume = volume;
    return command(s, CT_CONTROL, PDU_SET_ABSOLUTE_VOLUME, &volume, 1, &label);
}

static uint8_t play_status(const struct AVRCPSession *s)
{
    return s->playing ? PLAY_PLAYING : PLAY_PAUSED;
}

int AVRCPSetPlaying(struct AVRCPSession *s, int playing)
{
    uint8_t frame[32], params[2];
    if (!s) return -1;
    if (!!playing == s->playing) return 0;
    s->playing = (uint8_t)!!playing;
    if (!s->status_registered) return 0;
    /* a notification is answered once with CHANGED; the headset registers again */
    s->status_registered = 0;
    params[0] = EVENT_PLAYBACK_STATUS;
    params[1] = play_status(s);
    return s->send(s->context, frame,
        vendor(frame, s->status_label, 1, RSP_CHANGED, PDU_REGISTER_NOTIFICATION, params, 2));
}

/* The headset's commands to us as target. */
static void target(struct AVRCPSession *s, uint8_t label, const uint8_t *p, unsigned n)
{
    uint8_t frame[48], params[8];
    uint8_t ctype = p[3], opcode = p[5];
    s->commands++;
    if (opcode == OP_UNIT_INFO || opcode == OP_SUBUNIT_INFO) {
        /* UNIT INFO: 0x07, unit type/id; SUBUNIT INFO: page 0, then the
           subunit table - both name one panel subunit, no company id */
        unsigned k = header(frame, label, 1, RSP_STABLE, SUBUNIT_UNIT, opcode);
        frame[k++] = 0x07; frame[k++] = SUBUNIT_PANEL;
        frame[k++] = 0xff; frame[k++] = 0xff; frame[k++] = 0xff;
        s->send(s->context, frame, k);
        return;
    }
    if (opcode == OP_PASS_THROUGH && n >= 8) {
        uint8_t op = p[6] & 0x7f;
        int pressed = !(p[6] & 0x80);
        /* the answer echoes the operation */
        memcpy(frame, p, n < sizeof(frame) ? n : sizeof(frame));
        frame[0] = (uint8_t)((label & 15) << 4 | AVCTP_RESPONSE);
        frame[3] = RSP_ACCEPTED;
        s->send(s->context, frame, n < sizeof(frame) ? n : sizeof(frame));
        if (s->button) s->button(s->context, op, pressed);
        return;
    }
    if (opcode == OP_VENDOR && n >= 13 && p[6] == 0x00 && p[7] == 0x19 && p[8] == 0x58) {
        uint8_t pdu = p[9];
        unsigned length = (unsigned)p[11] << 8 | p[12];
        const uint8_t *q = p + 13;
        if (13 + length > n) length = n - 13;
        if (pdu == PDU_GET_CAPABILITIES && length >= 1 && q[0] == CAP_EVENTS) {
            params[0] = CAP_EVENTS; params[1] = 1; params[2] = EVENT_PLAYBACK_STATUS;
            s->send(s->context, frame, vendor(frame, label, 1, RSP_STABLE, pdu, params, 3));
            return;
        }
        if (pdu == PDU_REGISTER_NOTIFICATION && ctype == CT_NOTIFY && length >= 1 &&
            q[0] == EVENT_PLAYBACK_STATUS) {
            s->status_label = label;
            s->status_registered = 1;
            params[0] = EVENT_PLAYBACK_STATUS;
            params[1] = play_status(s);
            s->send(s->context, frame, vendor(frame, label, 1, RSP_INTERIM, pdu, params, 2));
            return;
        }
        if (pdu == PDU_SET_ABSOLUTE_VOLUME && length >= 1) {
            /* a sink does not set our volume; refuse without breaking the link */
            params[0] = STATUS_INVALID_COMMAND;
            s->send(s->context, frame, vendor(frame, label, 1, RSP_REJECTED, pdu, params, 1));
            return;
        }
        params[0] = STATUS_INVALID_COMMAND;
        s->send(s->context, frame, vendor(frame, label, 1, RSP_REJECTED, pdu, params, 1));
        return;
    }
    {
        /* anything else: the AV/C frame back as NOT IMPLEMENTED */
        unsigned k = n < sizeof(frame) ? n : sizeof(frame);
        memcpy(frame, p, k);
        frame[0] = (uint8_t)((label & 15) << 4 | AVCTP_RESPONSE);
        frame[3] = RSP_NOT_IMPL;
        s->send(s->context, frame, k);
    }
}

/* Answers to our controller commands. */
static void controller(struct AVRCPSession *s, uint8_t label, const uint8_t *p, unsigned n)
{
    uint8_t rsp = p[3];
    s->responses++;
    if (p[5] != OP_VENDOR || n < 13 || p[6] != 0x00 || p[7] != 0x19 || p[8] != 0x58) {
        s->ignored++;
        return;
    }
    {
        uint8_t pdu = p[9];
        unsigned length = (unsigned)p[11] << 8 | p[12];
        const uint8_t *q = p + 13;
        if (13 + length > n) length = n - 13;
        if (pdu == PDU_GET_CAPABILITIES && rsp == RSP_STABLE && length >= 2 && q[0] == CAP_EVENTS) {
            for (unsigned i = 0; i < q[1] && 2 + i < length; i++)
                if (q[2 + i] == EVENT_VOLUME_CHANGED) s->absolute_volume = 1;
            if (s->absolute_volume) register_volume(s);
            return;
        }
        if (pdu == PDU_REGISTER_NOTIFICATION && label == s->volume_label && length >= 2 &&
            q[0] == EVENT_VOLUME_CHANGED) {
            uint8_t volume = q[1] & AVRCP_VOLUME_MAX;
            /* INTERIM only confirms the registration: the source sets the
               volume at connection; a CHANGED is the user on the headset */
            if (rsp == RSP_CHANGED) {
                s->current_volume = volume;
                if (s->volume) s->volume(s->context, volume);
            }
            if (rsp == RSP_CHANGED) register_volume(s);   /* one CHANGED per registration */
            else if (rsp != RSP_INTERIM) s->volume_registered = 0;
            return;
        }
        if (pdu == PDU_SET_ABSOLUTE_VOLUME && rsp == RSP_ACCEPTED && length >= 1) {
            s->current_volume = q[0] & AVRCP_VOLUME_MAX;
            return;
        }
    }
    (void)label;
    s->ignored++;
}

void AVRCPReceive(struct AVRCPSession *s, const uint8_t *p, unsigned n)
{
    uint8_t label;
    if (!s || !p || n < 6) return;
    if ((p[0] & 0x0c) != 0 || p[1] != PID_HI || p[2] != PID_LO) {
        /* fragmented or another profile: an IPID answer says "not us" */
        if (!(p[0] & AVCTP_RESPONSE) && n >= 3) {
            uint8_t frame[3] = {(uint8_t)((p[0] & 0xf0) | AVCTP_RESPONSE | AVCTP_IPID), p[1], p[2]};
            s->send(s->context, frame, 3);
        }
        s->ignored++;
        return;
    }
    label = p[0] >> 4;
    if (p[0] & AVCTP_RESPONSE) controller(s, label, p, n);
    else target(s, label, p, n);
}
