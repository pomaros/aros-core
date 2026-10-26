/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "domain.h"
#include <stdint.h>

void KrnMappingDomainInitialize(struct KrnMappingDomain *domain)
{
    if (domain)
        *domain = (struct KrnMappingDomain){0};
}

int KrnMappingDomainJoin(struct KrnMappingDomain *domain,
    struct KrnMappingDomainMember *member, void *identity)
{
    if (!domain || !member || !identity || domain->closing || member->domain)
        return 0;
    for (struct KrnMappingDomainMember *m = domain->members; m; m = m->next)
        if (m == member || m->identity == identity)
            return 0;
    *member = (struct KrnMappingDomainMember){domain, domain->members, identity};
    domain->members = member;
    return 1;
}

int KrnMappingDomainLeave(struct KrnMappingDomainMember *member)
{
    if (!member || !member->domain)
        return 0;
    struct KrnMappingDomain *domain = member->domain;
    struct KrnMappingDomainMember **link = &domain->members;
    while (*link && *link != member)
        link = &(*link)->next;
    if (!*link)
        return 0;
    *link = member->next;
    *member = (struct KrnMappingDomainMember){0};
    if (!domain->members)
        domain->closing = 1;
    return 1;
}

void KrnMappingDomainStop(struct KrnMappingDomain *domain)
{
    if (domain)
        domain->closing = 1;
}

int KrnMappingDomainRetainMapping(struct KrnMappingDomain *domain,
    struct KrnMappingDomainPin *pin)
{
    if (!domain || !pin || pin->domain || domain->closing || !domain->members)
        return 0;
    *pin = (struct KrnMappingDomainPin){domain, domain->pins, NULL, 0,
        KDOMAIN_MAPPING};
    domain->pins = pin;
    return 1;
}

int KrnMappingDomainBeginIo(struct KrnMappingDomainPin *mapping,
    struct KrnMappingDomainPin *request, int cleanup)
{
    if (!mapping || !request || request == mapping || request->domain ||
        !mapping->domain || mapping->kind != KDOMAIN_MAPPING ||
        mapping->requests == SIZE_MAX ||
        (mapping->domain->closing && !cleanup))
        return 0;
    struct KrnMappingDomain *domain = mapping->domain;
    struct KrnMappingDomainPin *registered = domain->pins;
    while (registered && registered != mapping)
        registered = registered->next;
    if (!registered)
        return 0;
    *request = (struct KrnMappingDomainPin){domain, domain->pins, mapping, 0,
        KDOMAIN_IO};
    domain->pins = request;
    ++mapping->requests;
    return 1;
}

int KrnMappingDomainRelease(struct KrnMappingDomainPin *pin)
{
    if (!pin || !pin->domain || pin->requests ||
        (pin->kind != KDOMAIN_MAPPING && pin->kind != KDOMAIN_IO))
        return 0;
    struct KrnMappingDomain *domain = pin->domain;
    struct KrnMappingDomainPin **link = &domain->pins;
    while (*link && *link != pin)
        link = &(*link)->next;
    if (!*link)
        return 0;
    if (pin->kind == KDOMAIN_IO) {
        if (!pin->parent || pin->parent->domain != domain ||
            pin->parent->kind != KDOMAIN_MAPPING || !pin->parent->requests)
            return 0;
        --pin->parent->requests;
    }
    *link = pin->next;
    *pin = (struct KrnMappingDomainPin){0};
    return 1;
}

int KrnMappingDomainCanReclaim(const struct KrnMappingDomain *domain)
{
    return domain && domain->closing && !domain->members && !domain->pins;
}
