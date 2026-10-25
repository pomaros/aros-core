/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef KEYRING_SECRETPROBE_H
#define KEYRING_SECRETPROBE_H
#include <exec/io.h>
#include <exec/interrupts.h>
struct secret_probe {
    struct MsgPort *port;
    struct IOStdReq *request;
    struct Interrupt handler;
    volatile ULONG printable;
    int opened, added, positive_control, injection_refused;
};
int secret_probe_begin(struct secret_probe *probe);
ULONG secret_probe_end(struct secret_probe *probe);
#endif
