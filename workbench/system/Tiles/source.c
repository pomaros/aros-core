/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>. All rights reserved.
    SPDX-License-Identifier: APL-1.1

    Bounded asynchronous ARexx requests. A timeout never revokes ownership of
    a message held by another task: expired slots remain until their reply.
*/
#include "source.h"
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/rxslib.h>
#include <string.h>

BOOL TileSourcesInit(struct TileSources *sources)
{
    memset(sources, 0, sizeof(*sources));
    sources->replies = CreateMsgPort();
    return sources->replies != NULL;
}
BOOL TileSourcePending(struct TileSources *sources, APTR owner)
{
    unsigned i;
    for (i = 0; i < TILE_SOURCE_LIMIT; ++i)
        if (sources->requests[i].message && sources->requests[i].owner == owner)
            return TRUE;
    return FALSE;
}
BOOL TileSourceSend(struct TileSources *sources, APTR owner, BOOL setting,
                    CONST_STRPTR portName, CONST_STRPTR command, ULONG now)
{
    struct TileRequest *request = NULL;
    struct RexxMsg *message;
    struct MsgPort *destination;
    unsigned i;
    if (TileSourcePending(sources, owner)) return FALSE;
    for (i = 0; i < TILE_SOURCE_LIMIT; ++i)
        if (!sources->requests[i].message) { request = &sources->requests[i]; break; }
    if (!request) return FALSE;
    message = CreateRexxMsg(sources->replies, NULL, "TILES");
    if (!message) return FALSE;
    message->rm_Args[0] = (IPTR)CreateArgstring(command, strlen(command));
    if (!message->rm_Args[0]) { DeleteRexxMsg(message); return FALSE; }
    message->rm_Action = RXCOMM | RXFF_RESULT;
    /* Standard Exec public-port contract. Do not retain the discovered port.
     * This service's current runtime acceptance is UP; Forbid is not a
     * cross-CPU lifetime pin on SMP. */
    Forbid();
    destination = FindPort(portName);
    if (destination) {
        request->message = message;
        request->owner = owner;
        request->setting = setting;
        request->expired = FALSE;
        request->started = now;
        PutMsg(destination, (struct Message *)message);
    }
    Permit();
    if (!destination) {
        DeleteArgstring((STRPTR)message->rm_Args[0]);
        DeleteRexxMsg(message);
        return FALSE;
    }
    return TRUE;
}
void TileSourcesCollect(struct TileSources *sources, TileSourceResult callback, APTR context)
{
    struct RexxMsg *message;
    while ((message = (struct RexxMsg *)GetMsg(sources->replies))) {
        unsigned i;
        for (i = 0; i < TILE_SOURCE_LIMIT; ++i) {
            struct TileRequest *request = &sources->requests[i];
            if (request->message == message) {
                APTR owner = request->owner;
                BOOL setting = request->setting, expired = request->expired;
                BOOL success = message->rm_Result1 == 0;
                CONST_STRPTR value = success && message->rm_Result2 ?
                    (CONST_STRPTR)message->rm_Result2 : (CONST_STRPTR)"";
                /* Free the slot before the callback may issue a follow-up GET.
                 * The reply's argument/result remain valid during callback. */
                request->message = NULL;
                if (!expired) callback(owner, setting, success, value, context);
                if (success && message->rm_Result2)
                    DeleteArgstring((STRPTR)message->rm_Result2);
                DeleteArgstring((STRPTR)message->rm_Args[0]);
                DeleteRexxMsg(message);
                break;
            }
        }
        /* Only this module owns the private reply port; unknown messages
         * cannot be freed because their allocation contract is unknown. */
    }
}
void TileSourcesExpire(struct TileSources *sources, ULONG now,
                       TileSourceResult callback, APTR context)
{
    unsigned i;
    for (i = 0; i < TILE_SOURCE_LIMIT; ++i) {
        struct TileRequest *request = &sources->requests[i];
        if (request->message && !request->expired &&
            (ULONG)(now - request->started) >= TILE_SOURCE_TIMEOUT) {
            request->expired = TRUE;
            callback(request->owner, request->setting, FALSE, "", context);
        }
    }
}
ULONG TileSourcesNext(struct TileSources *sources, ULONG now, ULONG fallback)
{
    unsigned i;
    for (i = 0; i < TILE_SOURCE_LIMIT; ++i) {
        struct TileRequest *request = &sources->requests[i];
        if (request->message && !request->expired) {
            ULONG elapsed = now - request->started;
            ULONG delay = elapsed >= TILE_SOURCE_TIMEOUT ? 1 : TILE_SOURCE_TIMEOUT - elapsed;
            if (delay < fallback) fallback = delay;
        }
    }
    return fallback;
}
BOOL TileSourcesIdle(struct TileSources *sources)
{
    unsigned i;
    for (i = 0; i < TILE_SOURCE_LIMIT; ++i)
        if (sources->requests[i].message) return FALSE;
    return TRUE;
}
BOOL TileSourcesClose(struct TileSources *sources)
{
    if (!TileSourcesIdle(sources)) return FALSE;
    DeleteMsgPort(sources->replies);
    sources->replies = NULL;
    return TRUE;
}
BOOL TileSetCommand(CONST_STRPTR pattern, CONST_STRPTR value, STRPTR output, ULONG capacity)
{
    const char *format = (const char *)pattern;
    const char *slot = strstr(format, "%v");
    ULONG prefix, suffix, length;
    if (!slot || strchr(slot + 2, '%') || memchr(format, '%', slot - format)) return FALSE;
    prefix = slot - format; suffix = strlen(slot + 2); length = strlen(value);
    if (prefix + length + suffix + 1 > capacity) return FALSE;
    memcpy(output, pattern, prefix);
    memcpy(output + prefix, value, length);
    memcpy(output + prefix + length, slot + 2, suffix + 1);
    return TRUE;
}
