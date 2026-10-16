/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Licensed under the AROS Public License (APL) Version 1.1.
 * GICv2 operations adapted from aarch64-native/kernel/platform_bcm2711.c.
 * GICv3: Arm IHI 0069, single-security-state/non-secure EL1 interface.
 * All register bases and the interrupt supplier come from the firmware FDT.
 */
#include "gic.h"
#define R32(base, off) (*(volatile uint32_t *)((base) + (off)))
#define R64(base, off) (*(volatile uint64_t *)((base) + (off)))
#define R8(base, off) (*(volatile uint8_t *)((base) + (off)))
static void barrier(void) { __asm__ volatile("dsb sy\n\tisb" ::: "memory"); }
static int wait_clear(uintptr_t base, unsigned offset, uint32_t bits)
{
    for (unsigned i = 0; i < 1000000; ++i)
        if (!(R32(base, offset) & bits))
            return 1;
    return 0;
}
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
int AROSGICResolve(const struct AROSFDTIRQ *irq, uint32_t *id, uint32_t *trigger)
{
    if (!irq || !id || !trigger || (irq->count != 3 && irq->count != 4) ||
        (irq->count == 4 && irq->cells[3]) || irq->cells[0] > 1 ||
        (irq->cells[0] == 0 && irq->cells[1] > 987) || (irq->cells[0] == 1 && irq->cells[1] > 15))
        return 0;
    *trigger = irq->cells[2] & 15;
    if (*trigger != 1 && *trigger != 4 && !(irq->cells[0] == 1 && *trigger == 8))
        return 0;
    *id = irq->cells[1] + (irq->cells[0] ? 16 : 32);
    return 1;
}
static int initialize(struct AROSGIC *g, const struct AROSEarlyFDT *t, int node, int primary)
{
    uint64_t db, ds, cb, cs, mpidr;
    if (!g || !AROSEarlyFDTEnabled(t, node))
        return 0;
    g->version = 0;
    g->node = node;
    int v3 = AROSEarlyFDTCompatible(t, node, "arm,gic-v3");
    if (!v3 && !AROSEarlyFDTCompatible(t, node, "arm,gic-400") &&
        !AROSEarlyFDTCompatible(t, node, "arm,cortex-a15-gic"))
        return 0;
    if (!AROSEarlyFDTReg(t, node, 0, &db, &ds) || !AROSEarlyFDTReg(t, node, 1, &cb, &cs) ||
        (db & 0xfff) || (cb & 0xfff) || ds < (v3 ? 0x10000U : 0x1000U) ||
        cs < (v3 ? 0x20000U : 0x1000U))
        return 0;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    g->affinity = mpidr & UINT64_C(0xff00ffffff);
    g->distributor = (uintptr_t)db;
    g->cpu = (uintptr_t)cb;
    g->private = g->distributor;
    g->limit = ((R32(g->distributor, 4) & 31) + 1) * 32;
    if (g->limit > 1020)
        g->limit = 1020;
    if (primary) {
        R32(g->distributor, 0) = 0;
        barrier();
        if (v3 && !wait_clear(g->distributor, 0, 1U << 31))
            return 0;
        /* Firmware may leave SPIs enabled. Bootstrap owns this distributor and
         * must quarantine every external source before reopening its CPU line. */
        for (uint32_t id = 32; id < g->limit; id += 32)
            R32(g->distributor, 0x180 + 4 * (id / 32)) = UINT32_MAX;
        barrier();
        if (v3 && !wait_clear(g->distributor, 0, 1U << 31))
            return 0;
    } else if ((R32(g->distributor, 0) & (v3 ? 0x12U : 1U)) != (v3 ? 0x12U : 1U)) {
        return 0; /* The boot CPU must publish an initialized distributor. */
    }
    if (v3)
    {
        int length;
        const uint8_t *p = AROSEarlyFDTProperty(t, node, "#redistributor-regions", &length);
        if (p && (length != 4 || be32(p) != 1))
            return 0;
        uint64_t stride = 0;
        p = AROSEarlyFDTProperty(t, node, "redistributor-stride", &length);
        if (p)
        {
            if (length != 8)
                return 0;
            stride = (uint64_t)be32(p) << 32 | be32(p + 4);
            if (stride < 0x20000 || (stride & 0xffff))
                return 0;
        }
        uint32_t affinity = (uint32_t)(mpidr & 0xffffff) | ((uint32_t)(mpidr >> 32) & 255) << 24;
        int found = 0;
        for (uint64_t offset = 0; offset <= cs - 0x20000;)
        {
            uint64_t typer = R64((uintptr_t)(cb + offset), 8);
            if ((uint32_t)(typer >> 32) == affinity)
            {
                g->cpu = (uintptr_t)(cb + offset);
                g->private = g->cpu + 0x10000;
                found = 1;
                break;
            }
            uint64_t step = stride ? stride : ((typer & 2) ? 0x40000 : 0x20000);
            if ((typer & 16) || step > cs - offset)
                break;
            offset += step;
        }
        if (!found)
            return 0;
        R32(g->cpu, 0x14) &= ~2U;
        if (!wait_clear(g->cpu, 0x14, 4))
            return 0;
        R32(g->private, 0x180) = UINT32_MAX;
        if (!wait_clear(g->cpu, 0, 1U << 3))
            return 0;
        R32(g->private, 0x80) = UINT32_MAX;
        uint64_t value;
        __asm__ volatile("mrs %0, ICC_SRE_EL1" : "=r"(value));
        value |= 1;
        __asm__ volatile("msr ICC_SRE_EL1, %0\n\tisb" ::"r"(value) : "memory");
        __asm__ volatile("mrs %0, ICC_SRE_EL1" : "=r"(value));
        if (!(value & 1))
            return 0;
        value = 0;
        __asm__ volatile("msr ICC_CTLR_EL1, %0\n\tmsr ICC_BPR1_EL1, %0" ::"r"(value) : "memory");
        value = 0xff;
        __asm__ volatile("msr ICC_PMR_EL1, %0" ::"r"(value) : "memory");
        if (primary) {
            R32(g->distributor, 0) = 0x12; /* ARE_NS + EnableGrp1NS */
            if (!wait_clear(g->distributor, 0, 1U << 31)) return 0;
        }
        value = 1;
        __asm__ volatile("msr ICC_IGRPEN1_EL1, %0" ::"r"(value) : "memory");
        g->version = 3;
    }
    else
    {
        R32(g->cpu, 0) = 0;
        R32(g->cpu, 4) = 0xf0;
        R32(g->distributor, 0x180) = UINT32_MAX;
        if (primary) R32(g->distributor, 0) = 1;
        R32(g->cpu, 0) = 1;
        g->version = 2;
    }
    barrier();
    return 1;
}
int AROSGICInit(struct AROSGIC *g, const struct AROSEarlyFDT *t, int node)
{
    return initialize(g,t,node,1);
}
int AROSGICInitCPU(struct AROSGIC *g, const struct AROSEarlyFDT *t, int node)
{
    /* Only this CPU's banked/redistributor state may be reset here. */
    return initialize(g,t,node,0);
}
int AROSGICClearPending(struct AROSGIC *g, uint32_t id)
{
    if (!g || !g->version || id>=g->limit) return 0;
    uintptr_t base=id<32 ? g->private : g->distributor;
    R32(base,0x280+4*(id/32))=1U<<(id%32);
    barrier();
    return 1;
}
int AROSGICSendIPI(struct AROSGIC *g, uint32_t id, uint64_t affinity)
{
    /* GICv2 CPU target-list bits are not MPIDR affinities. Do not guess. */
    if (!g || g->version!=3 || id>15 || (affinity&~UINT64_C(0xff00ffffff)) ||
        (affinity&255)>15) return 0;
    uint64_t value=((affinity>>32)&255)<<48 | ((affinity>>16)&255)<<32 |
        (uint64_t)id<<24 | ((affinity>>8)&255)<<16 | UINT64_C(1)<<(affinity&255);
    barrier();
    __asm__ volatile("msr ICC_SGI1R_EL1, %0\n\tisb"::"r"(value):"memory");
    return 1;
}
int AROSGICEnable(struct AROSGIC *g, uint32_t id, uint32_t trigger)
{
    if (!g || !g->version || id >= g->limit ||
        (trigger != 1 && trigger != 4 && !(g->version == 2 && id >= 16 && id < 32 && trigger == 8)))
        return 0;
    uintptr_t base = id < 32 ? g->private : g->distributor;
    R8(base, 0x400 + id) = 0xa0;
    if (id >= 16)
    {
        unsigned off = 0xc00 + 4 * (id / 16), shift = (id % 16) * 2;
        uint32_t value = R32(base, off) & ~(2U << shift);
        if (trigger == 1)
            value |= 2U << shift;
        R32(base, off) = value;
    }
    if (g->version == 3)
    {
        R32(base, 0x80 + 4 * (id / 32)) |= 1U << (id % 32);
        if (id >= 32)
            R64(g->distributor, 0x6000 + 8 * id) = g->affinity;
    }
    else if (id >= 32)
    {
        /* Local CPU target mask is banked in the read-only SGI target byte. */
        R8(base, 0x800 + id) = R8(base, 0x800);
    }
    barrier();
    R32(base, 0x100 + 4 * (id / 32)) = 1U << (id % 32);
    barrier();
    return 1;
}
int AROSGICDisable(struct AROSGIC *g, uint32_t id)
{
    if (!g || !g->version || id >= g->limit)
        return 0;
    uintptr_t base = id < 32 ? g->private : g->distributor;
    R32(base, 0x180 + 4 * (id / 32)) = 1U << (id % 32);
    barrier();
    return g->version != 3 ||
           wait_clear(id < 32 ? g->cpu : g->distributor, 0, id < 32 ? 1U << 3 : 1U << 31);
}
uint32_t AROSGICAcknowledge(struct AROSGIC *g)
{
    uint64_t token;
    if (g->version == 3)
    {
        __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(token));
        barrier();
        return (uint32_t)token;
    }
    return R32(g->cpu, 0xc);
}
void AROSGICEnd(struct AROSGIC *g, uint32_t token)
{
    barrier();
    if (g->version == 3)
    {
        __asm__ volatile("msr ICC_EOIR1_EL1, %0" ::"r"((uint64_t)token) : "memory");
    }
    else
        R32(g->cpu, 0x10) = token;
    barrier();
}
int AROSGICSendSelfIPI(struct AROSGIC *g, uint32_t id)
{
    if (!g || !g->version || id > 15 || !AROSGICEnable(g, id, 1))
        return 0;
    barrier();
    if (g->version == 3)
    {
        return AROSGICSendIPI(g,id,g->affinity);
    }
    else
        R32(g->distributor, 0xf00) = 2U << 24 | id;
    return 1;
}
