/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1. */
#ifndef RELOADJOB_H
#define RELOADJOB_H
#include <exec/ports.h>
#define RELOAD_JOB_MAGIC 0x524a4f42UL
#define RELOAD_TRY_STOP 1
#define RELOAD_DRAIN_STOP 2
#define RELOAD_BUSY 1
struct ReloadJob {
    ULONG magic;
    CONST_STRPTR path;
    struct MsgPort *parent;
    struct MsgPort *control;
    struct Message ready;
    struct Message command;
    struct Message done;
    ULONG operation;
    ULONG result;
    ULONG generation;
    ULONG entered;
    ULONG left;
    ULONG closed;
    ULONG child_id;
};
#endif
