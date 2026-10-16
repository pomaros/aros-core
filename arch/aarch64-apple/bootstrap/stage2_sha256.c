/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Small freestanding SHA-256 used to bind early physical observations
 * to the exact final device tree consumed by stage two.
 */

#include "stage2_sha256.h"

struct SHA256Context {
    uint32_t state[8];
    uint64_t byte_count;
    uint8_t block[64];
    uint32_t used;
};

static uint32_t rotate_right(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32U - shift));
}

static uint32_t read_be32(const uint8_t *value)
{
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
        ((uint32_t)value[2] << 8) | value[3];
}

static void transform(struct SHA256Context *context, const uint8_t block[64])
{
    static const uint32_t constants[64] = {
        UINT32_C(0x428a2f98), UINT32_C(0x71374491),
        UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
        UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
        UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
        UINT32_C(0xd807aa98), UINT32_C(0x12835b01),
        UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
        UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe),
        UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
        UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
        UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
        UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa),
        UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
        UINT32_C(0x983e5152), UINT32_C(0xa831c66d),
        UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
        UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
        UINT32_C(0x06ca6351), UINT32_C(0x14292967),
        UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138),
        UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
        UINT32_C(0x650a7354), UINT32_C(0x766a0abb),
        UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
        UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
        UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
        UINT32_C(0xd192e819), UINT32_C(0xd6990624),
        UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
        UINT32_C(0x19a4c116), UINT32_C(0x1e376c08),
        UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
        UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
        UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
        UINT32_C(0x748f82ee), UINT32_C(0x78a5636f),
        UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
        UINT32_C(0x90befffa), UINT32_C(0xa4506ceb),
        UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
    };
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t index;

    for (index = 0; index < 16U; ++index)
        words[index] = read_be32(block + index * 4U);
    for (; index < 64U; ++index) {
        uint32_t left = words[index - 15U];
        uint32_t right = words[index - 2U];
        uint32_t sigma0 = rotate_right(left, 7U) ^
            rotate_right(left, 18U) ^ (left >> 3U);
        uint32_t sigma1 = rotate_right(right, 17U) ^
            rotate_right(right, 19U) ^ (right >> 10U);

        words[index] = words[index - 16U] + sigma0 +
            words[index - 7U] + sigma1;
    }
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0; index < 64U; ++index) {
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t sigma0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
            rotate_right(a, 22U);
        uint32_t sigma1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
            rotate_right(e, 25U);
        uint32_t temporary1 = h + sigma1 + choice + constants[index] +
            words[index];
        uint32_t temporary2 = sigma0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void update(
    struct SHA256Context *context, const uint8_t *data, size_t size)
{
    size_t take;
    size_t index;

    context->byte_count += size;
    while (size != 0) {
        take = 64U - context->used;
        if (take > size)
            take = size;
        for (index = 0; index < take; ++index)
            context->block[context->used + index] = data[index];
        context->used += (uint32_t)take;
        data += take;
        size -= take;
        if (context->used == 64U) {
            transform(context, context->block);
            context->used = 0;
        }
    }
}

void AROSAppleStage2SHA256(
    const void *data, size_t size,
    uint8_t digest[AROS_APPLE_STAGE2_SHA256_SIZE])
{
    struct SHA256Context context;
    uint64_t bit_count;
    uint32_t index;

    context.state[0] = UINT32_C(0x6a09e667);
    context.state[1] = UINT32_C(0xbb67ae85);
    context.state[2] = UINT32_C(0x3c6ef372);
    context.state[3] = UINT32_C(0xa54ff53a);
    context.state[4] = UINT32_C(0x510e527f);
    context.state[5] = UINT32_C(0x9b05688c);
    context.state[6] = UINT32_C(0x1f83d9ab);
    context.state[7] = UINT32_C(0x5be0cd19);
    context.byte_count = 0;
    context.used = 0;
    for (index = 0; index < sizeof(context.block); ++index)
        context.block[index] = 0;
    update(&context, (const uint8_t *)data, size);
    bit_count = context.byte_count << 3;
    context.block[context.used++] = UINT8_C(0x80);
    if (context.used > 56U) {
        while (context.used < 64U)
            context.block[context.used++] = 0;
        transform(&context, context.block);
        context.used = 0;
    }
    while (context.used < 56U)
        context.block[context.used++] = 0;
    for (index = 0; index < 8U; ++index)
        context.block[56U + index] =
            (uint8_t)(bit_count >> (56U - index * 8U));
    transform(&context, context.block);
    for (index = 0; index < 8U; ++index) {
        digest[index * 4U] = (uint8_t)(context.state[index] >> 24);
        digest[index * 4U + 1U] = (uint8_t)(context.state[index] >> 16);
        digest[index * 4U + 2U] = (uint8_t)(context.state[index] >> 8);
        digest[index * 4U + 3U] = (uint8_t)context.state[index];
    }
}
