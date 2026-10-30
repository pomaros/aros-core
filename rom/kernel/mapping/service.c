/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "service.h"
#include <string.h>

static struct KrnServiceSlot *lookup(struct KrnServiceQueue *q, uint64_t token)
{
    uint32_t index = (uint32_t)token;
    if (!q || !q->domain || !index || index > KRN_SERVICE_SLOTS)
        return NULL;
    struct KrnServiceSlot *s = &q->slots[index - 1];
    return s->state != KS_FREE && s->generation == (uint32_t)(token >> 32)
        ? s : NULL;
}

static int retire(struct KrnServiceSlot *s)
{
    if (!KrnMappingDomainLeave(&s->retention))
        return 0;
    uint32_t generation = s->generation;
    *s = (struct KrnServiceSlot){0};
    s->generation = generation;
    return 1;
}

int KrnServiceInitialize(struct KrnServiceQueue *q, struct KrnMappingDomain *d)
{
    if (!q || !d || q->domain || d->closing || !d->members)
        return 0;
    q->domain = d;
    return 1;
}

int KrnServiceSubmit(struct KrnServiceQueue *q, uint64_t owner,
    const void *input, size_t length, uint64_t *token)
{
    if (!q || !q->domain || q->domain->closing || !owner || !token ||
        length > KRN_SERVICE_BYTES || (length && !input))
        return 0;
    struct KrnServiceSlot *available = NULL;
    unsigned index = 0;
    for (unsigned i = 0; i < KRN_SERVICE_SLOTS; ++i) {
        struct KrnServiceSlot *s = &q->slots[i];
        if (s->state != KS_FREE && s->owner == owner)
            return 0;
        if (!available && s->state == KS_FREE && s->generation != UINT32_MAX) {
            available = s;
            index = i;
        }
    }
    if (!available || !KrnMappingDomainJoin(q->domain,
        &available->retention, available))
        return 0;
    ++available->generation;
    available->owner = owner;
    available->length = length;
    if (length)
        memcpy(available->bytes, input, length);
    available->state = KS_QUEUED;
    *token = ((uint64_t)available->generation << 32) | (index + 1);
    return 1;
}

int KrnServiceTake(struct KrnServiceQueue *q, uint64_t token,
    void *input, size_t capacity, size_t *length)
{
    struct KrnServiceSlot *s = lookup(q, token);
    if (!s || q->domain->closing || s->state != KS_QUEUED ||
        !length || capacity < s->length || (s->length && !input))
        return 0;
    if (s->length)
        memcpy(input, s->bytes, s->length);
    *length = s->length;
    s->state = KS_RUNNING;
    return 1;
}

int KrnServiceComplete(struct KrnServiceQueue *q, uint64_t token,
    const void *reply, size_t length)
{
    struct KrnServiceSlot *s = lookup(q, token);
    if (!s || (s->state != KS_RUNNING && s->state != KS_CANCELLED))
        return 0;
    /* Cancellation acknowledges the lease without inspecting discarded data. */
    if (s->state == KS_CANCELLED || q->domain->closing)
        return retire(s);
    if (length > KRN_SERVICE_BYTES || (length && !reply))
        return 0;
    if (length)
        memcpy(s->bytes, reply, length);
    s->length = length;
    s->state = KS_READY;
    return 1;
}

int KrnServiceCollect(struct KrnServiceQueue *q, uint64_t owner, uint64_t token,
    void *reply, size_t capacity, size_t *length)
{
    struct KrnServiceSlot *s = lookup(q, token);
    if (!s || s->owner != owner || q->domain->closing || s->state != KS_READY ||
        !length || capacity < s->length || (s->length && !reply))
        return 0;
    /* Validate retention before publishing any output. */
    size_t n = s->length;
    unsigned char copy[KRN_SERVICE_BYTES];
    if (n)
        memcpy(copy, s->bytes, n);
    if (!retire(s))
        return 0;
    if (n)
        memcpy(reply, copy, n);
    *length = n;
    return 1;
}

static int cancel(struct KrnServiceSlot *s)
{
    if (s->state == KS_RUNNING || s->state == KS_CANCELLED) {
        s->state = KS_CANCELLED;
        return 1;
    }
    return retire(s);
}

int KrnServiceCancel(struct KrnServiceQueue *q, uint64_t owner, uint64_t token)
{
    struct KrnServiceSlot *s = lookup(q, token);
    return s && s->owner == owner ? cancel(s) : 0;
}

void KrnServiceClose(struct KrnServiceQueue *q)
{
    if (!q || !q->domain)
        return;
    KrnMappingDomainStop(q->domain);
    for (unsigned i = 0; i < KRN_SERVICE_SLOTS; ++i)
        if (q->slots[i].state != KS_FREE)
            (void)cancel(&q->slots[i]);
}

int KrnServiceDrained(const struct KrnServiceQueue *q)
{
    if (!q || !q->domain)
        return 0;
    for (unsigned i = 0; i < KRN_SERVICE_SLOTS; ++i)
        if (q->slots[i].state != KS_FREE || q->slots[i].retention.domain)
            return 0;
    return 1;
}
