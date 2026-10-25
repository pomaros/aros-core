/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: Apple Silicon back-end for entropy.resource.

    On Apple machines m1n1 asks the SEP's hardware generator for 128 bytes
    (falling back to the 64 bytes of iBoot's /chosen/random-seed) and passes
    them to the next stage as /chosen/rng-seed, the property Linux's
    add_bootloader_randomness() consumes. The kernel takes that seed once at
    EL1, wipes it from the tree, and hands it out through
    AROS_APPLE_KATTR_BOOT_SEED. This back-end folds it into the ChaCha20
    state at initialisation and clears the kernel's copy, so the seed exists
    in exactly one place for as short a time as possible. The M1 has no
    RNDR instruction and no RNG block the application processor can reach,
    so later reseeds come from the generic software collector.
*/

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <aros/apple/startup.h>

#include "entropy_intern.h"

APTR KernelBase __attribute__((used)) = NULL;

void Entropy_HW_Init(struct EntropyBase *EntropyBase)
{
    struct AROSAppleBootSeed *seed;
    IPTR value;
    ULONG i;

    EntropyBase->eb_HWGather = NULL;
    EntropyBase->eb_HWData = NULL;
    EntropyBase->eb_Flags |= EIF_SOFTWARE;

    KernelBase = OpenResource("kernel.resource");
    if (KernelBase == NULL)
        return;
    value = KrnGetSystemAttr(AROS_APPLE_KATTR_BOOT_SEED);
    if (value == 0 || value == (IPTR)-1)
        return;
    seed = (struct AROSAppleBootSeed *)value;
    if (seed->length == 0 || seed->length > AROS_APPLE_BOOT_SEED_BYTES)
        return;

    Entropy_CRNG_Reseed(EntropyBase, seed->bytes, seed->length);
    /* 32 bytes from a hardware generator seed a 256-bit key fully. */
    if (seed->length >= 32)
    {
        EntropyBase->eb_Bits = ENTROPY_BITS_MAX;
        EntropyBase->eb_Flags |= EIF_HARDWARE;
    }
    for (i = 0; i < AROS_APPLE_BOOT_SEED_BYTES; i++)
        ((volatile UBYTE *)seed->bytes)[i] = 0;
    seed->length = 0;
}
