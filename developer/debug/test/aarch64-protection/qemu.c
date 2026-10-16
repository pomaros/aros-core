/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Standalone EL1 fixture: this is not an AROS task/recovery acceptance test.
 */
#include "protection.h"
#include "guarded_stack.h"

#ifndef PAGE_BITS
#define PAGE_BITS 12
#endif
#define PAGE_BYTES (1U << PAGE_BITS)
static uint64_t root[PAGE_BYTES / 8] __attribute__((aligned(PAGE_BYTES)));
static uint64_t pool[2][PAGE_BYTES / 8] __attribute__((aligned(PAGE_BYTES)));
static unsigned used;
static volatile unsigned expected, faults;
static uint64_t live_tcr;
static unsigned stack_released;
#define TEST_ADDRESS UINT64_C(0x80000000)

static void puts_uart(const char *s)
{
    volatile unsigned *uart = (volatile unsigned *)(uintptr_t)0x09000000;
    while (*s) *uart = (unsigned char)*s++;
}
static void finish(unsigned result)
{
    uint64_t block[2] = { UINT64_C(0x20026), result };
    register uint64_t x0 __asm__("x0") = 0x20;
    register uint64_t x1 __asm__("x1") = (uintptr_t)block;
    __asm__ volatile("hlt #0xf000" : "+r"(x0), "+r"(x1) :: "memory");
    for (;;) __asm__ volatile("wfe");
}
static void check(int condition)
{
    if (!condition) {
        puts_uart("AARCH64-PROTECTION QEMU FAIL\n");
        finish(1);
    }
}
static uint64_t *resolve(void *ctx, uint64_t pa)
{
    (void)ctx;
    return (uint64_t *)(uintptr_t)pa;
}
static uint64_t allocate(void *ctx, unsigned bits)
{
    (void)ctx;
    check(bits == PAGE_BITS);
    return used < 2 ? (uintptr_t)pool[used++] : 0;
}
static void clean(void *ctx, const void *p, size_t size)
{
    uintptr_t at, end = (uintptr_t)p + size;
    uint64_t ctr;
    uintptr_t line;
    (void)ctx;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line = UINT64_C(4) << ((ctr >> 16) & 15);
    for (at = (uintptr_t)p & ~(line - 1); at < end; at += line)
        __asm__ volatile("dc cvac, %0" :: "r"(at) : "memory");
}
static void invalidate(void *ctx)
{
    (void)ctx;
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}
static void sync_code(void *ctx, uint64_t va, size_t size)
{
    uint64_t ctr, at, end = va + size, dline, iline;
    (void)ctx;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    dline = UINT64_C(4) << ((ctr >> 16) & 15);
    iline = UINT64_C(4) << (ctr & 15);
    for (at = va & ~(dline - 1); at < end; at += dline)
        __asm__ volatile("dc cvau, %0" :: "r"(at) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    for (at = va & ~(iline - 1); at < end; at += iline)
        __asm__ volatile("ic ivau, %0" :: "r"(at) : "memory");
    __asm__ volatile("dsb ish; isb" ::: "memory");
}
static const struct AArch64ProtectionOps ops = {
    NULL, resolve, allocate, clean, invalidate, sync_code, NULL
};

static void *stack_allocate(void *context, size_t size)
{
    (void)context;
    check(size == 16384 + 65536);
    return (void *)(uintptr_t)TEST_ADDRESS;
}
static int stack_protect(void *context, void *base, size_t size, int no_access)
{
    uint64_t address = (uintptr_t)base;
    (void)context;
    for (size_t offset = 0; offset < size; offset += PAGE_BYTES)
        if (!AArch64ProtectPage(&ops, (uintptr_t)root, live_tcr,
                address + offset, no_access ? 0 : 3))
            return 0;
    return 1;
}
static void stack_release(void *context, void *base, size_t size)
{
    (void)context;
    check((uintptr_t)base == TEST_ADDRESS && size == 81920);
    ++stack_released;
}
static const struct AArch64GuardedStackOps stack_ops = {
    NULL, stack_allocate, stack_protect, stack_release
};

void fault(void)
{
    uint64_t esr, far, elr;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
    /* Only the explicitly armed single store may recover. */
    check(expected && (esr >> 26) == 0x25 && (esr & 0x40) &&
        (esr & 63) == expected && far == TEST_ADDRESS);
    expected = 0;
    ++faults;
    __asm__ volatile("msr elr_el1, %0" :: "r"(elr + 4));
}

static void write_fixture(void)
{
    __asm__ volatile("str wzr, [%0]" :: "r"(TEST_ADDRESS) : "memory");
}

int main(void)
{
    struct AArch64GuardedStack stack = {0};
    uint64_t sctlr, mmfr;
    uint64_t tcr = 25 | (UINT64_C(1) << 8) | (UINT64_C(1) << 10) |
        (UINT64_C(3) << 12) | (UINT64_C(2) << 32);
    *(volatile unsigned *)(uintptr_t)0x09000030 = 0x301; /* UARTEN, TXE, RXE */
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr));
#if PAGE_BITS == 12
    check(((mmfr >> 28) & 15) != 15);
    root[0] = UINT64_C(0x401); /* device UART */
    root[1] = UINT64_C(0x40000705); /* 1GB identity RAM, normal WB */
    root[2] = UINT64_C(0x80000705); /* separate block: safe to split */
