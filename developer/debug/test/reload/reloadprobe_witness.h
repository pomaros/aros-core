/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1. */
#ifndef RELOADPROBE_WITNESS_H
#define RELOADPROBE_WITNESS_H
#include <exec/types.h>
#define RELOAD_WITNESS_MAGIC 0x524c4431UL
/* Owned by the caller, kept alive until the final CloseLibrary returns.
 * A worker publishes completion after that close; message ownership orders
 * witness reads. No IRQ or DMA ownership is represented by these counters. */
struct ReloadWitness {
    ULONG magic;
    ULONG attached;
    ULONG released;
    ULONG generation;
};
#endif
