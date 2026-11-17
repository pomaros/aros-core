/*
 * Copyright (C) 2026 John KNIPPER
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef AROS_PAGE_RELEASE_PROOF_H
#define AROS_PAGE_RELEASE_PROOF_H

#include <stdint.h>

/* A command may load libraries which stay resident after it returns. Compare
 * the load's increase with the unload's decrease, excluding execution between
 * them. This is an aggregate allocator check, not per-allocation leak tracing;
 * unrelated concurrent frees can increase the observed decrease. */
static inline int AROSCodePagesRecovered(uint64_t before_load,
    uint64_t after_load, uint64_t before_unload, uint64_t after_unload)
{
    return after_load > before_load && before_unload >= after_unload &&
        before_unload - after_unload >= after_load - before_load;
}

#endif
