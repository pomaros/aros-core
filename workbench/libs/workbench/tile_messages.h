/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#ifndef WB_TILE_MESSAGES_H
#define WB_TILE_MESSAGES_H

/* Shared with the presenter. Caller holds the Workbench registry mutex.
 * Replies carry no dereferenceable icon pointer: match only the allocation
 * identity against live registrations, so removal and address reuse are safe. */
static void WBTileCollectReplies(struct WorkbenchBase *wb)
{
    struct Message *message;
    while ((message = GetMsg(&wb->wb_TileReplyPort))) {
        struct AppIcon *icon;
        ForeachNode(&wb->wb_AppIcons, icon)
            if (icon->ai_Tile && (APTR)icon->ai_Tile->pointerMessage == (APTR)message)
                icon->ai_Tile->pointerMessage = NULL;
        if (wb->wb_TileInflight) --wb->wb_TileInflight;
        FreeVec(message);
    }
}

static struct AppIconTileMessage *WBTileSendMessage(struct AppIcon *icon,
    UWORD kind, LONG x, LONG y, const char *region, struct WorkbenchBase *wb)
{
    struct AppIconTileMessage *message;
    if (!icon->ai_Tile->owner || !icon->ai_MsgPort || wb->wb_TileInflight >= 64)
        return NULL;
    message = AllocVec(sizeof(*message), MEMF_CLEAR);
    if (!message) return NULL;
    message->atm_Message.am_Message.mn_Length = sizeof(*message);
    message->atm_Message.am_Message.mn_ReplyPort = &wb->wb_TileReplyPort;
    message->atm_Message.am_Type = AMTYPE_APPICON;
    message->atm_Message.am_Version = AM_VERSION;
    message->atm_Message.am_ID = icon->ai_ID;
    message->atm_Message.am_UserData = icon->ai_UserData;
    message->atm_Message.am_Class = kind;
    message->atm_Message.am_MouseX = x;
    message->atm_Message.am_MouseY = y;
    if (region) memcpy(message->atm_Region, region, sizeof(message->atm_Region));
    ++wb->wb_TileInflight;
    PutMsg(icon->ai_MsgPort, &message->atm_Message.am_Message);
    return message;
}

static BOOL WBTileHovered(struct WBTile *tile, ULONG now)
{
    return !tile->suspended && tile->pointerEnabled && tile->hovered && tile->visible &&
        (ULONG)(now - tile->visibilityTick) < 1000 &&
        (ULONG)(now - tile->pointerTick) < 1000 &&
        WBTileStateFresh(&tile->state, now);
}

static void WBTileFlushPointers(struct WorkbenchBase *wb)
{
    struct AppIcon *icon;
    ForeachNode(&wb->wb_AppIcons, icon) {
        struct WBTile *tile = icon->ai_Tile;
        UWORD kind;
        if (!tile || !tile->pointerEnabled || !tile->owner) continue;
        if (!WBTileHovered(tile, WBTileClock(&tile->timer))) tile->hovered = FALSE;
        if (tile->pointerMessage) continue; /* never mutate a borrowed message */
        if (tile->hovered != tile->pointerSentInside)
            kind = tile->hovered ? AMCLASSICON_PointerEnter : AMCLASSICON_PointerLeave;
        else if (tile->hovered && (tile->pointerX != tile->pointerSentX ||
                                  tile->pointerY != tile->pointerSentY))
            kind = AMCLASSICON_PointerMove;
        else continue;
        tile->pointerMessage = WBTileSendMessage(icon, kind, tile->pointerX,
                                                 tile->pointerY, NULL, wb);
        if (tile->pointerMessage) {
            tile->pointerSentInside = tile->hovered;
            tile->pointerSentX = tile->pointerX;
            tile->pointerSentY = tile->pointerY;
        }
    }
}
#endif
