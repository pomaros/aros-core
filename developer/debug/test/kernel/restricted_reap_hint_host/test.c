/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
enum RestrictedState { R_FREE,R_ALLOCATING,R_LIVE,R_REAPING };
struct Space { unsigned stops; };
struct RestrictedDomain {
    struct RestrictedDomain *next;
    enum RestrictedState state;
    unsigned managed,closing,cancels;
    void *owner_context;
    struct Space space;
};
struct Restricted {
    struct Restricted *next;
    enum RestrictedState state;
    unsigned abandoned,suspend_count,wait_pending,close_calls;
    void *owner_context,*owner;
};
struct RestrictedShared {
    struct RestrictedShared *next;
    void *owner_context,*releaser_context;
    struct { unsigned closing,views,native_pending; } backing;
};
static struct Restricted *restricted_slots;
static struct RestrictedDomain *restricted_domains;
static struct RestrictedShared *restricted_shared;
static uint64_t owners_retired;
static unsigned orphan_reap_pending;
/* The HOST model deliberately keeps closures unclaimable. Production cleanup
 * must continue to be requested until a lease drains, not just until a claim
 * attempt has run. Native release, SVC routing and SMP require VM evidence. */
static int r_close(struct Restricted *r) { ++r->close_calls;return 0; }
static void r_clear_wait(struct Restricted *r) { r->wait_pending=0; }
static void r_cancel_waiters(struct RestrictedDomain *d) { ++d->cancels; }
static void AROSRestrictedSpaceStop(struct Space *s) { ++s->stops; }
#include "actual.inc"
int main(void)
{
    int a,b;
    assert(!r_refresh_reap_hint() && !orphan_reap_pending);
    struct Restricted other={.state=R_LIVE,.owner_context=&b,.owner=&b};
    struct Restricted r={.next=&other,.state=R_LIVE,.owner_context=&a,.owner=&a,
        .wait_pending=1,.suspend_count=2};
    struct RestrictedDomain foreign={.state=R_LIVE,.managed=1,.owner_context=&b};
    struct RestrictedDomain d={.next=&foreign,.state=R_LIVE,.managed=1,.owner_context=&a};
    struct RestrictedShared other_shared={.owner_context=&b};
    struct RestrictedShared shared={.next=&other_shared,.owner_context=&a,
        .backing={.views=1,.native_pending=1}};
    restricted_slots=&r;restricted_domains=&d;restricted_shared=&shared;
    r_mark_retired_resources(NULL);
    assert(!orphan_reap_pending && !owners_retired && r.owner_context==&a);
    assert(!r_refresh_reap_hint()); /* Live owned records are not orphan work. */
    r_mark_retired_resources(&a);
    assert(orphan_reap_pending && owners_retired==1);
    assert(!r.owner && !r.owner_context && r.abandoned && !r.wait_pending && !r.suspend_count);
    assert(r.close_calls==1 && r.state==R_LIVE); /* Worker lease prevents close. */
    assert(!d.owner_context && d.closing && d.space.stops==1 && d.cancels==1);
    assert(!shared.owner_context && shared.backing.closing);
    assert(other.owner_context==&b && !other.abandoned && !other.close_calls);
    assert(foreign.owner_context==&b && !foreign.closing && !foreign.space.stops);
    assert(other_shared.owner_context==&b && !other_shared.backing.closing);
    r_mark_retired_resources(NULL); /* A null identity cannot retire or recount orphans. */
    assert(owners_retired==1 && r.close_calls==1 && d.space.stops==1);
    assert(r_refresh_reap_hint() && orphan_reap_pending);
    r_mark_retired_resources(&a); /* Repeat must not count the dead owner twice. */
    assert(owners_retired==1 && r.close_calls==1 && orphan_reap_pending);

    restricted_domains=NULL;restricted_shared=NULL;
    assert(r_refresh_reap_hint());
    r.state=R_REAPING;assert(r_refresh_reap_hint()); /* Claim is not release. */
    r.state=R_FREE;assert(!r_refresh_reap_hint());
    restricted_slots=NULL;restricted_domains=&d;
    assert(r_refresh_reap_hint()); /* Managed domain still owns tables/regions. */
    d.state=R_REAPING;assert(r_refresh_reap_hint());
    d.state=R_ALLOCATING;assert(!r_refresh_reap_hint());
    d.state=R_LIVE;d.managed=0;assert(!r_refresh_reap_hint());
    restricted_domains=NULL;restricted_shared=&shared;
    assert(r_refresh_reap_hint()); /* Native/guest view has not drained. */
    shared.releaser_context=&b;assert(r_refresh_reap_hint()); /* Native release in progress. */
    restricted_shared=&other_shared;assert(!r_refresh_reap_hint() && !orphan_reap_pending);
    restricted_shared=NULL;
    r.state=R_LIVE;r.owner_context=&a;r.owner=&a;r.abandoned=0;r.next=NULL;
    restricted_slots=&r;r_mark_retired_resources(&a);
    assert(orphan_reap_pending && owners_retired==2 && r_refresh_reap_hint());
    restricted_slots=NULL;assert(!r_refresh_reap_hint() && !orphan_reap_pending);
    return 0;
}
