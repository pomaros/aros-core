/* Copyright (C) 2026 John KNIPPER.
 * Licensed under the AROS Public License (APL) Version 1.1. */

#ifndef AROS_SHA256_H
#define AROS_SHA256_H
#include <stddef.h>
#include <stdint.h>
#define AROS_SHA256_SIZE 32U
struct AROSSHA256Context {
    uint32_t state[8];
    uint64_t byte_count;
    uint8_t block[64];
    uint32_t used;
};
void AROSSHA256Init(struct AROSSHA256Context *context);
void AROSSHA256Update(struct AROSSHA256Context *context, const uint8_t *data, size_t size);
void AROSSHA256Final(struct AROSSHA256Context *context, uint8_t digest[AROS_SHA256_SIZE]);
void AROSSHA256(const void *data, size_t size, uint8_t digest[AROS_SHA256_SIZE]);
#endif
