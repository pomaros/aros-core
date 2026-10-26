/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "domain.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct KrnMappingDomain domain = {0}, other = {0};
    struct KrnMappingDomainMember creator = {0}, worker = {0}, duplicate = {0};
    struct KrnMappingDomainPin first = {0}, second = {0}, io = {0}, cleanup = {0};
    int creator_identity, worker_identity;
    KrnMappingDomainInitialize(&domain);
    assert(!KrnMappingDomainCanReclaim(&domain));
    assert(!KrnMappingDomainRetainMapping(&domain, &first));
    assert(KrnMappingDomainJoin(&domain, &creator, &creator_identity));
    assert(!KrnMappingDomainJoin(&other, &creator, &worker_identity));
    assert(!KrnMappingDomainJoin(&domain, &duplicate, &creator_identity));
    assert(KrnMappingDomainJoin(&domain, &worker, &worker_identity));
    assert(KrnMappingDomainRetainMapping(&domain, &first));
    assert(!KrnMappingDomainRetainMapping(&domain, &first));
    struct KrnMappingDomainPin copied = first;
    assert(!KrnMappingDomainBeginIo(&copied, &io, 0));
    assert(!KrnMappingDomainRelease(&copied));
    struct KrnMappingDomainMember copied_member = creator;
    assert(!KrnMappingDomainLeave(&copied_member));
    assert(KrnMappingDomainBeginIo(&first, &io, 0));
    assert(!KrnMappingDomainBeginIo(&first, &io, 0));
    assert(!KrnMappingDomainRelease(&first));

    /* The creator can leave while another worker and an I/O remain. */
    assert(KrnMappingDomainLeave(&creator));
    assert(!KrnMappingDomainLeave(&creator));
    assert(!domain.closing);
    assert(KrnMappingDomainRetainMapping(&domain, &second));
    assert(!KrnMappingDomainCanReclaim(&domain));
    assert(KrnMappingDomainLeave(&worker));
    assert(domain.closing && !KrnMappingDomainCanReclaim(&domain));
    assert(!KrnMappingDomainJoin(&domain, &creator, &creator_identity));
    assert(!KrnMappingDomainBeginIo(&second, &cleanup, 0));
    assert(KrnMappingDomainBeginIo(&second, &cleanup, 1));
    assert(!KrnMappingDomainRelease(&second));
    assert(!KrnMappingDomainRelease(&first));
    assert(KrnMappingDomainRelease(&io));
    assert(!KrnMappingDomainRelease(&io));
    assert(KrnMappingDomainRelease(&first));
    assert(!KrnMappingDomainCanReclaim(&domain));
    assert(KrnMappingDomainRelease(&cleanup));
    assert(KrnMappingDomainRelease(&second));
    assert(KrnMappingDomainCanReclaim(&domain));
    assert(!KrnMappingDomainRelease(&second));

    /* Explicit stop closes admission without pretending live workers detached. */
    assert(KrnMappingDomainJoin(&other, &creator, &creator_identity));
    KrnMappingDomainStop(&other);
    KrnMappingDomainStop(&other);
    assert(!KrnMappingDomainCanReclaim(&other));
    assert(!KrnMappingDomainRetainMapping(&other, &first));
    assert(!KrnMappingDomainJoin(&other, &worker, &worker_identity));
    assert(KrnMappingDomainLeave(&creator));
    assert(KrnMappingDomainCanReclaim(&other));
    puts("mapping domain HOST: PASS (worker survival, I/O pins, close and drain)");
    return 0;
}
