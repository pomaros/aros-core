/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef WIFI_SECRET_H
#define WIFI_SECRET_H
/* Stable byte representation, shared by prefs, supplicant and FullMAC driver. */
struct wifi_secret {
    unsigned char version; /* 1 */
    unsigned char security; /* 1 WEP, 2 WPA-PSK */
    unsigned char hex;
    char key[65];
};
static inline int wifi_secret_valid(const struct wifi_secret *secret)
{
    unsigned length=0,i;
    if(secret->version!=1 || secret->hex>1 || secret->key[64]) return 0;
    while(length<64 && secret->key[length]) length++;
    if(secret->security==2) {
        if(secret->hex ? length!=64 : (length<8 || length>63)) return 0;
    } else if(secret->security==1) {
        if(secret->hex ? (length!=10 && length!=26) : (length!=5 && length!=13)) return 0;
    } else return 0;
    for(i=0;i<length;i++) {
        unsigned char c=secret->key[i];
        if(c<32) return 0;
        if(secret->hex && !((c>='0' && c<='9') || (c>='A' && c<='F') ||
                           (c>='a' && c<='f'))) return 0;
    }
    return 1;
}
static inline void wifi_secret_wipe(void *data, unsigned long size)
{
    volatile unsigned char *p = data;
    while (size--) *p++ = 0;
}
#endif
