/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef VM_EXCHANGE_CLIENT_H
#define VM_EXCHANGE_CLIENT_H
#include <exec/types.h>
#include <exec/io.h>
struct VMExchangeGate;
struct VMExchangeClient { struct VMExchangeGate *gate; BOOL gateHeld; struct MsgPort *port;struct IOStdReq *io;ULONG id,status;BOOL opened,failed; };
BOOL VMExchangeOpen(struct VMExchangeClient *);
/* Skip a busy session, for background synchronization. */
BOOL VMExchangeTryOpen(struct VMExchangeClient *);
void VMExchangeClose(struct VMExchangeClient *);
BOOL VMExchangeRequest(struct VMExchangeClient *,UWORD,const void *,ULONG,void *,ULONG,ULONG *);
#endif
