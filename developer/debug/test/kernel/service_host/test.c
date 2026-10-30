/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x, name) do { if (!(x)) { puts("FAIL " name); exit(1); } } while (0)
struct Fixture {
    struct KrnMappingDomain d;
    struct KrnMappingDomainMember creator;
    struct KrnServiceQueue q;
};
static void init(struct Fixture *f)
{
    memset(f, 0, sizeof(*f));
    CHECK(KrnMappingDomainJoin(&f->d, &f->creator, f), "creator-join");
    CHECK(KrnServiceInitialize(&f->q, &f->d), "initialize");
    CHECK(!KrnServiceInitialize(&f->q, &f->d), "no-reset");
}
static uint64_t submit(struct Fixture *f, uint64_t owner)
{
    uint64_t token = 0;
    unsigned char bytes[] = {1, 2, 3, 4};
    CHECK(KrnServiceSubmit(&f->q, owner, bytes, sizeof(bytes), &token), "submit");
    memset(bytes, 0xff, sizeof(bytes));
    return token;
}
static void take(struct Fixture *f, uint64_t token)
{
    unsigned char bytes[4] = {0}; size_t length = 0;
    CHECK(KrnServiceTake(&f->q, token, bytes, sizeof(bytes), &length), "take");
    CHECK(length == 4 && bytes[0] == 1 && bytes[3] == 4, "owned-copy");
    CHECK(!KrnServiceTake(&f->q, token, bytes, sizeof(bytes), &length), "single-worker");
}
static void finish(struct Fixture *f)
{
    KrnServiceClose(&f->q);
    CHECK(KrnMappingDomainLeave(&f->creator), "creator-leave");
    CHECK(KrnServiceDrained(&f->q) && KrnMappingDomainCanReclaim(&f->d), "final-drain");
}
static void normal(void)
{
    struct Fixture f; init(&f); uint64_t t = submit(&f, 7), other = 99;
    CHECK(!KrnServiceSubmit(&f.q, 7, NULL, 0, &other) && other == 99, "one-per-owner");
    unsigned char output[4] = {0}; size_t n = 77;
    CHECK(!KrnServiceCollect(&f.q, 7, t, output, 4, &n) && n == 77, "not-ready");
    CHECK(!KrnServiceTake(&f.q, t, output, 3, &n) && n == 77, "take-capacity");
    CHECK(!KrnServiceComplete(&f.q, t, NULL, 0), "not-taken");
    take(&f, t);
    CHECK(!KrnServiceComplete(&f.q, t, output, SIZE_MAX), "reply-overflow");
    unsigned char reply[] = {9, 8, 7, 6};
    CHECK(KrnServiceComplete(&f.q, t, reply, 4), "complete");
    memset(reply, 0xff, 4);
    CHECK(!KrnServiceComplete(&f.q, t, NULL, 0), "complete-once");
    CHECK(!KrnServiceCollect(&f.q, 8, t, output, 4, &n) && n == 77, "collect-owner");
    CHECK(!KrnServiceCancel(&f.q, 8, t), "cancel-owner");
    CHECK(!KrnServiceCollect(&f.q, 7, t, output, 3, &n) && n == 77, "collect-capacity");
    CHECK(KrnServiceCollect(&f.q, 7, t, output, 4, &n), "collect");
    CHECK(n == 4 && output[0] == 9 && output[3] == 6, "reply-copy");
    CHECK(!KrnServiceCollect(&f.q, 7, t, output, 4, &n), "collect-once");
    CHECK(KrnServiceDrained(&f.q) && !KrnMappingDomainCanReclaim(&f.d), "creator-retained");
    finish(&f);
}
static void cancellation(void)
{
    for (unsigned started = 0; started < 2; ++started) {
        for (unsigned completed = 0; completed <= started; ++completed) {
            struct Fixture f; init(&f); uint64_t t = submit(&f, 1);
            if (started) take(&f, t);
            if (completed) CHECK(KrnServiceComplete(&f.q, t, "ok", 2), "race-complete-first");
            CHECK(KrnServiceCancel(&f.q, 1, t), "cancel");
            CHECK(KrnServiceDrained(&f.q) == (!started || completed), "cancel-retention");
            if (started && !completed) {
                CHECK(KrnServiceCancel(&f.q, 1, t), "cancel-idempotent");
                CHECK(KrnServiceComplete(&f.q, t, NULL, SIZE_MAX), "cancel-ack");
                CHECK(!KrnServiceComplete(&f.q, t, NULL, 0), "cancel-ack-once");
            }
            char out = 'x'; size_t n = 91;
            CHECK(!KrnServiceCollect(&f.q, 1, t, &out, 1, &n) && out == 'x' && n == 91, "no-cancelled-reply");
            finish(&f);
        }
    }
}
static void close_races(void)
{
    for (unsigned state = 0; state < 3; ++state) {
        struct Fixture f; init(&f); uint64_t t = submit(&f, 1), bad = 9;
        if (state) take(&f, t);
        if (state == 2) CHECK(KrnServiceComplete(&f.q, t, "ok", 2), "close-complete-first");
        KrnServiceClose(&f.q); KrnServiceClose(&f.q);
        CHECK(!KrnServiceSubmit(&f.q, 2, NULL, 0, &bad) && bad == 9, "closed-admission");
        CHECK(KrnMappingDomainLeave(&f.creator), "close-creator-leave");
        CHECK(KrnMappingDomainCanReclaim(&f.d) == (state != 1), "close-worker-retained");
        char out = 'x'; size_t n = 11;
        CHECK(!KrnServiceCollect(&f.q, 1, t, &out, 1, &n) && out == 'x' && n == 11, "close-no-output");
        if (state == 1) CHECK(KrnServiceComplete(&f.q, t, NULL, SIZE_MAX), "close-worker-ack");
        CHECK(KrnServiceDrained(&f.q) && KrnMappingDomainCanReclaim(&f.d), "close-drained");
    }
    /* Domain stop from another subsystem also suppresses completion output. */
    struct Fixture f; init(&f); uint64_t t = submit(&f, 1); take(&f, t);
    KrnMappingDomainStop(&f.d);
    CHECK(KrnServiceComplete(&f.q, t, NULL, SIZE_MAX), "external-stop-ack");
    finish(&f);
}
static void stale(void)
{
    struct Fixture f; init(&f); uint64_t old = submit(&f, 1);
    CHECK(KrnServiceCancel(&f.q, 1, old), "retire-old");
    uint64_t t = submit(&f, 1); CHECK(t != old, "new-generation");
    char out[4]; size_t n = 0;
    CHECK(!KrnServiceTake(&f.q, old, out, sizeof(out), &n), "stale-take");
    CHECK(!KrnServiceCancel(&f.q, 1, old), "stale-cancel");
    take(&f, t);
    CHECK(!KrnServiceComplete(&f.q, old, NULL, 0), "stale-complete");
    CHECK(!KrnServiceCollect(&f.q, 1, old, out, sizeof(out), &n), "stale-collect");
    CHECK(KrnServiceCancel(&f.q, 1, t) && KrnServiceComplete(&f.q, t, NULL, 0), "retire-new");
    /* Force the boundary without billions of requests: never wrap/reuse it. */
    for (unsigned i = 0; i < KRN_SERVICE_SLOTS; ++i) f.q.slots[i].generation = UINT32_MAX;
    CHECK(!KrnServiceSubmit(&f.q, 1, NULL, 0, &t), "generation-exhausted");
    finish(&f);
}
static void bounds(void)
{
    struct Fixture f; init(&f); uint64_t tokens[KRN_SERVICE_SLOTS], t = 42;
    CHECK(!KrnServiceSubmit(&f.q, 0, NULL, 0, &t), "zero-owner");
    CHECK(!KrnServiceSubmit(&f.q, 1, NULL, 1, &t), "null-input");
    CHECK(!KrnServiceSubmit(&f.q, 1, "x", SIZE_MAX, &t) && t == 42, "input-overflow");
    for (unsigned i = 0; i < KRN_SERVICE_SLOTS; ++i) tokens[i] = submit(&f, i + 1);
    CHECK(!KrnServiceSubmit(&f.q, 20, NULL, 0, &t), "queue-capacity");
    CHECK(!KrnServiceCancel(&f.q, 1, 0) && !KrnServiceCancel(&f.q, 1, UINT64_MAX), "invalid-token");
    for (unsigned i = 0; i < KRN_SERVICE_SLOTS; ++i) CHECK(KrnServiceCancel(&f.q, i + 1, tokens[i]), "drain-slot");
    CHECK(KrnServiceSubmit(&f.q, 1, NULL, 0, &t), "empty-submit");
    size_t n = 99;
    CHECK(KrnServiceTake(&f.q, t, NULL, 0, &n) && n == 0, "empty-take");
    CHECK(KrnServiceComplete(&f.q, t, NULL, 0), "empty-complete");
    CHECK(KrnServiceCollect(&f.q, 1, t, NULL, 0, &n) && n == 0, "empty-collect");
    finish(&f);
}
static void mixed_lifetime(void)
{
    struct Fixture f; init(&f);
    struct KrnMappingDomainPin mapping = {0};
    CHECK(KrnMappingDomainRetainMapping(&f.d, &mapping), "retain-mapping");
    uint64_t queued = submit(&f, 1), running = submit(&f, 2), ready = submit(&f, 3);
    (void)queued; take(&f, running); take(&f, ready);
    CHECK(KrnServiceComplete(&f.q, ready, "ok", 2), "mixed-ready");
    KrnServiceClose(&f.q);
    CHECK(KrnMappingDomainLeave(&f.creator), "mixed-creator-leave");
    CHECK(!KrnMappingDomainCanReclaim(&f.d), "mixed-retained");
    CHECK(KrnServiceComplete(&f.q, running, NULL, 0), "mixed-ack");
    CHECK(KrnServiceDrained(&f.q) && !KrnMappingDomainCanReclaim(&f.d), "mapping-outlives-queue");
    CHECK(KrnMappingDomainRelease(&mapping) && KrnMappingDomainCanReclaim(&f.d), "mapping-final-drain");
}
static void repeated(void)
{
    struct Fixture f; init(&f);
    unsigned char input[KRN_SERVICE_BYTES], output[KRN_SERVICE_BYTES];
    for (unsigned i = 0; i < KRN_SERVICE_BYTES; ++i) input[i] = (unsigned char)i;
    for (unsigned round = 0; round < 1000; ++round) {
        uint64_t t; size_t n = 0;
        CHECK(KrnServiceSubmit(&f.q, 1, input, sizeof(input), &t), "repeat-submit");
        CHECK(KrnServiceTake(&f.q, t, output, sizeof(output), &n) && n == sizeof(input), "repeat-take");
        for (unsigned i = 0; i < sizeof(output); ++i) output[i] ^= 0x5a;
        CHECK(KrnServiceComplete(&f.q, t, output, sizeof(output)), "repeat-complete");
        CHECK(KrnServiceCollect(&f.q, 1, t, output, sizeof(output), &n), "repeat-collect");
        for (unsigned i = 0; i < sizeof(output); ++i) CHECK(output[i] == (unsigned char)(i ^ 0x5a), "repeat-bytes");
        CHECK(KrnServiceDrained(&f.q) && f.d.members == &f.creator && !f.creator.next, "repeat-members");
    }
    finish(&f);
}
int main(void)
{
    normal(); cancellation(); close_races(); stale(); bounds(); mixed_lifetime(); repeated();
    puts("PASS service ownership cancellation close generations bounds");
    return 0;
}
