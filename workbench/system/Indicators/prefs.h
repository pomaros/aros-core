/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#ifndef INDICATORS_PREFS_H
#define INDICATORS_PREFS_H
#include <stddef.h>
#define INDICATORS_PREFS_PATH "SYS/Indicators.prefs"
#define INDICATORS_ALL_ENABLED 15U
#define INDICATORS_PREFS_SIZE 5
/* Bits follow IndicatorKind: battery, network, Bluetooth, sound.
 * Invalid/partial writes never replace the last applied configuration. */
static int IndicatorsPrefsDecode(const unsigned char *bytes, size_t size, unsigned *mask)
{
    if (size != INDICATORS_PREFS_SIZE || bytes[0]!='I' || bytes[1]!='N' ||
        bytes[2]!='D' || bytes[3]!='1' || bytes[4]>INDICATORS_ALL_ENABLED) return 0;
    *mask=bytes[4]; return 1;
}
static void IndicatorsPrefsEncode(unsigned char *bytes, unsigned mask)
{
    bytes[0]='I'; bytes[1]='N'; bytes[2]='D'; bytes[3]='1';
    bytes[4]=(unsigned char)(mask & INDICATORS_ALL_ENABLED);
}
#endif
