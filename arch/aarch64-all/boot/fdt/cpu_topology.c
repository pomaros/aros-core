/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "cpu_topology.h"
#include <libfdt.h>
static int property_is(const struct AROSEarlyFDT *f, int node, const char *key, const char *s)
{
    int n;const char *p=AROSEarlyFDTProperty(f,node,key,&n);
    if (!p || n<1) return 0;
    int i=0;
    while (i<n && s[i] && p[i]==s[i]) i++;
    return i==n-1 && p[i]==0 && s[i]==0;
}
int AROSCPUDiscover(const struct AROSEarlyFDT *f, uint64_t boot, struct AROSCPUTopology *out)
{
    if (!f || !out) return 0;
    out->count=0;out->boot_cpu=AROS_CPU_MAX;out->psci_available=out->psci_hvc=0;
    boot&=AROS_CPU_AFFINITY_MASK;
    int cpus=AROSEarlyFDTFindPath(f,"/cpus"),node,depth=0;
    if (cpus<0) return 0;
    for (node=AROSEarlyFDTNext(f,-1,&depth);node>=0;node=AROSEarlyFDTNext(f,node,&depth)) {
        if (AROSEarlyFDTEnabled(f,node) &&
            (AROSEarlyFDTCompatible(f,node,"arm,psci-0.2") ||
             AROSEarlyFDTCompatible(f,node,"arm,psci-1.0"))) {
            if (out->psci_available) return 0;
            if (property_is(f,node,"method","hvc")) out->psci_hvc=1;
            else if (!property_is(f,node,"method","smc")) return 0;
            out->psci_available=1;
        }
    }
    /* Visit direct CPU children without a parent lookup for every tree node. */
    for (node=fdt_first_subnode(f->blob,cpus);node>=0;
         node=fdt_next_subnode(f->blob,node)) {
        if (!property_is(f,node,"device_type","cpu")) continue;
        if (out->count==AROS_CPU_MAX) return 0;
        struct AROSCPUDescription *c=&out->cpu[out->count];
        uint64_t size;int length;
        const void *reg=AROSEarlyFDTProperty(f,node,"reg",&length);
        if (!reg || (length!=4 && length!=8) ||
            !AROSEarlyFDTRawReg(f,node,0,&c->affinity,&size) || size ||
            (c->affinity&~AROS_CPU_AFFINITY_MASK)) return 0;
        for (unsigned i=0;i<out->count;i++) if (out->cpu[i].affinity==c->affinity) return 0;
        c->enabled=AROSEarlyFDTEnabled(f,node);c->method=AROS_CPU_START_UNKNOWN;c->release_address=0;
        if (property_is(f,node,"enable-method","psci")) c->method=AROS_CPU_START_PSCI;
        if (property_is(f,node,"enable-method","spin-table")) {
            const fdt64_t *p=AROSEarlyFDTProperty(f,node,"cpu-release-addr",&length);
            if (!p || length!=8) {
                if (c->enabled && c->affinity!=boot) return 0;
            } else {
                c->release_address=fdt64_ld(p);
                if (!c->release_address || (c->release_address&7)) {
                    if (c->enabled && c->affinity!=boot) return 0;
                    c->release_address=0;
                }
            }
            c->method=AROS_CPU_START_SPIN_TABLE;
        }
        if (c->affinity==boot) {
            if (!c->enabled) return 0;
            out->boot_cpu=out->count;
        }
        out->count++;
    }
    return out->count && out->boot_cpu<out->count;
}

int AROSCPUReleasePage(const struct AROSEarlyFDT *f, uint64_t slot, uint64_t *page)
{
    if (!f || !page || !slot || (slot&7U) || slot >= (UINT64_C(1)<<48)) return 0;
    uint64_t start=slot&~UINT64_C(4095),end=start+4096;
    int ram=0,reserved=0;
    for (int node=fdt_first_subnode(f->blob,0); node>=0;
         node=fdt_next_subnode(f->blob,node)) {
        if (!property_is(f,node,"device_type","memory") || !AROSEarlyFDTEnabled(f,node)) continue;
        uint64_t address,length;
        for (unsigned i=0; AROSEarlyFDTRawReg(f,node,i,&address,&length); i++) {
            if (address<=start && length<=UINT64_MAX-address && length>=end-address) { ram=1; break; }
        }
    }
    int count=fdt_num_mem_rsv(f->blob);
    for (int i=0;i<count;i++) {
        uint64_t address,length;
        if (fdt_get_mem_rsv(f->blob,i,&address,&length)!=0) return 0;
        if (address<=start && length<=UINT64_MAX-address && length>=end-address) { reserved=1; break; }
    }
    if (!ram || !reserved) return 0;
    *page=start;
    return 1;
}
