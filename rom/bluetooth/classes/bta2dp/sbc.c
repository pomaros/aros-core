/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1
    Desc: SBC encoder written from the A2DP specification's SBC appendix:
          polyphase analysis, scale factors, joint stereo, loudness/SNR bit
          allocation, quantisation and frame packing with its CRC-8. The
          prototype filter tables are the specification's (sbc_proto.h).
*/
#include <math.h>
#include <string.h>

#include "sbc.h"
#include "sbc_proto.h"

/* Loudness bit allocation offsets, by sampling frequency index. */
static const int offset4[4][4] = {
    {-1, 0, 0, 0}, {-2, 0, 0, 1}, {-2, 0, 0, 1}, {-2, 0, 0, 1}
};
static const int offset8[4][8] = {
    {-2, 0, 0, 0, 0, 0, 0, 1}, {-3, 0, 0, 0, 0, 0, 1, 2},
    {-4, 0, 0, 0, 0, 0, 1, 2}, {-4, 0, 0, 0, 0, 0, 1, 2}
};

static int frequency_index(unsigned hz)
{
    switch (hz) {
    case 16000: return 0;
    case 32000: return 1;
    case 44100: return 2;
    case 48000: return 3;
    }
    return -1;
}

static int valid(const struct SBCConfig *c)
{
    unsigned channels = c->mode == SBC_MONO ? 1 : 2;
    if (frequency_index(c->frequency) < 0 || c->mode > SBC_JOINT ||
        (c->subbands != 4 && c->subbands != 8) ||
        (c->blocks != 4 && c->blocks != 8 && c->blocks != 12 && c->blocks != 16) ||
        c->allocation > SBC_SNR || c->bitpool < 2)
        return 0;
    /* Per-channel modes carry at most 16 bits per subband, stereo modes 32. */
    if (c->bitpool > (channels == 1 || c->mode == SBC_DUAL ? 16U : 32U) * c->subbands)
        return 0;
    return 1;
}

unsigned SBCFrameLength(const struct SBCConfig *c)
{
    unsigned channels, bits;
    if (!valid(c)) return 0;
    channels = c->mode == SBC_MONO ? 1 : 2;
    if (c->mode == SBC_MONO || c->mode == SBC_DUAL)
        bits = c->blocks * channels * c->bitpool;
    else
        bits = (c->mode == SBC_JOINT ? c->subbands : 0) + c->blocks * c->bitpool;
    return 4 + (4 * c->subbands * channels) / 8 + (bits + 7) / 8;
}

unsigned SBCFrameSamples(const struct SBCConfig *c)
{
    return valid(c) ? c->blocks * c->subbands : 0;
}

int SBCInit(struct SBCEncoder *e, const struct SBCConfig *c)
{
    unsigned m;
    if (!e || !c || !valid(c)) return -1;
    memset(e, 0, sizeof(*e));
    e->cfg = *c;
    e->channels = c->mode == SBC_MONO ? 1 : 2;
    m = c->subbands;
    for (unsigned k = 0; k < m; k++)
        for (unsigned i = 0; i < 2 * m; i++)
            e->matrix[k][i] = (float)cos((k + 0.5) * ((double)i - m / 2.0) * M_PI / m);
    return 0;
}

/* One block of one channel: shift in M new samples, window, fold, matrix. */
static void analyse(struct SBCEncoder *e, unsigned ch, const int16_t *pcm, unsigned stride, float *out)
{
    unsigned m = e->cfg.subbands, taps = 10 * m;
    const float *proto = m == 8 ? sbc_proto8 : sbc_proto4;
    float *x = e->history[ch], y[16];
    memmove(x + m, x, (taps - m) * sizeof(float));
    for (unsigned i = 0; i < m; i++) x[m - 1 - i] = pcm[i * stride];
    for (unsigned i = 0; i < 2 * m; i++) {
        float sum = 0;
        for (unsigned j = 0; j < 5; j++) sum += proto[i + j * 2 * m] * x[i + j * 2 * m];
        y[i] = sum;
    }
    for (unsigned k = 0; k < m; k++) {
        float sum = 0;
        for (unsigned i = 0; i < 2 * m; i++) sum += e->matrix[k][i] * y[i];
        out[k] = sum;
    }
}

