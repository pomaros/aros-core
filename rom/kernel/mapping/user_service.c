/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "user_service.h"
_Static_assert(KBUF_BYTES <= KRN_SERVICE_BYTES, "staging capacity");
static int finish(struct KrnBufferService *r)
{
    if (!KrnBufferRelease(&r->output)) return 0;
    *r = (struct KrnBufferService){0}; return 1;
}
int KrnBufferServiceSubmit(struct KrnBufferRegistry *registry, uint64_t principal,
    uint64_t handle, struct KrnServiceQueue *queue, uintptr_t input,
    uintptr_t output, size_t length, const struct KrnBufferOps *ops,
    struct KrnBufferService *r)
{
    struct KrnBufferSpace *s = KrnBufferLookup(registry, principal, handle);
    if (!r || r->queue || r->output.space || !s || !queue || queue->domain != s->domain) return 0;
    struct KrnBufferLease in = {0}; unsigned char bytes[KBUF_BYTES];
    if (!KrnBufferPin(registry, principal, handle, input, length, KMAP_READ, ops, &in)) return 0;
    if (!KrnBufferPin(registry, principal, handle, output, length, KMAP_WRITE, ops, &r->output)) {
        (void)KrnBufferRelease(&in); return 0;
    }
    int ok = KrnBufferRead(&in, principal, bytes, sizeof(bytes), ops);
    (void)KrnBufferRelease(&in);
    uint64_t token;
    if (!ok || !KrnServiceSubmit(queue, principal, bytes, length, &token)) {
        (void)KrnBufferRelease(&r->output); return 0;
    }
    r->queue = queue; r->principal = principal; r->token = token; r->state = KS_QUEUED;
    return 1;
}
int KrnBufferServiceTake(struct KrnBufferService *r, uint64_t token, void *bytes, size_t cap, size_t *length)
{
    if (!r || !r->queue || r->token != token || r->state != KS_QUEUED ||
        !KrnServiceTake(r->queue, r->token, bytes, cap, length)) return 0;
    r->state = KS_RUNNING; return 1;
}
int KrnBufferServiceComplete(struct KrnBufferService *r, uint64_t token, const void *bytes, size_t length)
{
    if (!r || !r->queue || r->token != token || (r->state != KS_RUNNING && r->state != KS_CANCELLED)) return 0;
    int discarded = r->state == KS_CANCELLED || r->queue->domain->closing;
    if (!discarded && length != r->output.length) return 0;
    if (!KrnServiceComplete(r->queue, r->token, bytes, length)) return 0;
    if (discarded) return finish(r);
    r->state = KS_READY; return 1;
}
int KrnBufferServiceCancel(struct KrnBufferService *r, uint64_t principal, uint64_t token)
{
    if (!r || !r->queue || r->principal != principal || r->token != token ||
        !KrnServiceCancel(r->queue, principal, token)) return 0;
    if (r->state == KS_RUNNING || r->state == KS_CANCELLED) {
        r->state = KS_CANCELLED; return 1;
    }
    return finish(r);
}
int KrnBufferServiceCollect(struct KrnBufferService *r, uint64_t principal,
    uint64_t token, const struct KrnBufferOps *ops)
{
    if (!r || !r->queue || r->principal != principal || r->token != token || r->state != KS_READY) return 0;
    unsigned char bytes[KBUF_BYTES]; size_t length = 0;
    int collected = KrnServiceCollect(r->queue, principal, token, bytes, sizeof(bytes), &length);
    if (!collected && !KrnServiceCancel(r->queue, principal, token)) return 0;
    int copied = collected && KrnBufferWrite(&r->output, principal, bytes, length, ops);
    return finish(r) && copied;
}
