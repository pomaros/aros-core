/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: SBC (A2DP mandatory codec) encoder.
*/
#ifndef BTA2DP_SBC_H
#define BTA2DP_SBC_H

#include <stdint.h>

enum { SBC_MONO, SBC_DUAL, SBC_STEREO, SBC_JOINT };
enum { SBC_LOUDNESS, SBC_SNR };

struct SBCConfig {
    unsigned frequency;     /* 16000, 32000, 44100 or 48000 Hz */
    unsigned mode;          /* SBC_MONO .. SBC_JOINT */
    unsigned blocks;        /* 4, 8, 12 or 16 */
    unsigned subbands;      /* 4 or 8 */
    unsigned allocation;    /* SBC_LOUDNESS or SBC_SNR */
    unsigned bitpool;       /* 2..250, further limited by the mode */
};

struct SBCEncoder {
    struct SBCConfig cfg;
    unsigned channels;
    float history[2][80];   /* analysis input, newest sample first */
    float matrix[8][16];    /* cos((k + 0.5)(i - M/2) pi / M) */
};

/* Zero on success; rejects configurations the specification forbids. */
int SBCInit(struct SBCEncoder *e, const struct SBCConfig *cfg);
/* Bytes of one encoded frame, or 0 for an invalid configuration. */
unsigned SBCFrameLength(const struct SBCConfig *cfg);
/* PCM frames (per channel) consumed by one SBC frame: blocks * subbands. */
unsigned SBCFrameSamples(const struct SBCConfig *cfg);
/* Encodes blocks * subbands interleaved signed 16-bit frames (one or two
 * channels by mode) into one SBC frame. Returns its length, 0 on error. */
unsigned SBCEncode(struct SBCEncoder *e, const int16_t *pcm, uint8_t *out, unsigned size);

#endif