/* Smallest factor with every |sample| < 2^(factor + 1). */
static unsigned scale_factor(float (*s)[2][8], unsigned blocks, unsigned ch, unsigned sb)
{
    float peak = 0;
    unsigned f = 0;
    for (unsigned b = 0; b < blocks; b++) {
        float v = fabsf(s[b][ch][sb]);
        if (v > peak) peak = v;
    }
    while (f < 15 && peak >= (float)(2U << f)) f++;
    return f;
}

/* Bit allocation over one channel (mono, dual) or both (stereo, joint). */
static void allocate(const struct SBCConfig *c, unsigned first, unsigned count,
    const unsigned sf[2][8], int bits[2][8])
{
    const int fi = frequency_index(c->frequency);
    unsigned m = c->subbands, bitcount = 0, slicecount = 0;
    int need[2][8], maxneed = 0, slice;
    for (unsigned ch = first; ch < first + count; ch++)
        for (unsigned sb = 0; sb < m; sb++) {
            int n;
            if (c->allocation == SBC_SNR) n = (int)sf[ch][sb];
            else if (!sf[ch][sb]) n = -5;
            else {
                int loudness = (int)sf[ch][sb] - (m == 4 ? offset4[fi][sb] : offset8[fi][sb]);
                n = loudness > 0 ? loudness / 2 : loudness;
            }
            need[ch][sb] = n;
            if (n > maxneed) maxneed = n;
        }
    slice = maxneed + 1;
    do {
        slice--;
        bitcount += slicecount;
        slicecount = 0;
        for (unsigned ch = first; ch < first + count; ch++)
            for (unsigned sb = 0; sb < m; sb++) {
                if (need[ch][sb] > slice + 1 && need[ch][sb] < slice + 16) slicecount++;
                else if (need[ch][sb] == slice + 1) slicecount += 2;
            }
    } while (bitcount + slicecount < c->bitpool);
    if (bitcount + slicecount == c->bitpool) {
        bitcount += slicecount;
        slice--;
    }
    for (unsigned ch = first; ch < first + count; ch++)
        for (unsigned sb = 0; sb < m; sb++) {
            int b = need[ch][sb] < slice + 2 ? 0 : need[ch][sb] - slice;
            bits[ch][sb] = b > 16 ? 16 : b;
        }
    /* Leftover bits go subband by subband, channels interleaved. */
    for (unsigned sb = 0; sb < m && bitcount < c->bitpool; sb++)
        for (unsigned ch = first; ch < first + count && bitcount < c->bitpool; ch++) {
            if (bits[ch][sb] >= 2 && bits[ch][sb] < 16) { bits[ch][sb]++; bitcount++; }
            else if (need[ch][sb] == slice + 1 && c->bitpool > bitcount + 1) { bits[ch][sb] = 2; bitcount += 2; }
        }
    for (unsigned sb = 0; sb < m && bitcount < c->bitpool; sb++)
        for (unsigned ch = first; ch < first + count && bitcount < c->bitpool; ch++)
            if (bits[ch][sb] < 16) { bits[ch][sb]++; bitcount++; }
}

struct Writer { uint8_t *p; unsigned bit, size, crc_bits; uint8_t crc; int fail; };
static void put(struct Writer *w, unsigned value, unsigned n, int crc)
{
    while (n--) {
        unsigned b = (value >> n) & 1;
        if (crc) {
            unsigned top = (w->crc >> 7) & 1;
            w->crc = (uint8_t)(w->crc << 1);
            if (top ^ b) w->crc ^= 0x1d;    /* x^8 + x^4 + x^3 + x^2 + 1 */
        }
        if (w->bit / 8 >= w->size) { w->fail = 1; return; }
        if (b) w->p[w->bit / 8] |= (uint8_t)(0x80 >> (w->bit % 8));
        w->bit++;
    }
}

