/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1. */
#ifndef RELOADPROBE_BASE_H
#define RELOADPROBE_BASE_H
#include <exec/libraries.h>
#include "reloadprobe_witness.h"
struct ReloadProbeBase {
    struct Library library;
    APTR allocation;
    struct ReloadWitness *witness;
};
#endif
