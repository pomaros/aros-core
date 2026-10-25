/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: RustTest - call every rusttest.resource LVO and check the results.

    The resource must be loaded first (C:LoadResident DEVS:rusttest.resource).
    The RustTestAlloc digests are computed here in C from the same recipe,
    so a PASS means the Rust collections held the right values in memory
    from the Rust global allocator. RustTestLive must be 0 afterwards: every
    block went back through FreeVec. One line per check, then
    "RustTest: result=PASS" (return code 0) or result=FAIL (10).
*/

#include <dos/dos.h>
#include <exec/types.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/rusttest.h>

APTR RustTestBase;

#define RUSTTEST_MAGIC 0x52555354UL

static ULONG fnv(ULONG hash, ULONG value)
{
    return (hash ^ value) * 16777619UL;
}

/* The recipe of rusttest_rs_alloc_checksum in rust/src/lib.rs. */
static ULONG expected_digest(ULONG count)
{
    static const char line[] = "rust-resource\n";
    ULONG hash = 2166136261UL;
    ULONG i, j;

    for (i = 0; i < count; ++i)
        hash = fnv(hash, i * 2654435761UL);
    for (i = 0; i < count / 10; ++i)
        for (j = 0; j < sizeof(line) - 1; ++j)
            hash = fnv(hash, (UBYTE)line[j]);
    for (i = 0; i < count; ++i) {
        hash = fnv(hash, i);
        hash = fnv(hash, (count - 1 - i) * 3);
    }
    return hash;
}

static const char *verdict(int ok)
{
    return ok ? "PASS" : "FAIL";
}

int main(void)
{
    static const ULONG counts[] = { 1000, 4096 };
    ULONG version, digest, expected, live, index;
    LONG sum;
    int ok = 1, good;

    RustTestBase = OpenResource("rusttest.resource");
    if (RustTestBase == NULL) {
        Printf("RustTest: rusttest.resource not loaded\n");
        Printf("RustTest: result=FAIL\n");
        return RETURN_ERROR;
    }
    Printf("RustTest: rusttest.resource at 0x%lx\n", (unsigned long)(IPTR)RustTestBase);

    version = RustTestVersion();
    good = version == RUSTTEST_MAGIC;
    ok &= good;
    Printf("RustTestVersion() = 0x%08lx %s\n", (unsigned long)version, verdict(good));

    sum = RustTestAdd(40, 2);
    good = sum == 42;
    ok &= good;
    Printf("RustTestAdd(40, 2) = %ld %s\n", (long)sum, verdict(good));

    sum = RustTestAdd(0x7fffffff, 1);
    good = sum == (LONG)0x80000000UL;
    ok &= good;
    Printf("RustTestAdd(2147483647, 1) = %ld %s\n", (long)sum, verdict(good));

    for (index = 0; index < sizeof(counts) / sizeof(counts[0]); ++index) {
        digest = RustTestAlloc(counts[index]);
        expected = expected_digest(counts[index]);
        good = digest == expected;
        ok &= good;
        Printf("RustTestAlloc(%lu) = 0x%08lx expected 0x%08lx %s\n", (unsigned long)counts[index],
            (unsigned long)digest, (unsigned long)expected, verdict(good));
    }

    digest = RustTestAlloc(0);
    good = digest == 0;
    ok &= good;
    Printf("RustTestAlloc(0) = %lu %s\n", (unsigned long)digest, verdict(good));

    live = RustTestLive();
    good = live == 0;
    ok &= good;
    Printf("RustTestLive() = %lu %s\n", (unsigned long)live, verdict(good));

    Printf("RustTest: result=%s\n", ok ? "PASS" : "FAIL");
    return ok ? RETURN_OK : RETURN_ERROR;
}
