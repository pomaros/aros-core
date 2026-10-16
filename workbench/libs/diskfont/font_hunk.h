/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Single-segment bitmap fonts are data, never executable allocations.
 */
#ifndef DISKFONT_FONT_HUNK_H
#define DISKFONT_FONT_HUNK_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct FontData { uint8_t *data; size_t size; };
static uint32_t FontBE32(const uint8_t *p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 |
        (uint32_t)p[2]<<8 | p[3];
}
static int FontDataRange(const struct FontData *image, const void *p, size_t n)
{
    uintptr_t start=(uintptr_t)image->data, value=(uintptr_t)p;
    return value >= start && value-start <= image->size &&
        n <= image->size-(value-start);
}
/* Decode in place. marks has at least (length+15)/16 zeroed bytes.
 * Relative tokens use offset+4 so relocated offset zero differs from NULL.
 * Return 0 for another HUNK profile, -1 for malformed data, 1 on success.
 */
static int FontHunkDecode(uint8_t *file, size_t length, uint8_t *marks,
                          struct FontData *image)
{
    size_t pos, bytes;
    int ended=0;
    if (length<4 || FontBE32(file)!=1011) return 0;
    if (length<24) return -1;
    if (FontBE32(file+4)!=0 || FontBE32(file+8)!=1 ||
        FontBE32(file+12)!=0 || FontBE32(file+16)!=0) return 0;
    if (length<32) return -1;
    bytes=(size_t)(FontBE32(file+20)&0x3fffffffU)*4;
    if ((FontBE32(file+24)!=1001 && FontBE32(file+24)!=1002) ||
        bytes<110 || bytes>length-32 || FontBE32(file+28)!=bytes/4)
        return -1;
    image->data=file+32; image->size=bytes;
    pos=32+bytes;
    while (pos<length) {
        uint32_t kind;
        if (length-pos<4 || ended) return -1;
        kind=FontBE32(file+pos); pos+=4;
        if (kind==1010) { ended=1; continue; }
        if (kind==1004) {
            for (;;) {
                uint32_t count;
                if (length-pos<4) return -1;
                count=FontBE32(file+pos); pos+=4;
                if (!count) break;
                if (length-pos<4 || FontBE32(file+pos)!=0) return -1;
                pos+=4;
                if (count>(length-pos)/4) return -1;
                while (count--) {
                    uint32_t offset=FontBE32(file+pos), value;
                    size_t bit=offset/2;
                    pos+=4;
                    if ((offset&1) || offset>bytes-4 ||
                        (marks[bit/8] & (1U<<(bit%8)))) return -1;
                    marks[bit/8] |= 1U<<(bit%8);
                    value=FontBE32(image->data+offset);
                    if (value>=bytes || value>UINT32_MAX-4) return -1;
                    value+=4;
                    image->data[offset]=value>>24;
                    image->data[offset+1]=value>>16;
                    image->data[offset+2]=value>>8;
                    image->data[offset+3]=value;
                }
            }
        } else if (kind==1009) { /* debug payload */
            uint32_t words;
            if (length-pos<4) return -1;
            words=FontBE32(file+pos); pos+=4;
            if (words>(length-pos)/4) return -1;
            pos+=(size_t)words*4;
        } else if (kind==1008) { /* symbol names and values */
            for (;;) {
                uint32_t words;
                if (length-pos<4) return -1;
                words=FontBE32(file+pos); pos+=4;
                if (!words) break;
                if (length-pos<4 || words>(length-pos-4)/4) return -1;
                pos+=(size_t)words*4+4;
            }
        } else return -1;
    }
    return ended ? 1 : -1;
}
#endif
