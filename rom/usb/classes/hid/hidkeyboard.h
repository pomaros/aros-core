/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1. */
#ifndef HIDKEYBOARD_H
#define HIDKEYBOARD_H
#include "hid.h"
#if defined(AROS_VM_GUEST)
BOOL nKeyboardItem(struct NepHidItem *item);
BOOL nKeyboardReport(struct NepClassHid *,struct NepHidReport *,const UBYTE *,ULONG,BOOL *);
void nKeyboardDetach(struct NepClassHid *);
void nKeyboardExit(struct NepHidBase *);
#else
#define nKeyboardItem(item) FALSE
#define nKeyboardReport(nch,nhr,buf,len,valid) FALSE
#define nKeyboardDetach(nch) ((void)0)
#define nKeyboardExit(nh) ((void)0)
#endif
#endif