unsigned SBCEncode(struct SBCEncoder *e, const int16_t *pcm, uint8_t *out, unsigned size)
{
    const struct SBCConfig *c;
    float s[16][2][8];
    unsigned sf[2][8], join = 0, length;
    int bits[2][8];
    struct Writer w;
    if (!e || !pcm || !out) return 0;
    c = &e->cfg;
    length = SBCFrameLength(c);
    if (!length || size < length) return 0;
    for (unsigned b = 0; b < c->blocks; b++)
        for (unsigned ch = 0; ch < e->channels; ch++)
            analyse(e, ch, pcm + b * c->subbands * e->channels + ch, e->channels, s[b][ch]);
    for (unsigned ch = 0; ch < e->channels; ch++)
        for (unsigned sb = 0; sb < c->subbands; sb++)
            sf[ch][sb] = scale_factor(s, c->blocks, ch, sb);
    if (c->mode == SBC_JOINT) {
        /* Code a subband as mid/side when that needs smaller scale factors;
         * the last subband is never joined. */
        for (unsigned sb = 0; sb + 1 < c->subbands; sb++) {
            float ms[16][2][8];
            unsigned fm, fs;
            for (unsigned b = 0; b < c->blocks; b++) {
                ms[b][0][sb] = (s[b][0][sb] + s[b][1][sb]) / 2;
                ms[b][1][sb] = (s[b][0][sb] - s[b][1][sb]) / 2;
            }
            fm = scale_factor(ms, c->blocks, 0, sb);
            fs = scale_factor(ms, c->blocks, 1, sb);
            if (fm + fs < sf[0][sb] + sf[1][sb]) {
                join |= 1U << sb;
                sf[0][sb] = fm; sf[1][sb] = fs;
                for (unsigned b = 0; b < c->blocks; b++) {
                    s[b][0][sb] = ms[b][0][sb];
                    s[b][1][sb] = ms[b][1][sb];
                }
            }
        }
    }
    memset(bits, 0, sizeof(bits));
    if (c->mode == SBC_MONO || c->mode == SBC_DUAL)
        for (unsigned ch = 0; ch < e->channels; ch++) allocate(c, ch, 1, sf, bits);
    else allocate(c, 0, 2, sf, bits);

    memset(out, 0, length);
    w = (struct Writer){out, 0, length, 0, 0x0f, 0};
    put(&w, 0x9c, 8, 0);
    put(&w, (unsigned)frequency_index(c->frequency), 2, 1);
    put(&w, c->blocks / 4 - 1, 2, 1);
    put(&w, c->mode, 2, 1);
    put(&w, c->allocation, 1, 1);
    put(&w, c->subbands == 8, 1, 1);
    put(&w, c->bitpool, 8, 1);
    put(&w, 0, 8, 0);                       /* CRC, filled in below */
    if (c->mode == SBC_JOINT)
        for (unsigned sb = 0; sb < c->subbands; sb++) put(&w, (join >> sb) & 1, 1, 1);
    for (unsigned ch = 0; ch < e->channels; ch++)
        for (unsigned sb = 0; sb < c->subbands; sb++) put(&w, sf[ch][sb], 4, 1);
    out[3] = w.crc;
    for (unsigned b = 0; b < c->blocks; b++)
        for (unsigned ch = 0; ch < e->channels; ch++)
            for (unsigned sb = 0; sb < c->subbands; sb++) {
                unsigned n = (unsigned)bits[ch][sb], levels, q;
                float scaled;
                if (!n) continue;
                levels = (1U << n) - 1;
                scaled = (s[b][ch][sb] / (float)(2U << sf[ch][sb]) + 1.0f) * levels / 2.0f;
                q = scaled <= 0 ? 0 : (unsigned)scaled;
                if (q > levels - 1) q = levels - 1;
                put(&w, q, n, 0);
            }
    return w.fail ? 0 : length;
}
