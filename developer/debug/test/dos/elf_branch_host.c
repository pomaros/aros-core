/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1. */
#include <assert.h>
#include <stdio.h>
#include "../../../../rom/dos/elf_branch.h"
int main(void)
{
    uintptr_t base = UINT64_C(0x400000006100);
    struct elf_branch_pool pool = {0};
    uint64_t size = 147230588;
    assert(elf_branch_reachable(base, base + 0x7fffffc));
    assert(!elf_branch_reachable(base, base + 0x8000000));
    assert(elf_branch_reachable(base + 0x8000000, base));
    assert(!elf_branch_reachable(base + 0x8000004, base));
    assert(!elf_branch_reachable(base, base + 1));
    assert(!elf_branch_reachable(UINTPTR_MAX - 3, 0));
    assert(elf_branch_reserve(&pool, size, 0x2c654));
    assert(elf_branch_reserve(&pool, size, size - 4));
    assert(pool.before_bytes == 16 && pool.after_bytes == 16);
    assert(!elf_branch_reserve(&pool, size, size));
    assert(!elf_branch_reserve(&pool, size, 1));
    pool.before = base - 16;
    pool.after = (base + size + 15) & ~(uintptr_t)15;
    /* Process the tail first: it must not consume a front slot even if reachable. */
    assert(elf_branch_slot(&pool, base + size - 4, size - 4, size) == pool.after);
    assert(elf_branch_slot(&pool, base + 0x2c654, 0x2c654, size) == pool.before);
    assert(!elf_branch_slot(&pool, base + 0x2c654, 0x2c654, size));
    pool.before_used = 0; pool.before = base - 0x8000004;
    assert(!elf_branch_slot(&pool, base, 0, size));
    assert(pool.before_used == 0);
    pool.before_bytes = UINT32_MAX - 15;
    assert(!elf_branch_reserve(&pool, size, 0));
    {
        struct elf_branch_function entries[1024];
        struct elf_branch_symbols symbols = { entries, 1024 };
        unsigned i;
        for (i = 0; i < 1024; ++i) {
            entries[i].section = 1 + (i % 7);
            entries[i].value = (1023 - i) * UINT64_C(4);
        }
        elf_branch_function_sort(&symbols);
        for (i = 0; i < 1024; ++i)
            assert(elf_branch_function_contains(&symbols, 1+(i%7), (1023-i)*4));
        for (i = 1; i < 1024; ++i)
            assert(!elf_branch_function_less(entries[i], entries[i-1]));
        assert(!elf_branch_function_contains(&symbols, 8, 0));
        assert(!elf_branch_function_contains(&symbols, 1, 1));
        symbols.count = 0; elf_branch_function_sort(&symbols);
        assert(!elf_branch_function_contains(&symbols, 1, 0));
    }
    puts("elf-branch-host PASS");
    return 0;
}
