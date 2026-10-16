/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: Host test of the A2DP media pump on a simulated clock: pacing,
          lead, RTP continuity and an underrun. Writes the SBC frames carried
          by the packets and the PCM fed in, for an independent decoder:
          test_stream out.sbc out.pcm
*/
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "stream.h"

struct Sink { FILE *sbc; unsigned packets; uint16_t sequence; uint32_t timestamp; uint64_t now; uint64_t played; };
static struct A2DPStream st;

static int media(void *context, const uint8_t *p, unsigned n)
{
    struct Sink *k = context;
    uint16_t seq = (uint16_t)(p[2] << 8 | p[3]);
    uint32_t ts = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
    unsigned frames = p[12] & 15;
    assert(p[0] == 0x80 && p[1] == 0x60 && n == 13 + frames * 119 && n <= 895);
    if (k->packets) assert(seq == (uint16_t)(k->sequence + 1) && ts == k->timestamp);
    k->sequence = seq; k->timestamp = ts + frames * 128;
    /* Never more than the lead (30 ms) ahead of the sink's clock. */
    k->played += frames * 128;
    fwrite(p + 13, 1, n - 13, k->sbc);
    k->packets++;
    return 0;
}

int main(int argc, char **argv)
{
    struct A2DPSource src;
    struct Sink k = {0};
    FILE *raw;
    double t = 0;
    uint64_t first = 0;
    unsigned stall_until = 0;
    assert(argc == 3 && (k.sbc = fopen(argv[1], "wb")) && (raw = fopen(argv[2], "wb")));
    memset(&src, 0, sizeof(src));
    assert(!A2DPChooseConfig((const uint8_t[]){0x21, 0x15, 2, 53}, &src.config));
    assert(!A2DPStreamInit(&st, &src, 895, 30));
    for (uint64_t now = 0; now < 10000000; now += 1000) {
        /* AHI mixes 512-frame buffers whenever the ring accepts them. */
        if (now / 1000 < 4000 || now / 1000 >= 4400) {
            while (A2DP_RING_FRAMES - st.count >= 512) {
                int16_t pcm[512 * 2];
                for (unsigned i = 0; i < 512; i++, t += 1.0 / 44100) {
                    pcm[2 * i] = (int16_t)lrint(0.5 * 32767 * sin(2 * M_PI * 440 * t));
                    pcm[2 * i + 1] = (int16_t)lrint(0.5 * 32767 * sin(2 * M_PI * 660 * t));
                }
                assert(A2DPStreamWrite(&st, pcm, 512) == 512);
                fwrite(pcm, 2, 1024, raw);
            }
        } else stall_until = 1;
        assert(A2DPStreamPump(&st, now, media, &k) >= 0);
        if (k.packets && !first) first = now;
        if (st.started) {
            uint64_t clock = (now - st.start_us) * 44100 / 1000000;
            assert(st.sent_samples <= clock + 30 * 44100 / 1000 + 1);
        }
    }
    fclose(k.sbc); fclose(raw);
    assert(stall_until && st.underruns >= 1);
    /* ~10 s of audio, minus the 400 ms stall the 186 ms ring cannot cover, in 7-frame packets of 896 samples. */
    assert(k.packets > 9 * 44100 / 896 && k.packets <= 10 * 44100 / 896 + 5);
    printf("A2DP media pump: PASS packets=%u underruns=%u\n", k.packets, st.underruns);
    return 0;
}
