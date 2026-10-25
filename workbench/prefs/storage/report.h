/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
   Distributed under the AROS Public License, version 1.1. */
#ifndef STORAGE_REPORT_H
#define STORAGE_REPORT_H
#include <stddef.h>
#include <stdint.h>
struct AfsplusExtRequest;
typedef int32_t (*StorageSend)(void *, struct AfsplusExtRequest *);
/* Returns 1 for a complete report, 0 for unavailable diagnostics. */
int storage_report(void *context, StorageSend send, char *text, size_t capacity);
typedef void (*StorageSnapshotEntry)(void *, uint64_t, uint64_t, uint64_t);
/* A -1 result identifies a malformed or non-progressing handler reply. */
int32_t storage_snapshots(void *context, StorageSend send, StorageSnapshotEntry entry,
    void *entry_context, uint32_t *total, int *incomplete);
int32_t storage_snapshot_delete(void *context, StorageSend send, uint64_t id);
#endif
