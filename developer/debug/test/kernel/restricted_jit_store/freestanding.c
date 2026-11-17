/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <stdint.h>
#include <stddef.h>
void *memcpy(void *d,const void *s,size_t n) { unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;++i)a[i]=b[i];return d; }
void *memset(void *d,int v,size_t n) { unsigned char *a=d;for(size_t i=0;i<n;++i)a[i]=(unsigned char)v;return d; }
int memcmp(const void *a,const void *b,size_t n) { const unsigned char *x=a,*y=b;for(size_t i=0;i<n;++i)if(x[i]!=y[i])return x[i]-y[i];return 0; }
static void puts_raw(const char *s) { volatile uint32_t *uart=(void *)0x09000000;while(*s){while(uart[6]&(1U<<5)){}uart[0]=(unsigned char)*s++;} }
static __attribute__((noreturn)) void finish(unsigned code) {
    uint64_t args[2]={0x20026,code};
    register uint64_t x0 __asm__("x0")=0x20;
    register uint64_t x1 __asm__("x1")=(uintptr_t)args;
    __asm__ volatile("hlt #0xf000" : "+r"(x0),"+r"(x1) : : "memory");
    for(;;) __asm__ volatile("wfe");
}
void fail(const char *s) { puts_raw("FAIL ");puts_raw(s);puts_raw("\n");finish(1); }
void passed(void) { puts_raw("PASS release stores and restricted admission\n");finish(0); }
