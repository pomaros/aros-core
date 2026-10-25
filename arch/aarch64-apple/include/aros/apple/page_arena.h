/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef AROS_APPLE_PAGE_ARENA_H
#define AROS_APPLE_PAGE_ARENA_H
#include <stdint.h>
#define AROS_APPLE_PAGE_SIZE UINT64_C(4096)
/*
 * Capacity of the W^X page arena that LoadSeg uses for every ELF hunk
 * (dos/loadseg.c AllocFunc under __AROSPLATFORM_WXSEG__). Stage two hands
 * half of the memory the loader reserved for it to the arena, up to this
 * cap; a program whose hunks do not fit fails to load with
 * ERROR_NO_FREE_STORE. Large static programs (a WebKit browser has about
 * 100 MiB of hunks, Chromium about 400 MiB) need the bundle to reserve
 * enough stage-two memory too.
 */
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
#define AROS_APPLE_PAGE_ARENA_MAX_BYTES (UINT64_C(256) * 1024U * 1024U)
#else
#define AROS_APPLE_PAGE_ARENA_MAX_BYTES (UINT64_C(1024) * 1024U * 1024U)
#endif
#endif
