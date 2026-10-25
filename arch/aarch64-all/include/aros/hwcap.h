/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: AArch64 CPU features, as returned by KrnGetSystemAttr(KATTR_HWCap)
 *       and KrnGetSystemAttr(KATTR_HWCap2).
 *
 * The bit numbers are those of the Linux AArch64 AT_HWCAP and AT_HWCAP2
 * words, so feature probes written for Linux map one to one. A bit is set
 * only when every CPU in the system has the feature.
 */
#ifndef AROS_AARCH64_HWCAP_H
#define AROS_AARCH64_HWCAP_H

#include <stdint.h>

#define AROS_HWCAP_FP           (1UL << 0)
#define AROS_HWCAP_ASIMD        (1UL << 1)
#define AROS_HWCAP_AES          (1UL << 3)
#define AROS_HWCAP_PMULL        (1UL << 4)
#define AROS_HWCAP_SHA1         (1UL << 5)
#define AROS_HWCAP_SHA2         (1UL << 6)
#define AROS_HWCAP_CRC32        (1UL << 7)
#define AROS_HWCAP_ATOMICS      (1UL << 8)
#define AROS_HWCAP_FPHP         (1UL << 9)
#define AROS_HWCAP_ASIMDHP      (1UL << 10)
#define AROS_HWCAP_ASIMDRDM     (1UL << 12)
#define AROS_HWCAP_JSCVT        (1UL << 13)
#define AROS_HWCAP_FCMA         (1UL << 14)
#define AROS_HWCAP_LRCPC        (1UL << 15)
#define AROS_HWCAP_DCPOP        (1UL << 16)
#define AROS_HWCAP_SHA3         (1UL << 17)
#define AROS_HWCAP_SM3          (1UL << 18)
#define AROS_HWCAP_SM4          (1UL << 19)
#define AROS_HWCAP_ASIMDDP      (1UL << 20)
#define AROS_HWCAP_SHA512       (1UL << 21)
#define AROS_HWCAP_SVE          (1UL << 22)
#define AROS_HWCAP_ASIMDFHM     (1UL << 23)
#define AROS_HWCAP_DIT          (1UL << 24)
#define AROS_HWCAP_ILRCPC       (1UL << 26)
#define AROS_HWCAP_FLAGM        (1UL << 27)
#define AROS_HWCAP_SB           (1UL << 29)

#define AROS_HWCAP2_SVE2        (1UL << 1)
#define AROS_HWCAP2_FRINT       (1UL << 8)
#define AROS_HWCAP2_I8MM        (1UL << 13)
#define AROS_HWCAP2_BF16        (1UL << 14)
#define AROS_HWCAP2_RNG         (1UL << 16)

static inline unsigned int AROSAArch64IDField(uint64_t reg, unsigned int shift)
{
    return (unsigned int)((reg >> shift) & 0xf);
}

/* Decode ID_AA64ISAR0_EL1, ID_AA64ISAR1_EL1, ID_AA64PFR0_EL1 and
 * ID_AA64ZFR0_EL1 (0 when SVE is absent) into the two feature words. */
static inline void AROSAArch64DecodeHWCap(uint64_t isar0, uint64_t isar1,
    uint64_t pfr0, uint64_t zfr0, uint64_t *hwcap, uint64_t *hwcap2)
{
    uint64_t caps = 0, caps2 = 0;
    unsigned int fp = AROSAArch64IDField(pfr0, 16);
    unsigned int simd = AROSAArch64IDField(pfr0, 20);
    unsigned int aes = AROSAArch64IDField(isar0, 4);
    unsigned int sha2 = AROSAArch64IDField(isar0, 12);
    unsigned int lrcpc = AROSAArch64IDField(isar1, 20);

    /* FP and AdvSIMD are signed fields: 0xf means not implemented. */
    if (fp != 0xf) caps |= AROS_HWCAP_FP;
    if (fp == 1) caps |= AROS_HWCAP_FPHP;
    if (simd != 0xf) caps |= AROS_HWCAP_ASIMD;
    if (simd == 1) caps |= AROS_HWCAP_ASIMDHP;

    if (aes >= 1) caps |= AROS_HWCAP_AES;
    if (aes >= 2) caps |= AROS_HWCAP_PMULL;
    if (AROSAArch64IDField(isar0, 8) >= 1) caps |= AROS_HWCAP_SHA1;
    if (sha2 >= 1) caps |= AROS_HWCAP_SHA2;
    if (sha2 >= 2) caps |= AROS_HWCAP_SHA512;
    if (AROSAArch64IDField(isar0, 16) >= 1) caps |= AROS_HWCAP_CRC32;
    if (AROSAArch64IDField(isar0, 20) >= 2) caps |= AROS_HWCAP_ATOMICS;
    if (AROSAArch64IDField(isar0, 28) >= 1) caps |= AROS_HWCAP_ASIMDRDM;
    if (AROSAArch64IDField(isar0, 32) >= 1) caps |= AROS_HWCAP_SHA3;
    if (AROSAArch64IDField(isar0, 36) >= 1) caps |= AROS_HWCAP_SM3;
    if (AROSAArch64IDField(isar0, 40) >= 1) caps |= AROS_HWCAP_SM4;
    if (AROSAArch64IDField(isar0, 44) >= 1) caps |= AROS_HWCAP_ASIMDDP;
    if (AROSAArch64IDField(isar0, 48) >= 1) caps |= AROS_HWCAP_ASIMDFHM;
    if (AROSAArch64IDField(isar0, 52) >= 1) caps |= AROS_HWCAP_FLAGM;
    if (AROSAArch64IDField(isar0, 60) >= 1) caps2 |= AROS_HWCAP2_RNG;

    if (AROSAArch64IDField(isar1, 0) >= 1) caps |= AROS_HWCAP_DCPOP;
    if (AROSAArch64IDField(isar1, 12) >= 1) caps |= AROS_HWCAP_JSCVT;
    if (AROSAArch64IDField(isar1, 16) >= 1) caps |= AROS_HWCAP_FCMA;
    if (lrcpc >= 1) caps |= AROS_HWCAP_LRCPC;
    if (lrcpc >= 2) caps |= AROS_HWCAP_ILRCPC;
    if (AROSAArch64IDField(isar1, 32) >= 1) caps2 |= AROS_HWCAP2_FRINT;
    if (AROSAArch64IDField(isar1, 36) >= 1) caps |= AROS_HWCAP_SB;
    if (AROSAArch64IDField(isar1, 44) >= 1) caps2 |= AROS_HWCAP2_BF16;
    if (AROSAArch64IDField(isar1, 52) >= 1) caps2 |= AROS_HWCAP2_I8MM;

    if (AROSAArch64IDField(pfr0, 48) >= 1) caps |= AROS_HWCAP_DIT;
    if (AROSAArch64IDField(pfr0, 32) >= 1)
    {
        caps |= AROS_HWCAP_SVE;
        if (AROSAArch64IDField(zfr0, 0) >= 1) caps2 |= AROS_HWCAP2_SVE2;
    }

    *hwcap = caps;
    *hwcap2 = caps2;
}

#endif
