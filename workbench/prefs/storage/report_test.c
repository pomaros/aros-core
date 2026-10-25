/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
   Distributed under the AROS Public License, version 1.1. */
#include <filesystems/afsplus_aros.h>
#include <filesystems/afsplus_ext_packet.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "report.h"

struct fixture { int error; int invalid; int large; int fault; unsigned calls; int bad_page; uint64_t deleted; };
static int32_t send(void *context, struct AfsplusExtRequest *request)
{
    struct fixture *f = context;
    f->calls++;
    assert(request->magic == AFSPLUS_EXT_MAGIC);
    assert(request->header_size == sizeof(*request));
    if (f->error) return f->error;
    if (request->operation == AFSPLUS_EXT_CAPABILITIES)
    {
        struct AfsplusArosCapabilities *c = request->buffer;
        assert(c->struct_size == sizeof(*c));
        c->block_size = 4096;
        c->total_blocks = UINT64_C(1) << 40;
        c->free_blocks = UINT64_C(1) << 39;
        c->available_blocks = UINT64_C(1) << 38;
        if (f->invalid) c->available_blocks = c->total_blocks;
    }
    else if (request->operation == AFSPLUS_EXT_HEALTH)
    {
        struct AfsplusArosHealth *h = request->buffer;
        assert(h->struct_size == sizeof(*h));
        h->flags = f->fault ? AFSPLUS_AROS_HEALTH_CORRUPTION : 0;
        h->corruption_errors = f->fault;
        h->generation = 29;
    }
    else if (request->operation == AFSPLUS_EXT_INFO_JSON)
    {
        const char *json = "{\"volume\":{\"uuid\":\"0123456789abcdef0123456789abcdef\"}}";
        if (f->large) { request->output_value = request->buffer_size + 100; return 0; }
        memcpy(request->buffer, json, strlen(json) + 1);
        request->output_value = strlen(json);
    }
    else if (request->operation == AFSPLUS_EXT_SNAPSHOT_LIST)
    {
        struct AfsplusArosSnapshotInfo *records = request->buffer;
        assert(request->length == 64 && request->buffer_size == sizeof(*records) * 64);
        uint64_t id = request->offset[0] ? request->offset[0] : 1;
        records[0].id = id; records[0].generation = 29; records[0].committed_tx_id = 28;
        records[1] = records[0]; records[1].id++;
        request->output_count = 2;
        request->output_value = id < 3 ? 3 : 0;
        if (f->bad_page == 1) request->output_count = 65;
        if (f->bad_page == 2) request->output_value = id;
        if (f->bad_page == 3) { request->output_count = 0; request->output_value = 3; }
    }
    else if (request->operation == AFSPLUS_EXT_SNAPSHOT_DELETE)
        f->deleted = request->object[0];
    else assert(0);
    return 0;
}
static void snapshot(void *context, uint64_t id, uint64_t generation, uint64_t tx)
{
    uint64_t *sum = context;
    assert(generation == 29 && tx == 28);
    *sum += id;
}
int main(void)
{
    struct fixture f = {0};
    char text[768], short_text[8];
    assert(storage_report(&f, send, text, sizeof(text)) == 1);
    assert(f.calls == 3);
    assert(strstr(text, "Capacity: 4294967296 MiB"));
    assert(strstr(text, "Available: 1073741824 MiB"));
    assert(strstr(text, "No recorded faults"));
    assert(strstr(text, "0123456789abcdef0123456789abcdef"));
    f.fault = 1;
    assert(storage_report(&f, send, text, sizeof(text)) == 1);
    assert(strstr(text, "Attention required"));
    assert(strstr(text, "corruption errors: 1"));
    f.large = 1;
    assert(storage_report(&f, send, text, sizeof(text)) == 1);
    assert(strstr(text, "Volume ID: unavailable"));
    f.invalid = 1;
    assert(storage_report(&f, send, text, sizeof(text)) == 0);
    assert(strstr(text, "invalid capacity"));
    f.invalid = 0; f.error = 209; f.calls = 0;
    assert(storage_report(&f, send, text, sizeof(text)) == 0);
    assert(f.calls == 1 && strstr(text, "DOS error 209"));
    assert(storage_report(&f, send, short_text, sizeof(short_text)) == 0);
    assert(short_text[7] == 0);
    f.error = 0; f.calls = 0;
    uint32_t total = 0; int incomplete = 0; uint64_t sum = 0;
    assert(storage_snapshots(&f, send, snapshot, &sum, &total, &incomplete) == 0);
    assert(f.calls == 2 && total == 4 && sum == 10 && !incomplete);
    for (f.bad_page = 1; f.bad_page <= 3; f.bad_page++)
        assert(storage_snapshots(&f, send, snapshot, &sum, &total, &incomplete) == -1);
    f.bad_page = 0; f.error = 214;
    assert(storage_snapshots(&f, send, snapshot, &sum, &total, &incomplete) == 214);
    assert(storage_snapshot_delete(&f, send, 7) == 214);
    f.error = 0;
    assert(storage_snapshot_delete(&f, send, 7) == 0 && f.deleted == 7);
    assert(storage_snapshot_delete(&f, send, 0) == -1);
    puts("storage-report-host PASS capacity=64bit health=real unavailable=explicit partial-json=rejected snapshot-pagination=validated snapshot-delete=selected denial=explicit");
    return 0;
}