#else
    check(((mmfr >> 20) & 15) != 0);
    tcr |= UINT64_C(2) << 14;
    root[0] = (uintptr_t)pool[used++] | 3;
    pool[0][4] = UINT64_C(0x08000401); /* 32MB device block: UART */
    pool[0][32] = UINT64_C(0x40000705); /* code, stack, tables */
    pool[0][64] = UINT64_C(0x80000705); /* separate fixture block */
#endif
    __asm__ volatile("msr mair_el1, %0; msr tcr_el1, %1; msr ttbr0_el1, %2;"
        "dsb sy; isb" :: "r"(UINT64_C(0xff00)), "r"(tcr),
        "r"((uintptr_t)root) : "memory");
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= 1;
    __asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(sctlr) : "memory");
    live_tcr = tcr;

    write_fixture();
    check(faults == 0);
    puts_uart("AARCH64-PROTECTION control no-guard: access completed\n");
    check(AArch64GuardedStackCreate(&stack_ops, &stack, 65536, 16384, PAGE_BYTES));
    check((uintptr_t)stack.lower == TEST_ADDRESS + 16384 &&
        (uintptr_t)stack.upper == TEST_ADDRESS + 81920);
    *(volatile uint64_t *)stack.lower = 42; /* stack adjacent to guard is usable */
    expected = 7; /* level-three translation fault */
    write_fixture();
    check(faults == 1 && !expected);
    check(AArch64GuardedStackDestroy(&stack_ops, &stack));
    check(stack_released == 1 && !stack.allocation);
    write_fixture();
    check(faults == 1);
    *(volatile uint32_t *)(uintptr_t)TEST_ADDRESS = UINT32_C(0xd65f03c0); /* ret */
    check(AArch64ProtectPage(&ops, (uintptr_t)root, tcr, TEST_ADDRESS, 0));
    /* Granting execute directly from a guard must synchronize via a valid VA. */
    check(AArch64ProtectPage(&ops, (uintptr_t)root, tcr, TEST_ADDRESS, 5));
    ((void (*)(void))(uintptr_t)TEST_ADDRESS)();
    expected = 15; /* level-three permission fault */
    write_fixture();
    check(faults == 2 && !expected);
    check(!AArch64ProtectPage(&ops, (uintptr_t)root, tcr, TEST_ADDRESS, 7));
    check(AArch64ProtectPage(&ops, (uintptr_t)root, tcr, TEST_ADDRESS, 3));
    write_fixture();
    check(faults == 2);
    puts_uart("AARCH64-PROTECTION QEMU PASS guard-fault=1 restore=1 rx-exec=1 ro-fault=1 wx-rejected=1\n");
    finish(0);
    return 0;
}
