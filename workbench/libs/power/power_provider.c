/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    The generic provider: a machine that reports nothing about its power.
    A platform with a battery or an adapter replaces this file.
*/
#include "power_private.h"

BOOL power_ProviderInit(struct PowerBase *PowerBase)
{
    (void)PowerBase;
    return FALSE;
}

BOOL power_ProviderRead(struct PowerBase *PowerBase, struct PowerState *state)
{
    (void)PowerBase;
    (void)state;
    return FALSE;
}
