/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef LIBRARIES_KEYRING_H
#define LIBRARIES_KEYRING_H
#include <exec/types.h>
#define KEYRINGNAME "keyring.library"
#define KR_SERVICE_WIFI 1
#define KR_SERVICE_BLUETOOTH 2
#define KR_SUCCESS 0
#define KR_ERR_NOT_FOUND -5
#define KR_ERR_LOCKED -1
#define KR_ERR_AUTH -2
#define KR_ERR_BOUNDS -3
#define KR_ERR_OWNER -4
#define KR_ERR_NOTFOUND -5
#define KR_ERR_NOMEM -6
#define KR_ERR_ENTROPY -7
#define KR_ERR_FORMAT -8
#define KR_ERR_IO -19
#define KR_ERR_INPUT -20
#define KR_ERR_CONTEXT -21
/* All ownership checks provide containment in AROS's shared address space. */
#endif
