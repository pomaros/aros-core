#ifndef RESOURCES_BWFM_H
#define RESOURCES_BWFM_H
/* Copyright (C) 2026 John KNIPPER.
 * Licensed under the AROS Public License (APL) Version 1.1. */

/* BWFMGetCapabilities(): the resource AND running firmware implement this
 * contract. Join/JoinPSK return success only after key negotiation completes;
 * callers must not infer this capability from a chip name or association. */
#define BWFM_CAP_4WAY_PSK 1u
#define BWFM_PSK_BYTES 32u

#endif
