/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: Host test for the SBC encoder. Writes a raw SBC stream and the PCM
          it encodes so an independent decoder can judge it:
          test_sbc FREQ MODE BLOCKS SUBBANDS ALLOC BITPOOL SECONDS out.sbc out.pcm
*/
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "sbc.h"

int main(int argc, char **argv)
{
    struct SBCConfig c;
    struct SBCEncoder e;
    FILE *sbc, *raw;
    unsigned frames, n, total, channels;
    int16_t pcm[16 * 8 * 2];
    uint8_t out[1024];
    double t = 0;

    if (argc != 10) { fprintf(stderr, "usage\n"); return 2; }
    c.frequency = atoi(argv[1]); c.mode = atoi(argv[2]); c.blocks = atoi(argv[3]);
    c.subbands = atoi(argv[4]); c.allocation = atoi(argv[5]); c.bitpool = atoi(argv[6]);
    if (SBCInit(&e, &c)) { fprintf(stderr, "invalid config\n"); return 1; }
    channels = c.mode == SBC_MONO ? 1 : 2;
    n = SBCFrameSamples(&c);
    total = (unsigned)(atof(argv[7]) * c.frequency);
    frames = total / n;
    assert((sbc = fopen(argv[8], "wb")) && (raw = fopen(argv[9], "wb")));
    for (unsigned f = 0; f < frames; f++) {
        unsigned len;
        for (unsigned i = 0; i < n; i++, t += 1.0 / c.frequency) {
            /* Left: 440 Hz plus a 100 Hz-8 kHz sweep; right: 1 kHz. */
            double sweep = 100.0 * pow(80.0, fmod(t, 4.0) / 4.0);
            double l = 0.45 * sin(2 * M_PI * 440 * t) + 0.25 * sin(2 * M_PI * sweep * t);
            double r = 0.6 * sin(2 * M_PI * 1000 * t + 0.3);
            pcm[i * channels] = (int16_t)lrint(l * 32767);
            if (channels == 2) pcm[i * channels + 1] = (int16_t)lrint(r * 32767);
        }
        len = SBCEncode(&e, pcm, out, sizeof(out));
        assert(len == SBCFrameLength(&c));
        assert(out[0] == 0x9c);
        fwrite(out, 1, len, sbc);
        fwrite(pcm, sizeof(int16_t), n * channels, raw);
    }
    fclose(sbc); fclose(raw);
    printf("frames=%u frame_length=%u\n", frames, SBCFrameLength(&c));
    return 0;
}
