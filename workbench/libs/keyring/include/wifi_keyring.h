/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef WIFI_KEYRING_H
#define WIFI_KEYRING_H
#include <libraries/keyring.h>
#define __KEYRING_NOLIBBASE__
#include <proto/keyring.h>
#include <proto/exec.h>
#include "wifi_secret.h"
/* module is a registered loaded service module, NULL selects the executable.
 * keyring.library validates its identity against the installed allowlist. */
static inline LONG wifi_keyring(APTR module, unsigned operation, CONST_STRPTR ssid,
                         struct wifi_secret *secret)
{
    struct Library *KeyringBase = OpenLibrary(KEYRINGNAME, 1);
    APTR context;
    LONG result = KR_ERR_CONTEXT;
    ULONG size = sizeof(*secret);
    if (!KeyringBase) return result;
    if (operation == 1 && !wifi_secret_valid(secret)) {
        CloseLibrary(KeyringBase); return KR_ERR_FORMAT;
    }
    context = KR_ServiceOpen(module, KR_SERVICE_WIFI);
    if (context) {
        if (operation == 0) {
            result = KR_ServiceFetch(context, ssid, secret, &size);
            if (result == KR_SUCCESS && (size != sizeof(*secret) ||
                !wifi_secret_valid(secret)))
                result = KR_ERR_FORMAT;
        } else if (operation == 1)
            result = KR_ServiceStore(context, ssid, secret, sizeof(*secret));
        else
            result = KR_ServiceDelete(context, ssid);
        KR_ServiceClose(context);
    }
    CloseLibrary(KeyringBase);
    return result;
}
/* The same operations on a context opened by another task of the program:
 * a worker process has no CLI, and the keyring tells callers apart by the
 * program they run, so the program's main process opens the context. */
static inline LONG wifi_keyring_context(APTR context, unsigned operation, CONST_STRPTR ssid,
                         struct wifi_secret *secret)
{
    struct Library *KeyringBase = OpenLibrary(KEYRINGNAME, 1);
    LONG result = KR_ERR_CONTEXT;
    ULONG size = sizeof(*secret);
    if (!KeyringBase) return result;
    if (operation == 1 && !wifi_secret_valid(secret)) {
        CloseLibrary(KeyringBase); return KR_ERR_FORMAT;
    }
    if (operation == 0) {
        result = KR_ServiceFetch(context, ssid, secret, &size);
        if (result == KR_SUCCESS && (size != sizeof(*secret) || !wifi_secret_valid(secret)))
            result = KR_ERR_FORMAT;
    } else if (operation == 1)
        result = KR_ServiceStore(context, ssid, secret, sizeof(*secret));
    else
        result = KR_ServiceDelete(context, ssid);
    CloseLibrary(KeyringBase);
    return result;
}
#endif
