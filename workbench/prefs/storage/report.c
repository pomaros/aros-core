/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
   Distributed under the AROS Public License, version 1.1. */
#include <filesystems/afsplus_aros.h>
#include <filesystems/afsplus_ext_packet.h>
#include <stdio.h>
#include <string.h>
#include "report.h"

static int32_t query(void *context, StorageSend send, uint32_t operation,
    void *buffer, uint32_t size, uint64_t *required)
{
    struct AfsplusExtRequest request;
    int32_t error;
    memset(&request, 0, sizeof(request));
    request.magic = AFSPLUS_EXT_MAGIC;
    request.version = AFSPLUS_EXT_VERSION;
    request.header_size = sizeof(request);
    request.operation = operation;
    request.buffer = buffer;
    request.buffer_size = size;
    error = send(context, &request);
    if (required) *required = request.output_value;
    return error;
}

static uint64_t mib(uint64_t blocks, uint32_t size)
{
    /* Accepted block sizes are powers of two, up to 64KiB. */
    return (blocks / 1048576) * size + (blocks % 1048576) * size / 1048576;
}

int storage_report(void *context, StorageSend send, char *text, size_t capacity)
{
    struct AfsplusArosCapabilities caps;
    struct AfsplusArosHealth health;
    char json[4096], uuid[33];
    const char *identity;
    uint64_t required = 0;
    int32_t error;
    size_t i;
    memset(&caps, 0, sizeof(caps));
    caps.struct_size = sizeof(caps);
    error = query(context, send, AFSPLUS_EXT_CAPABILITIES, &caps, sizeof(caps), NULL);
    if (error)
    {
        snprintf(text, capacity, "Filesystem diagnostics unavailable (DOS error %ld)", (long)error);
        return 0;
    }
    if (caps.struct_size != sizeof(caps) || !caps.block_size || caps.block_size > 65536 ||
        (caps.block_size & (caps.block_size - 1)) || caps.free_blocks > caps.total_blocks ||
        caps.available_blocks > caps.free_blocks)
    {
        snprintf(text, capacity, "Filesystem returned invalid capacity information");
        return 0;
    }
    memset(&health, 0, sizeof(health));
    health.struct_size = sizeof(health);
    error = query(context, send, AFSPLUS_EXT_HEALTH, &health, sizeof(health), NULL);
    if (error || health.struct_size != sizeof(health))
    {
        snprintf(text, capacity, "Capacity: %llu MiB\nAvailable: %llu MiB\nHealth unavailable (DOS error %ld)",
            (unsigned long long)mib(caps.total_blocks, caps.block_size),
            (unsigned long long)mib(caps.available_blocks, caps.block_size), (long)error);
        return 0;
    }
    memset(json, 0, sizeof(json));
    error = query(context, send, AFSPLUS_EXT_INFO_JSON, json, sizeof(json), &required);
    strcpy(uuid, "unavailable");
    /* The identity in INFO_JSON is exactly 32 hexadecimal bytes. A partial
       report is never parsed, and its contents cannot change capacity/health. */
    if (!error && required > 0 && required < sizeof(json) &&
        (identity = strstr(json, "\"uuid\":\"")) != NULL)
    {
        identity += strlen("\"uuid\":\"");
        for (i = 0; i < 32 && identity[i] && strchr("0123456789abcdef", identity[i]); i++)
            uuid[i] = identity[i];
        if (i == 32 && identity[i] == '"') uuid[32] = 0;
        else strcpy(uuid, "unavailable");
    }
    snprintf(text, capacity,
        "Capacity: %llu MiB\nAvailable: %llu MiB\nHealth: %s\n"
        "Device errors: %llu; corruption errors: %llu\n"
        "Checkpoint: %llu; pending cleanup: %llu\nVolume ID: %s",
        (unsigned long long)mib(caps.total_blocks, caps.block_size),
        (unsigned long long)mib(caps.available_blocks, caps.block_size),
        health.flags ? "Attention required" : "No recorded faults",
        (unsigned long long)health.device_errors, (unsigned long long)health.corruption_errors,
        (unsigned long long)health.generation, (unsigned long long)health.pending_orphans, uuid);
    return 1;
}

int32_t storage_snapshots(void *context, StorageSend send, StorageSnapshotEntry entry,
    void *entry_context, uint32_t *total, int *incomplete)
{
    struct AfsplusArosSnapshotInfo records[64];
    struct AfsplusExtRequest request;
    uint64_t next = 0, previous_id = 0;
    uint32_t i;
    int32_t error;
    *total = 0;
    *incomplete = 0;
    do
    {
        memset(&request, 0, sizeof(request));
        request.magic = AFSPLUS_EXT_MAGIC;
        request.version = AFSPLUS_EXT_VERSION;
        request.header_size = sizeof(request);
        request.operation = AFSPLUS_EXT_SNAPSHOT_LIST;
        request.offset[0] = next;
        request.length = 64;
        request.buffer = records;
        request.buffer_size = sizeof(records);
        error = send(context, &request);
        if (error) return error;
        if (request.output_count > 64 || (!request.output_count && request.output_value) ||
            (request.output_value && request.output_value <= next)) return -1;
        for (i = 0; i < request.output_count; i++)
        {
            if (!records[i].id || records[i].id <= previous_id || records[i].id < next)
                return -1;
            previous_id = records[i].id;
            entry(entry_context, records[i].id, records[i].generation, records[i].committed_tx_id);
            (*total)++;
        }
        if (request.output_value && request.output_value <= previous_id) return -1;
        next = request.output_value;
    } while (next && *total < 8192);
    *incomplete = next != 0;
    return 0;
}

int32_t storage_snapshot_delete(void *context, StorageSend send, uint64_t id)
{
    struct AfsplusExtRequest request;
    if (!id) return -1;
    memset(&request, 0, sizeof(request));
    request.magic = AFSPLUS_EXT_MAGIC;
    request.version = AFSPLUS_EXT_VERSION;
    request.header_size = sizeof(request);
    request.operation = AFSPLUS_EXT_SNAPSHOT_DELETE;
    request.object[0] = id;
    return send(context, &request);
}
