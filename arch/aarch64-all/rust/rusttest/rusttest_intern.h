/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

#ifndef AROS_AARCH64_RUSTTEST_INTERN_H
#define AROS_AARCH64_RUSTTEST_INTERN_H

#include <exec/nodes.h>
#include <exec/types.h>

struct RustTestBase {
    struct Node node;
    ULONG rust_version;         /* rusttest_rs_version() at init */
};

/* The Rust staticlib's exports (rust/src/lib.rs). */
ULONG rusttest_rs_version(void);
LONG rusttest_rs_add(LONG a, LONG b);
ULONG rusttest_rs_alloc_checksum(ULONG count);
ULONG rusttest_rs_live_blocks(void);

#endif
