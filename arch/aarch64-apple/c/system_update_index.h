/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef APPLE_SYSTEM_UPDATE_INDEX_H
#define APPLE_SYSTEM_UPDATE_INDEX_H
#include <stddef.h>
/* The system package: the whole SYS tree, published by tools/sysupdate. */
#define SYSTEM_UPDATE_PACKAGE "aros-system"
/* A delivered channel's index is read whole into this much. */
#define SYSTEM_UPDATE_INDEX_MAX 2048
/* Shared depot: 0 for another package type (a boot generation, a channel
 * without the system package, an older format), 1 for a pkg channel index
 * that lists the system package, -1 for an index that names the system
 * package but is malformed or truncated: only that blocks. A pkg channel
 * index is one line per version: name, version, CPU, the manifest's
 * SHA-256; the final LF is optional. */
int SystemUpdateIndexKind(const char *text, size_t size);
#endif
