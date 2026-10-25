/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef KEYRING_BLUETOOTH_SECRET_H
#define KEYRING_BLUETOOTH_SECRET_H
#include <stdint.h>
/* Service-private, byte-exact keyring payload. The IFF layout belongs only to
 * the explicit importer. All byte arrays retain Bluetooth wire ordering. */
struct BtKeyCfg
{
    uint8_t bkc_Version;                  /* version 1 */
    uint8_t bkc_Flags;                      /* BKCF_xxx */
    uint8_t bkc_LinkKeyType;                /* BR/EDR link key type */
    uint8_t bkc_LinkKey[16];                /* BR/EDR link key */
    uint8_t bkc_LTK[16];                    /* LE long term key */
    uint8_t bkc_EDIV[2];
    uint8_t bkc_Rand[8];
    uint8_t bkc_IRK[16];
    uint8_t bkc_CSRK[16];
    uint8_t bkc_Reserved[4];
    /* Key our peripheral role distributed to a remote central.  It cannot
       share bkc_LTK, which is the peer key used when we are central. */
    uint8_t bkc_LocalLTK[16];
    uint8_t bkc_LocalEDIV[2];
    uint8_t bkc_LocalRand[8];
};

_Static_assert(sizeof(struct BtKeyCfg)==107,"Bluetooth keyring payload ABI");
#endif
