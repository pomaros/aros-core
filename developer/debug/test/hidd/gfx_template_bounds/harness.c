/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef uint8_t UBYTE;
typedef uint16_t UWORD;
typedef int16_t WORD;
typedef uint32_t ULONG;
typedef void VOID;
typedef int OOP_Class;
typedef struct { ULONG fg, bg; int expansion, mode; } OOP_Object;
#define GC_FG(gc) ((gc)->fg)
#define GC_BG(gc) ((gc)->bg)
#define GC_COLEXP(gc) ((gc)->expansion)
#define GC_DRMD(gc) ((gc)->mode)
#define vHidd_GC_ColExp_Transparent 1
#define vHidd_GC_DrawMode_Invert 2
#define AROS_BE2WORD(x) __builtin_bswap16(x)
#define AROS_BIG_ENDIAN 0
struct Template {
    OOP_Object *gc;
    UBYTE *masktemplate;
    ULONG modulo, dstMod;
    WORD srcx, x, y, width, height;
    void *dst;
    int inverttemplate;
};
#define pHidd_BitMap_PutMemTemplate8 Template
#define pHidd_BitMap_PutMemTemplate16 Template
#define pHidd_BitMap_PutMemTemplate24 Template
#define pHidd_BitMap_PutMemTemplate32 Template

/* The runner inserts the four actual production functions here. */
/* PRODUCTION_FUNCTIONS */

static void invoke(unsigned bytes, struct Template *msg)
{
    switch (bytes) {
    case 1: BM__Hidd_BitMap__PutMemTemplate8(NULL, NULL, msg); break;
    case 2: BM__Hidd_BitMap__PutMemTemplate16(NULL, NULL, msg); break;
    case 3: BM__Hidd_BitMap__PutMemTemplate24(NULL, NULL, msg); break;
    case 4: BM__Hidd_BitMap__PutMemTemplate32(NULL, NULL, msg); break;
    }
}

int main(void)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    UBYTE *mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
    const unsigned widths[] = {64, 1, 7, 8, 15, 16, 17, 63, 65};
    _Alignas(8) UBYTE actual[8192], expected[8192];
    unsigned cases = 0;
    if (mapping == MAP_FAILED || mprotect(mapping + page, page, PROT_NONE))
        return 2;
    for (unsigned bytes = 1; bytes <= 4; ++bytes)
    for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w)
    for (unsigned srcx = 0; srcx < 18; ++srcx)
    for (unsigned mode = 0; mode < 6; ++mode)
    for (unsigned height = 1; height <= 8; height += 7) {
        unsigned width = widths[w];
        size_t modulo = (srcx + width + 7) / 8;
        size_t input_bytes = modulo * height;
        UBYTE *input = mapping + page - input_bytes;
        OOP_Object gc = {0x12345678, 0xaabbccdd,
                         mode / 2 == 0 ? vHidd_GC_ColExp_Transparent : 0,
                         mode / 2 == 1 ? vHidd_GC_DrawMode_Invert : 0};
        struct Template msg = {&gc, input, (ULONG)modulo,
            (width + 6) * bytes, (WORD)srcx, 2, 1,
            (WORD)width, (WORD)height, actual, (int)(mode & 1)};
        memset(actual, 0x5a, sizeof(actual));
        memcpy(expected, actual, sizeof(actual));
        for (size_t i = 0; i < input_bytes; ++i)
            input[i] = (UBYTE)(19 + i * 53);
        for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            unsigned pixel = srcx + x;
            int set = !!(input[y * modulo + pixel / 8] & (0x80 >> (pixel % 8)));
            size_t at = (y + 1) * msg.dstMod + (x + 2) * bytes;
            set ^= mode & 1;
            for (unsigned channel = 0; channel < bytes; ++channel) {
                if (mode / 2 == 1) {
                    if (set) expected[at + channel] ^= 0xff;
                } else if (set || mode / 2 == 2) {
                    ULONG color = set ? gc.fg : gc.bg;
                    expected[at + channel] = (UBYTE)(color >> (channel * 8));
                }
            }
        }
        invoke(bytes, &msg);
        if (memcmp(actual, expected, sizeof(actual))) {
            fprintf(stderr, "FAIL bytes=%u width=%u srcx=%u mode=%u height=%u\n",
                    bytes, width, srcx, mode, height);
            return 1;
        }
        ++cases;
    }
    /* Empty rectangles must not read an inaccessible source or touch output. */
    OOP_Object gc = {0};
    struct Template empty = {&gc, mapping + page, 1, 4, 0, 0, 0, 0, 8, actual, 0};
    for (unsigned bytes = 1; bytes <= 4; ++bytes) {
        invoke(bytes, &empty);
        empty.width = 8; empty.height = 0;
        invoke(bytes, &empty);
        empty.width = 0; empty.height = 8;
    }
    munmap(mapping, page * 2);
    printf("gfx-template-bounds result=PASS cases=%u formats=8,16,24,32 modes=6 guard=PROT_NONE\n", cases);
    return 0;
}
