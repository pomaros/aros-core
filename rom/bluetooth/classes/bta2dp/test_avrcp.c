/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: host test of the AVRCP session against a scripted headset.
*/
#include <stdio.h>
#include <string.h>
#include "avrcp.h"
#include "stream.h"

static uint8_t sent[16][64];
static unsigned sent_len[16], nsent;
static int buttons[8], nbuttons, last_volume = -1;

static int send(void *c, const uint8_t *d, unsigned n) { (void)c; memcpy(sent[nsent], d, n); sent_len[nsent++] = n; return 0; }
static void button(void *c, uint8_t op, int pressed) { (void)c; buttons[nbuttons++] = op | (pressed ? 0 : 0x80); }
static void volume(void *c, uint8_t v) { (void)c; last_volume = v; }

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static unsigned vendor_rsp(uint8_t *f, uint8_t label, uint8_t rsp, uint8_t pdu, const uint8_t *p, unsigned n)
{
    uint8_t h[13] = {(uint8_t)(label << 4 | 2), 0x11, 0x0e, rsp, 0x48, 0x00, 0x00, 0x19, 0x58, pdu, 0, 0, (uint8_t)n};
    memcpy(f, h, 13); memcpy(f + 13, p, n); return 13 + n;
}

int main(void)
{
    struct AVRCPSession s;
    uint8_t f[64];
    unsigned n;
    memset(&s, 0, sizeof(s));
    s.send = send; s.button = button; s.volume = volume;

    /* controller: GET_CAPABILITIES(events) */
    CHECK(AVRCPStart(&s) == 0 && nsent == 1);
    CHECK(sent[0][0] == 0x00 && sent[0][1] == 0x11 && sent[0][2] == 0x0e && sent[0][3] == 0x01 &&
          sent[0][4] == 0x48 && sent[0][5] == 0x00 && sent[0][9] == 0x10 && sent[0][12] == 1 && sent[0][13] == 0x03);
    CHECK(AVRCPSetVolume(&s, 100) == -1);   /* not offered yet */
    /* the headset offers VOLUME_CHANGED: we register for it */
    { const uint8_t caps[] = {0x03, 2, 0x01, 0x0d}; n = vendor_rsp(f, 0, 0x0c, 0x10, caps, 4); }
    AVRCPReceive(&s, f, n);
    CHECK(s.absolute_volume && nsent == 2 && sent[1][3] == 0x03 && sent[1][9] == 0x31 && sent[1][13] == 0x0d);
    /* INTERIM only confirms; CHANGED reports and re-registers */
    { const uint8_t v[] = {0x0d, 0x40}; n = vendor_rsp(f, s.volume_label, 0x0f, 0x31, v, 2); }
    AVRCPReceive(&s, f, n);
    CHECK(last_volume == -1 && nsent == 2);
    { const uint8_t v[] = {0x0d, 0x50}; n = vendor_rsp(f, s.volume_label, 0x0d, 0x31, v, 2); }
    AVRCPReceive(&s, f, n);
    CHECK(last_volume == 0x50 && nsent == 3 && sent[2][9] == 0x31);
    /* SET_ABSOLUTE_VOLUME is a CONTROL with one byte, clamped */
    CHECK(AVRCPSetVolume(&s, 200) == 0 && nsent == 4 && sent[3][3] == 0x00 && sent[3][9] == 0x50 &&
          sent[3][12] == 1 && sent[3][13] == 0x7f);

    /* target: a PLAY press and release are accepted and reported */
    { const uint8_t pt[] = {0x30, 0x11, 0x0e, 0x00, 0x48, 0x7c, 0x44, 0x00}; AVRCPReceive(&s, pt, 8); }
    CHECK(nsent == 5 && sent[4][0] == 0x32 && sent[4][3] == 0x09 && sent[4][6] == 0x44);
    { const uint8_t pt[] = {0x40, 0x11, 0x0e, 0x00, 0x48, 0x7c, 0xc4, 0x00}; AVRCPReceive(&s, pt, 8); }
    CHECK(nbuttons == 2 && buttons[0] == 0x44 && buttons[1] == (0x44 | 0x80));
    /* target: GET_CAPABILITIES and a playback-status registration */
    { const uint8_t q[] = {0x50, 0x11, 0x0e, 0x01, 0x48, 0x00, 0x00, 0x19, 0x58, 0x10, 0x00, 0x00, 0x01, 0x03};
      AVRCPReceive(&s, q, sizeof(q)); }
    CHECK(sent[nsent - 1][3] == 0x0c && sent[nsent - 1][13] == 0x03 && sent[nsent - 1][15] == 0x01);
    { const uint8_t q[] = {0x60, 0x11, 0x0e, 0x03, 0x48, 0x00, 0x00, 0x19, 0x58, 0x31, 0x00, 0x00, 0x05, 0x01, 0, 0, 0, 0};
      AVRCPReceive(&s, q, sizeof(q)); }
    CHECK(sent[nsent - 1][0] == 0x62 && sent[nsent - 1][3] == 0x0f && sent[nsent - 1][14] == 0x02);
    n = nsent;
    CHECK(AVRCPSetPlaying(&s, 1) == 0 && nsent == n + 1 && sent[n][0] == 0x62 && sent[n][3] == 0x0d && sent[n][14] == 0x01);
    CHECK(AVRCPSetPlaying(&s, 0) == 0 && nsent == n + 1);   /* not registered again yet */
    /* an unknown vendor PDU is rejected, an unknown opcode not implemented */
    { const uint8_t q[] = {0x70, 0x11, 0x0e, 0x01, 0x48, 0x00, 0x00, 0x19, 0x58, 0x20, 0x00, 0x00, 0x00};
      AVRCPReceive(&s, q, sizeof(q)); }
    CHECK(sent[nsent - 1][3] == 0x0a);
    { const uint8_t q[] = {0x80, 0x11, 0x0e, 0x01, 0x48, 0x31, 0xff, 0xff}; AVRCPReceive(&s, q, sizeof(q)); }
    CHECK(sent[nsent - 1][3] == 0x0c && sent[nsent - 1][5] == 0x31);
    /* another profile's PID gets an IPID answer */
    { const uint8_t q[] = {0x90, 0x11, 0x0f, 0x00, 0x48, 0x7c}; AVRCPReceive(&s, q, sizeof(q)); }
    CHECK(sent_len[nsent - 1] == 3 && sent[nsent - 1][0] == 0x93);

    /* stream gain: half, and saturation at a gain above unity */
    {
        static struct A2DPStream st; static struct A2DPSource src;
        int16_t in[4] = {20000, -20000, 1000, -1000};
        src.config.frequency = 44100; src.config.mode = 3; src.config.blocks = 16;
        src.config.subbands = 8; src.config.allocation = 0; src.config.bitpool = 53;
        CHECK(A2DPStreamInit(&st, &src, 1023, 30) == 0 && st.gain == 65536);
        A2DPStreamSetGain(&st, 32768);
        CHECK(A2DPStreamWrite(&st, in, 1) == 1 && st.ring[0] == 10000 && st.ring[1] == -10000);
        A2DPStreamSetGain(&st, 131072);
        CHECK(A2DPStreamWrite(&st, in, 2) == 2 && st.ring[2] == 32767 && st.ring[3] == -32768 && st.ring[4] == 2000);
    }
    printf("PASS avrcp frames=%u buttons=%d volume=%d\n", nsent, nbuttons, last_volume);
    return 0;
}
