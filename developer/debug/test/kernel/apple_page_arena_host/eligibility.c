/* Copyright (C) 2026 John Knipper. Licensed under the AROS Public License 1.1. */
#include <aros/apple/runtime_local.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void *SysBase;
static struct AROSAppleRuntimeLocal local;
static int supervisor, nesting, pinned, pins;
static uint64_t mask, dispatch_mask;

static int AROSAppleRuntimeIsSuper(void) { return supervisor; }
static uint64_t cpu_RequiredCurrentTaskMasks(void)
{
    assert(pinned);
    return AROSAppleRequiredIRQMasks(&local, nesting);
}
static void Forbid(void)
{
    assert(!pinned);
    pinned = 1;
    ++pins;
    /* A task could have migrated between the first read and Forbid(). */
    mask = dispatch_mask;
}
static void Permit(void) { assert(pinned); pinned = 0; }

#include "arena-eligibility.inc"

static void check(uint64_t initial, uint64_t sampled, int allow)
{
    mask = initial;
    dispatch_mask = sampled;
    assert(arena_can_grow() == allow);
    assert(!pinned);
}

int main(void)
{
    SysBase = &local;
    nesting = -1;
    local.fiq_ready = 1;
    check(0x80, 0x80, 1);
    local.irq_ready = 1;
    check(0, 0, 1);
    check(0x80, 0x80, 0);
    check(0x80, 0, 1);
    local.fiq_ready = 0;
    check(0x40, 0x40, 1);
    local.irq_ready = 0;
    check(0x40, 0xc0, 0);
    local.fiq_ready = 1;
    local.irq_mask_depth = 1;
    check(0x80, 0x80, 0);
    local.irq_mask_depth = 0;
    local.interrupt_depth = 1;
    check(0x80, 0x80, 0);
    local.interrupt_depth = 0;
    nesting = 0;
    check(0x80, 0x80, 0);
    int before = pins;
    check(0xc0, 0xc0, 0);
    supervisor = 1;
    check(0, 0, 0);
    supervisor = 0;
    SysBase = NULL;
    check(0, 0, 0);
    assert(pins == before);
    puts("arena growth context PASS");
}
