/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef AROS_APPLE_PAGE_ARENA_H
#define AROS_APPLE_PAGE_ARENA_H
#include <stdint.h>
#define AROS_APPLE_PAGE_SIZE UINT64_C(4096)
/*
 * The page arena: identity-mapped RAM that every translation root maps with
 * 4 KiB leaves, so that a protection change never splits a block. It holds
 * page tables, DMA buffers and probes (KrnAllocPages without MEMF_PUBLIC or
 * MEMF_EXECUTABLE), guarded stacks and everything a restricted domain owns,
 * its guest memory included. ELF hunks and JIT regions are in the code window
 * (code_window.h), not here.
 *
 * Stage two hands the kernel the arena's first part: half of what remains of
 * its own allocation once the kernel is loaded, at most this many bytes, and
 * gives the rest of that allocation to Exec. The kernel grows the arena with
 * 2 MiB-aligned chunks of Exec memory when a request does not fit, and gives
 * a chunk back to Exec once nothing in it is in use (page_allocator.c).
 */
#define AROS_APPLE_PAGE_ARENA_MAX_BYTES (UINT64_C(64) * 1024U * 1024U)
/* The unit of growth: one level-2 block of the identity map. */
#define AROS_APPLE_PAGE_ARENA_BLOCK (UINT64_C(2) * 1024U * 1024U)
#endif
