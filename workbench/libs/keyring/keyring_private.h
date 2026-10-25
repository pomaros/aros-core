/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef KEYRING_PRIVATE_H
#define KEYRING_PRIVATE_H
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <dos/bptr.h>
#include "engine.h"
struct kr_context {
    struct kr_context *next;
    unsigned char owner[32];
    APTR module;
};
struct KeyringBase {
    struct Library library;
    struct SignalSemaphore mutex;
    struct kr_engine *engine;
    struct kr_context *contexts;
    int prompted;
    BPTR home_lock;
    char home[1024], vault[1024], pending[1024], backup[1024];
    /* The machine store: the services' secrets (Wi-Fi, Bluetooth bonds),
     * sealed with a key kept beside it, so a boot joins the network and
     * reconnects devices without a passphrase. */
    struct kr_engine *machine;
};
int kr_prompt_passphrase(unsigned char *buffer, size_t capacity, size_t *length);
#endif
