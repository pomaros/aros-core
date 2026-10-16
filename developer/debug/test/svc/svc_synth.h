/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: [SMP13]'s synthetic audio service, the same code in every vehicle.

    A client produces BLOCKS blocks of FRAMES samples into the request ring
    and collects the engine's output from the result ring; the engine turns
    each block into output with a running filter. Both sides block on their
    waker when a ring is full or empty, with a timeout as the only fallback.
    The output digest depends on the data alone, so every vehicle must
    produce the same one; anything else is a lost or reordered block.

    The vehicle supplies svc_synth_wait(): wait for this side's wake or the
    timeout, 1 if woken, 0 on timeout.
*/

#ifndef SVC_SYNTH_H
#define SVC_SYNTH_H

#include <stdint.h>
#include <aros/svc.h>

#define SVC_SYNTH_FRAMES   256U
#define SVC_SYNTH_BLOCKS   4000U
#define SVC_SYNTH_SLOTS    8U          /* blocks per ring */
#define SVC_SYNTH_TIMEOUT  20U         /* ms */

struct SvcSynthBlock { int32_t s[SVC_SYNTH_FRAMES]; };

struct SvcSynth
{
    struct SvcRing requests, results;
    struct SvcSynthBlock request_slots[SVC_SYNTH_SLOTS], result_slots[SVC_SYNTH_SLOTS];
    struct SvcWaker to_engine, to_client;
    uint32_t timeouts_engine, timeouts_client, overruns, spins;
    uint32_t digest;
    int32_t state;                      /* the engine's filter */
};

/* the vehicle's wait: 1 woken, 0 timed out */
int svc_synth_wait(struct SvcSynth *s, struct SvcWaker *w, uint32_t timeout_ms);

static inline void svc_synth_init(struct SvcSynth *s)
{
    SvcRingInit(&s->requests, s->request_slots, SVC_SYNTH_SLOTS, sizeof(struct SvcSynthBlock));
    SvcRingInit(&s->results, s->result_slots, SVC_SYNTH_SLOTS, sizeof(struct SvcSynthBlock));
    s->timeouts_engine = s->timeouts_client = s->overruns = s->spins = 0;
    s->digest = 2166136261U;
    s->state = 0;
}

/* The engine: until BLOCKS blocks have gone through. */
static inline void svc_synth_engine(struct SvcSynth *s)
{
    struct SvcSynthBlock in, out;
    uint32_t done = 0, k;

    while (done < SVC_SYNTH_BLOCKS) {
        if (!SvcRingFill(&s->requests) || !SvcRingSpace(&s->results)) {
            if (!svc_synth_wait(s, &s->to_engine, SVC_SYNTH_TIMEOUT)) {
                s->timeouts_engine++;
                SvcCheckLost(&s->to_engine);
            }
            continue;
        }
        SvcRingRead(&s->requests, &in, 1);
        SvcWake(&s->to_client);         /* request space */
        for (k = 0; k < SVC_SYNTH_FRAMES; ++k) {
            s->state = (int32_t)((uint32_t)s->state * 3U + (uint32_t)in.s[k]) >> 1;
            out.s[k] = s->state;
        }
        if (SvcRingWrite(&s->results, &out, 1) != 1)
            s->overruns++;
        SvcWake(&s->to_client);         /* a result */
        ++done;
    }
}

/* The client: produce and collect. Returns the digest. */
static inline uint32_t svc_synth_client(struct SvcSynth *s)
{
    struct SvcSynthBlock b;
    uint32_t produced = 0, collected = 0, lcg = 12345U, k;

    while (collected < SVC_SYNTH_BLOCKS) {
        int progress = 0;

        while (produced < SVC_SYNTH_BLOCKS && SvcRingSpace(&s->requests)) {
            for (k = 0; k < SVC_SYNTH_FRAMES; ++k) {
                lcg = lcg * 1664525U + 1013904223U;
                b.s[k] = (int32_t)(lcg >> 8) - (1 << 23);
            }
            SvcRingWrite(&s->requests, &b, 1);
            SvcWake(&s->to_engine);
            ++produced;
            progress = 1;
        }
        while (SvcRingRead(&s->results, &b, 1) == 1) {
            for (k = 0; k < SVC_SYNTH_FRAMES; ++k) {
                uint32_t v = (uint32_t)b.s[k];
                uint32_t i;

                for (i = 0; i < 4; ++i) {
                    s->digest ^= (v >> (8 * i)) & 0xffU;
                    s->digest *= 16777619U;
                }
            }
            SvcWake(&s->to_engine);     /* result space */
            ++collected;
            progress = 1;
        }
        if (!progress && !svc_synth_wait(s, &s->to_client, SVC_SYNTH_TIMEOUT)) {
            s->timeouts_client++;
            SvcCheckLost(&s->to_client);
        }
    }
    return s->digest;
}

#endif
