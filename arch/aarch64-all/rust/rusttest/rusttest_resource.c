/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: rusttest.resource - the C shell of a module implemented in Rust.

    The module's RomTag, base, LVO table and register convention are the
    ordinary genmodule output for rusttest.conf; each LVO calls one
    extern "C" function of the Rust staticlib in rust/. The three functions
    below are the only AROS services the Rust code uses: its global
    allocator calls AllocVec and FreeVec, and its panic handler reports and
    stops. It is the reference for Rust kernel-side modules on native
    AArch64 AROS; loaded from disk with C:LoadResident.
*/

#include <aros/debug.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <exec/alerts.h>
#include <exec/memory.h>
#include <exec/types.h>

#include <proto/exec.h>

#include <stddef.h>

#include "rusttest_intern.h"

/* The Rust global allocator does its own alignment over these blocks. */
void *rusttest_c_allocvec(size_t size)
{
    return AllocVec(size, MEMF_ANY);
}

void rusttest_c_freevec(void *block)
{
    FreeVec(block);
}

/* panic = "abort": there is no unwinding to return through. */
void rusttest_c_panic(void)
{
    bug("[RUSTTEST] rust panic\n");
    Alert(AT_DeadEnd | AN_Unknown);
    for (;;)
        Wait(0);
}

static int rusttest_init(struct RustTestBase *base)
{
    base->rust_version = rusttest_rs_version();
    bug("[RUSTTEST] init: rust version 0x%08lx\n",
        (unsigned long)base->rust_version);
    return TRUE;
}

AROS_LH0(ULONG, RustTestVersion,
    struct RustTestBase *, RustTestBase, 1, Rusttest)
{
    AROS_LIBFUNC_INIT

    return rusttest_rs_version();

    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, RustTestAdd,
    AROS_LHA(LONG, a, D0),
    AROS_LHA(LONG, b, D1),
    struct RustTestBase *, RustTestBase, 2, Rusttest)
{
    AROS_LIBFUNC_INIT

    return rusttest_rs_add(a, b);

    AROS_LIBFUNC_EXIT
}

AROS_LH1(ULONG, RustTestAlloc,
    AROS_LHA(ULONG, count, D0),
    struct RustTestBase *, RustTestBase, 3, Rusttest)
{
    AROS_LIBFUNC_INIT

    return rusttest_rs_alloc_checksum(count);

    AROS_LIBFUNC_EXIT
}

AROS_LH0(ULONG, RustTestLive,
    struct RustTestBase *, RustTestBase, 4, Rusttest)
{
    AROS_LIBFUNC_INIT

    return rusttest_rs_live_blocks();

    AROS_LIBFUNC_EXIT
}

ADD2INITLIB(rusttest_init, 0)
