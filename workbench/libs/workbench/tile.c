/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1.
    SPDX-License-Identifier: APL-1.1
*/
#define __GRAPHICS_NOLIBBASE__
#define __LAYERS_NOLIBBASE__
#include <graphics/gfx.h>
#include <graphics/layers.h>
#include <proto/layers.h>
#include <proto/icon.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include "workbench_intern.h"
#include "tile.h"
#include "notifyworkbench.h"

/* Snapshot nodes own every copied bitmap and never borrow owner memory. */
struct WBTileSnapshotNode {
    struct WBTileSnapshotNode *next;
    APTR handle;
    struct Task *owner;
    struct Screen *screen;
    struct Library *graphics;
    ULONG count;
    struct AppIconSnapshotEntry entries[APPICON_SNAPSHOT_MAX];
    UQUAD frames[APPICON_SNAPSHOT_MAX];
    struct BitMap *fallbacks[APPICON_SNAPSHOT_MAX];
};
static struct WBTileSnapshotNode *snapshots;
static UQUAD registrationSerial;
static IPTR snapshotSerial;
static void freeSnapshot(struct WBTileSnapshotNode *snapshot)
{
    struct GfxBase *GfxBase = (struct GfxBase *)snapshot->graphics;
    ULONG i;
    for (i=0;i<snapshot->count;++i) {
        if (snapshot->entries[i].ais_Bitmap) FreeBitMap(snapshot->entries[i].ais_Bitmap);
        if (snapshot->fallbacks[i]) FreeBitMap(snapshot->fallbacks[i]);
    }
    if (snapshot->graphics) CloseLibrary(snapshot->graphics);
    FreeVec(snapshot);
}
/* Owner-exit hook only marks retired nodes; graphics cleanup occurs here in
 * ordinary task context, never while running inside RemTask's callback. */
static void sweepSnapshots(void)
{
    struct WBTileSnapshotNode **link=&snapshots;
    while (*link) {
        struct WBTileSnapshotNode *node=*link;
        if (!node->owner) { *link=node->next; freeSnapshot(node); }
        else link=&node->next;
    }
}

static void cleanReplies(struct WorkbenchBase *WorkbenchBase)
{
    LockWorkbench();
    WBTileCollectReplies(WorkbenchBase);
    WBTileFlushPointers(WorkbenchBase);
    sweepSnapshots();
    UnlockWorkbench();
}

static BOOL copyFrame(struct WBTile *tile, struct BitMap *source, ULONG now)
{
    struct GfxBase *GfxBase = (struct GfxBase *)tile->graphics;
    struct BitMap *copy;
    if (!source || GetBitMapAttr(source, BMA_WIDTH) != tile->state.width ||
        GetBitMapAttr(source, BMA_HEIGHT) != tile->state.height)
        return FALSE;
    copy = AllocBitMap(tile->state.width, tile->state.height,
                      GetBitMapAttr(source, BMA_DEPTH), BMF_CLEAR, source);
    if (!copy)
        return FALSE;
    BltBitMap(source, 0, 0, copy, 0, 0, tile->state.width,
              tile->state.height, 0xc0, 0xff, NULL);
    WaitBlit();
    if (tile->bitmap) FreeBitMap(tile->bitmap);
    tile->bitmap = copy;
    ++tile->frameGeneration;
    tile->suspended = FALSE;
    tile->state.lastFrame = now;
    tile->state.lastAlive = now;
    tile->state.fresh = TRUE;
    tile->state.haveFrame = TRUE;
    tile->dirty = TRUE;
    return TRUE;
}

/* AddAppIconA calls this before inserting the registration or locking the
 * registry. Render only the private duplicated DiskObject, on a clipped layer.
 * No application render hook participates in the static cache. */
static BOOL cacheFallback(struct AppIcon *icon, struct WorkbenchBase *WorkbenchBase)
{
    struct WBTile *tile=icon->ai_Tile;
    struct GfxBase *GfxBase=(struct GfxBase *)tile->graphics;
    struct Library *LayersBase=OpenLibrary((CONST_STRPTR)"layers.library",0);
    struct Layer_Info *info=NULL;
    struct Layer *layer=NULL;
    struct Rectangle bounds;
    struct TagItem tags[]={{ICONDRAWA_Frameless,TRUE},{ICONDRAWA_Borderless,TRUE},
        {ICONDRAWA_EraseBackground,FALSE},{TAG_DONE,0}};
    BOOL result=FALSE;
    if (!LayersBase) return FALSE;
    tile->fallbackBitmap=AllocBitMap(tile->state.width,tile->state.height,
        GetBitMapAttr(tile->bitmap,BMA_DEPTH),BMF_CLEAR,tile->bitmap);
    if (!tile->fallbackBitmap || !(info=NewLayerInfo())) goto done;
    layer=CreateUpfrontLayer(info,tile->fallbackBitmap,0,0,
        tile->state.width-1,tile->state.height-1,LAYERSIMPLE,NULL);
    if (!layer || !GetIconRectangleA(NULL,icon->ai_DiskObject,NULL,&bounds,tags)) goto done;
    DrawIconStateA(layer->rp,icon->ai_DiskObject,NULL,
        ((LONG)tile->state.width-(bounds.MaxX-bounds.MinX+1))/2,
        ((LONG)tile->state.height-(bounds.MaxY-bounds.MinY+1))/2,IDS_NORMAL,tags);
    WaitBlit();result=TRUE;
done:
    if (layer) DeleteLayer(0,layer);
    if (info) DisposeLayerInfo(info);
    CloseLibrary(LayersBase);
    return result;
}
static BOOL dynamicSnapshot(struct WBTile *tile)
{
    return tile->owner && !tile->suspended && tile->bitmap &&
        WBTileStateFresh(&tile->state,WBTileClock(&tile->timer));
}

BOOL WBTileCreate(struct AppIcon *icon, struct DiskObject *fallback,
                  struct TagItem *tags, struct WorkbenchBase *WorkbenchBase)
{
    struct WBTile *tile = AllocVec(sizeof(*tile), MEMF_CLEAR);
    struct BitMap *surface = (struct BitMap *)GetTagData(WBAPPICONA_Surface, 0, tags);
    struct TagItem copyTags[] = {
        { ICONDUPA_DuplicateImages, TRUE },
        { ICONDUPA_DuplicateImageData, TRUE },
        { TAG_DONE, 0 }
    };
    if (!tile) return FALSE;
    if (!WorkbenchBase->wb_TileTaskResource || !icon->ai_MsgPort ||
        icon->ai_MsgPort->mp_Flags != PA_SIGNAL ||
        icon->ai_MsgPort->mp_SigTask != FindTask(NULL)) {
        FreeVec(tile);
        return FALSE;
    }
    {
        const char *definition=(const char *)GetTagData(WBAPPICONA_DefinitionPath,0,tags);
        ULONG length=0;
        tile->group=!!GetTagData(WBAPPICONA_Group,FALSE,tags);
        tile->groupAnchor=GetTagData(WBAPPICONA_GroupAnchor,0,tags);
        if(definition) {
            while(length<sizeof(tile->definitionPath) && definition[length])++length;
            if(length==sizeof(tile->definitionPath) || (length && !strchr(definition,':'))) {
                FreeVec(tile);return FALSE;
            }
            memcpy(tile->definitionPath,definition,length+1);
        }
        if(tile->groupAnchor>4 || (tile->groupAnchor && !tile->group)) {FreeVec(tile);return FALSE;}
    }
    icon->ai_Tile = tile;
    /* Tile labels/fallbacks must survive an owner exiting without cleanup. */
    icon->ai_DiskObject = NULL;
    {
        const char *text = icon->ai_Text ? (const char *)icon->ai_Text : "";
        char *copy = AllocVec(strlen(text) + 1, MEMF_ANY);
        if (copy) strcpy(copy, text);
        icon->ai_Text = (CONST_STRPTR)copy;
    }
    tile->owner = FindTask(NULL);
    tile->graphics = OpenLibrary((CONST_STRPTR)"graphics.library", 0);
    tile->timer.tr_node.io_Message.mn_Length = sizeof(tile->timer);
    tile->timerOpen = OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK,
                                (struct IORequest *)&tile->timer, 0) == 0;
    if (!icon->ai_Text || !tile->graphics || !tile->timerOpen ||
        !(tile->group ? WBTileStateInitBounded(&tile->state,
            GetTagData(WBAPPICONA_Cells, WBAPPICON_CELLS(1,1), tags),
            (const struct AppIconRegions *)GetTagData(WBAPPICONA_Regions, 0, tags),
            GetTagData(WBAPPICONA_Cadence, 500, tags),32) : WBTileStateInit(&tile->state,
            GetTagData(WBAPPICONA_Cells, WBAPPICON_CELLS(1,1), tags),
            (const struct AppIconRegions *)GetTagData(WBAPPICONA_Regions, 0, tags),
            GetTagData(WBAPPICONA_Cadence, 500, tags))) ||
        !(icon->ai_DiskObject = DupDiskObjectA(fallback, copyTags)) ||
        !copyFrame(tile, surface, WBTileClock(&tile->timer)) ||
        !cacheFallback(icon,WorkbenchBase)) {
        WBTileFree(icon, WorkbenchBase);
        return FALSE;
    }
    LockWorkbench();
    if (registrationSerial == ~(UQUAD)0) {
        UnlockWorkbench();
        WBTileFree(icon, WorkbenchBase);
        return FALSE;
    }
    tile->registrationGeneration = ++registrationSerial;
    UnlockWorkbench();
    tile->pointerEnabled = GetTagData(WBAPPICONA_Pointer, FALSE, tags) != 0;
    tile->hoverCadence = tile->pointerEnabled ? GetTagData(WBAPPICONA_HoverCadence, 0, tags) : 0;
    if (tile->hoverCadence && tile->hoverCadence < WBAPPICON_HOVER_MIN_MS)
        tile->hoverCadence = WBAPPICON_HOVER_MIN_MS;
    /* No callback or propagation may touch the original owner's objects. */
    icon->ai_RenderHook = NULL;
    icon->ai_Flags &= ~WBAPPICONF_PropagatePosition;
    return TRUE;
}

void WBTileFree(struct AppIcon *icon, struct WorkbenchBase *WorkbenchBase)
{
    struct WBTile *tile = icon->ai_Tile;
    struct GfxBase *GfxBase;
    if (!tile) return;
    cleanReplies(WorkbenchBase);
    GfxBase = (struct GfxBase *)tile->graphics;
    if (tile->bitmap) FreeBitMap(tile->bitmap);
    if (tile->fallbackBitmap) FreeBitMap(tile->fallbackBitmap);
    if (tile->graphics) CloseLibrary(tile->graphics);
    if (tile->timerOpen) CloseDevice((struct IORequest *)&tile->timer);
    if (icon->ai_DiskObject) FreeDiskObject(icon->ai_DiskObject);
    FreeVec((APTR)icon->ai_Text);
    FreeVec(tile);
    icon->ai_Tile = NULL;
}

BOOL WBTilePublish(struct AppIconPublish *publish, struct WorkbenchBase *WorkbenchBase)
{
    struct AppIcon *icon;
    BOOL ok = FALSE;
    if (!publish) return FALSE;
    cleanReplies(WorkbenchBase);
    LockWorkbench();
    ForeachNode(&WorkbenchBase->wb_AppIcons, icon) {
        struct WBTile *tile;
        ULONG now;
        BOOL changed = FALSE;
        if (icon != publish->aip_Icon) continue;
        tile = icon->ai_Tile;
        if (!tile || tile->owner != FindTask(NULL)) break;
        now = WBTileClock(&tile->timer);
        if (!publish->aip_Surface)
        {
            /* A heartbeat changes liveness, never the accepted-frame cadence. */
            changed = !tile->suspended && !tile->state.fresh;
            if (changed)
                tile->dirty = TRUE;
            tile->state.lastAlive = now;
            tile->state.fresh = !tile->suspended && tile->state.haveFrame;
            ok = TRUE;
        }
        else
        {
            BOOL fast = tile->hoverCadence && WBTileHovered(tile, now);
            BOOL due = fast ? (ULONG)(now - tile->state.lastFrame) >= tile->hoverCadence :
                              WBTileStateDue(&tile->state, now);
            if (fast && WorkbenchBase->wb_TileHaveFastFrame &&
                (ULONG)(now - WorkbenchBase->wb_TileFastTick) < WBAPPICON_HOVER_MIN_MS)
                due = FALSE;
            ok = due && copyFrame(tile, publish->aip_Surface, now);
            if (ok && fast) {
                WorkbenchBase->wb_TileFastTick = now;
                WorkbenchBase->wb_TileHaveFastFrame = TRUE;
            }
            changed = ok;
        }
        if (changed)
            ++WorkbenchBase->wb_TileSerial;
        /* One asynchronous refresh relay, even if the desktop is stalled. */
        if (ok && tile->dirty && !WorkbenchBase->wb_TileNotifyPending)
        {
            WorkbenchBase->wb_TileNotifiedSerial = WorkbenchBase->wb_TileSerial;
            WorkbenchBase->wb_TileNotifyPending =
                NotifyWorkbench(WBNOTIFY_RefreshTiles, WBNOTIFY_AppIcon, WorkbenchBase);
        }
        break;
    }
    UnlockWorkbench();
    return ok;
}

/* Reply acknowledgement and dirty inspection are one registry transaction:
 * a frame published after presentation but before reply must get a new wake. */
void WBTileRefreshDone(struct WorkbenchBase *WorkbenchBase)
{
    struct AppIcon *icon;

    LockWorkbench();
    WorkbenchBase->wb_TileNotifyPending = FALSE;
    ForeachNode(&WorkbenchBase->wb_AppIcons, icon)
    {
        /* An older desktop may reply without understanding the new command.
         * Only a newly published generation warrants another relay. */
        if (WorkbenchBase->wb_TileNotifiedSerial != WorkbenchBase->wb_TileSerial &&
            icon->ai_Tile && icon->ai_Tile->dirty)
        {
            WorkbenchBase->wb_TileNotifiedSerial = WorkbenchBase->wb_TileSerial;
            WorkbenchBase->wb_TileNotifyPending =
                NotifyWorkbench(WBNOTIFY_RefreshTiles, WBNOTIFY_AppIcon, WorkbenchBase);
            break;
        }
    }
    UnlockWorkbench();
}

BOOL WBTileQuery(struct AppIconQuery *query, struct WorkbenchBase *WorkbenchBase)
{
    struct AppIcon *icon;
    BOOL result = FALSE;

    if (!query)
        return FALSE;
    query->aiq_Flags = 0;
    query->aiq_MouseX = query->aiq_MouseY = 0;
    LockWorkbench();
    WBTileCollectReplies(WorkbenchBase);
    WBTileFlushPointers(WorkbenchBase);
    ForeachNode(&WorkbenchBase->wb_AppIcons, icon)
    {
        if (icon == query->aiq_Icon && icon->ai_Tile &&
            icon->ai_Tile->owner == FindTask(NULL))
        {
            struct WBTile *tile = icon->ai_Tile;
            ULONG now = WBTileClock(&tile->timer);
            if (tile->visible && (ULONG)(now - tile->visibilityTick) < 1000)
                query->aiq_Flags |= APPICONQF_VISIBLE;
            if (WBTileHovered(tile, now)) query->aiq_Flags |= APPICONQF_HOVERED;
            query->aiq_MouseX = tile->pointerX;
            query->aiq_MouseY = tile->pointerY;
            result = TRUE;
            break;
        }
    }
    UnlockWorkbench();
    return result;
}

BOOL WBTileQueryGeometry(struct AppIconGeometry *query, struct WorkbenchBase *WorkbenchBase)
{
    struct AppIcon *icon;
    BOOL result = FALSE;
    if (!query) return FALSE;
    query->aig_Flags = 0;
    query->aig_Left = query->aig_Top = 0;
    query->aig_Width = query->aig_Height = 0;
    LockWorkbench();
    ForeachNode(&WorkbenchBase->wb_AppIcons, icon) {
        if (icon == query->aig_Icon && icon->ai_Tile &&
            icon->ai_Tile->owner == FindTask(NULL)) {
            struct WBTile *tile = icon->ai_Tile;
            ULONG now = WBTileClock(&tile->timer);
            if (tile->geometryValid && tile->visible &&
                tile->geometryWidth && tile->geometryHeight &&
                (ULONG)(now - tile->geometryTick) < 1000 &&
                (ULONG)(now - tile->visibilityTick) < 1000) {
                query->aig_Flags = APPICONGF_VALID;
                query->aig_Left = tile->geometryLeft; query->aig_Top = tile->geometryTop;
                query->aig_Width = tile->geometryWidth; query->aig_Height = tile->geometryHeight;
            }
            result = TRUE;
            break;
        }
    }
    UnlockWorkbench();
    return result;
}

BOOL WBTileSuspend(struct AppIcon *target, struct WorkbenchBase *WorkbenchBase)
{
    struct AppIcon *icon;
    BOOL result = FALSE;
    LockWorkbench();
    WBTileCollectReplies(WorkbenchBase);
    ForeachNode(&WorkbenchBase->wb_AppIcons, icon) {
        if (icon == target && icon->ai_Tile && icon->ai_Tile->owner == FindTask(NULL)) {
            struct WBTile *tile = icon->ai_Tile;
            if (!tile->suspended) {
                tile->suspended = TRUE;
                tile->state.fresh = FALSE;
                tile->dirty = TRUE;
                ++WorkbenchBase->wb_TileSerial;
            }
            tile->hovered = FALSE;
            if (tile->dirty && !WorkbenchBase->wb_TileNotifyPending) {
                WorkbenchBase->wb_TileNotifiedSerial = WorkbenchBase->wb_TileSerial;
                WorkbenchBase->wb_TileNotifyPending =
                    NotifyWorkbench(WBNOTIFY_RefreshTiles, WBNOTIFY_AppIcon, WorkbenchBase);
            }
            result = TRUE;
            break;
        }
    }
    WBTileFlushPointers(WorkbenchBase);
    UnlockWorkbench();
    return result;
}

/* Called under the same registry mutex used by click-time dispatch. */
BOOL WBTileDetach(struct AppIcon *target, struct WorkbenchBase *WorkbenchBase)
{
    struct AppIcon *icon;
    BOOL result = FALSE;
    LockWorkbench();
    ForeachNode(&WorkbenchBase->wb_AppIcons, icon) {
        if (icon == target && icon->ai_Tile && icon->ai_Tile->owner == FindTask(NULL)) {
            icon->ai_Tile->owner = NULL;
            icon->ai_MsgPort = NULL;
            result = TRUE;
            break;
        }
    }
    UnlockWorkbench();
    return result;
}

void WBTileOwnerRemoved(struct Task *owner, struct WorkbenchBase *WorkbenchBase)
{
    struct AppIcon *icon;
    struct WBTileSnapshotNode *snapshot;
    LockWorkbench();
    ForeachNode(&WorkbenchBase->wb_AppIcons, icon) {
        if (icon->ai_Tile && icon->ai_Tile->owner == owner) {
            icon->ai_Tile->owner = NULL;
            icon->ai_MsgPort = NULL;
        }
    }
    for (snapshot=snapshots;snapshot;snapshot=snapshot->next)
        if (snapshot->owner==owner) snapshot->owner=NULL;
    UnlockWorkbench();
}

BOOL WBTileSnapshot(struct AppIconSnapshotRequest *request, struct WorkbenchBase *WorkbenchBase)
{
    struct WBTileSnapshotNode *node, *scan;
    struct AppIcon *icon;
    struct Task *owner=FindTask(NULL);
    ULONG count=0, omitted=0, active=0;
    BOOL result=FALSE;
    if (!request) return FALSE;
    request->aisr_Handle=NULL; request->aisr_Count=0;
    request->aisr_Flags=request->aisr_Omitted=0;
    if (!request->aisr_Screen || !request->aisr_Entries ||
        !request->aisr_Capacity || request->aisr_Capacity>APPICON_SNAPSHOT_MAX) return FALSE;
    memset(request->aisr_Entries,0,request->aisr_Capacity*sizeof(*request->aisr_Entries));
    LockWorkbench();
    sweepSnapshots();
    for (scan=snapshots;scan;scan=scan->next) {
        if (scan->owner==owner) goto done;
        ++active;
    }
    if (active>=4 || snapshotSerial == ~(IPTR)0) goto done;
    ForeachNode(&WorkbenchBase->wb_AppIcons,icon) {
        struct WBTile *tile=icon->ai_Tile;
        if (!tile || !tile->placementValid || tile->placementScreen!=request->aisr_Screen ||
            (ULONG)(WBTileClock(&tile->timer)-tile->placementTick)>=1000) continue;
        if (!(dynamicSnapshot(tile)?tile->bitmap:tile->fallbackBitmap)) { ++omitted; continue; }
        ++count;
    }
    if (count>request->aisr_Capacity) goto done;
    node=AllocVec(sizeof(*node),MEMF_CLEAR);
    if (!node) goto done;
    node->owner=owner;node->screen=request->aisr_Screen;
    node->graphics=OpenLibrary((CONST_STRPTR)"graphics.library",0);
    if (!node->graphics) { freeSnapshot(node); goto done; }
    ForeachNode(&WorkbenchBase->wb_AppIcons,icon) {
        struct WBTile *tile=icon->ai_Tile;
        struct AppIconSnapshotEntry *entry;
        struct GfxBase *GfxBase=(struct GfxBase *)node->graphics;
        ULONG now;
        struct BitMap *source;
        BOOL dynamic;
        if (!tile) continue;
        now=WBTileClock(&tile->timer);
        if (!tile->placementValid || tile->placementScreen!=node->screen ||
            (ULONG)(now-tile->placementTick)>=1000) continue;
        dynamic=dynamicSnapshot(tile);source=dynamic?tile->bitmap:tile->fallbackBitmap;
        if (!source) continue;
        entry=&node->entries[node->count];
        entry->ais_Bitmap=AllocBitMap(tile->state.width,tile->state.height,
            GetBitMapAttr(source,BMA_DEPTH),BMF_CLEAR,source);
        if (!entry->ais_Bitmap) { freeSnapshot(node); goto done; }
        ++node->count; /* partial allocation rollback owns this entry now */
        node->fallbacks[node->count-1]=AllocBitMap(tile->state.width,tile->state.height,
            GetBitMapAttr(tile->fallbackBitmap,BMA_DEPTH),BMF_CLEAR,tile->fallbackBitmap);
        if (!node->fallbacks[node->count-1]) { freeSnapshot(node);goto done; }
        BltBitMap(tile->fallbackBitmap,0,0,node->fallbacks[node->count-1],0,0,
            tile->state.width,tile->state.height,0xc0,0xff,NULL);
        BltBitMap(source,0,0,entry->ais_Bitmap,0,0,tile->state.width,
                  tile->state.height,0xc0,0xff,NULL);
        WaitBlit();
        entry->ais_Icon=icon;entry->ais_Generation=tile->registrationGeneration;
        entry->ais_Left=tile->placementLeft;entry->ais_Top=tile->placementTop;
        entry->ais_Width=tile->state.width;entry->ais_Height=tile->state.height;
        if (icon->ai_Text) {
            ULONG n=0;
            while (n<sizeof(entry->ais_Label)-1 && icon->ai_Text[n]) {
                entry->ais_Label[n]=icon->ai_Text[n];++n;
            }
        }
        node->frames[node->count-1]=dynamic?tile->frameGeneration:0;
    }
    node->handle=(APTR)++snapshotSerial;
    node->next=snapshots;snapshots=node;
    memcpy(request->aisr_Entries,node->entries,node->count*sizeof(*node->entries));
    request->aisr_Handle=node->handle;request->aisr_Count=node->count;
    request->aisr_Omitted=omitted;
    request->aisr_Flags=omitted?APPICON_SNAPSHOT_INCOMPLETE:0;
    result=TRUE;
done:
    UnlockWorkbench();return result;
}
BOOL WBTileSnapshotFree(APTR handle, struct WorkbenchBase *WorkbenchBase)
{
    struct WBTileSnapshotNode **link;BOOL result=FALSE;
    LockWorkbench();sweepSnapshots();
    for (link=&snapshots;*link;link=&(*link)->next)
        if ((*link)->handle==handle && (*link)->owner==FindTask(NULL)) {
            struct WBTileSnapshotNode *node=*link;*link=node->next;
            freeSnapshot(node);result=TRUE;break;
        }
    UnlockWorkbench();return result;
}
BOOL WBTileSnapshotActivate(struct AppIconSnapshotActivate *request, struct WorkbenchBase *WorkbenchBase)
{
    struct WBTileSnapshotNode *node;struct AppIcon *icon;BOOL result=FALSE;
    if (!request) return FALSE;
    LockWorkbench();sweepSnapshots();WBTileCollectReplies(WorkbenchBase);
    for (node=snapshots;node;node=node->next)
        if (node->handle==request->aisa_Handle && node->owner==FindTask(NULL)) break;
    if (!node || request->aisa_Index>=node->count) goto done;
    ForeachNode(&WorkbenchBase->wb_AppIcons,icon) {
        struct WBTile *tile=icon->ai_Tile;
        struct AppIconSnapshotEntry *entry=&node->entries[request->aisa_Index];
        LONG hit;
        if (icon!=entry->ais_Icon || !tile || tile->registrationGeneration!=entry->ais_Generation) continue;
        if (!tile->owner || !icon->ai_MsgPort || tile->suspended ||
            tile->frameGeneration!=node->frames[request->aisa_Index] ||
            !tile->placementValid || tile->placementScreen!=node->screen ||
            tile->placementLeft!=entry->ais_Left || tile->placementTop!=entry->ais_Top ||
            (ULONG)(WBTileClock(&tile->timer)-tile->placementTick)>=1000 ||
            !WBTileStateFresh(&tile->state,WBTileClock(&tile->timer)) ||
            request->aisa_X<0 || request->aisa_Y<0 ||
            request->aisa_X>=(LONG)tile->state.width || request->aisa_Y>=(LONG)tile->state.height) break;
        hit=WBTileStateHit(&tile->state,request->aisa_X,request->aisa_Y);
        if (hit>=0) result=WBTileSendMessage(icon,AMCLASSICON_Region,
            request->aisa_X,request->aisa_Y,tile->state.regions.air_Regions[hit].air_Name,WorkbenchBase)!=NULL;
        break;
    }
done:
    UnlockWorkbench();return result;
}

/* Caller is the sole presenter and must not concurrently read its bitmap while
 * this synchronous refresh runs. Pointers and geometry never change in place.
 * Missing/stale/suspended owners show the snapshot-owned static fallback with
 * epoch0, so one unhealthy producer never disables unrelated lifted tiles.
 * FALSE means only an invalid/foreign handle; activation always revalidates. */
BOOL WBTileSnapshotRefresh(APTR handle, struct WorkbenchBase *WorkbenchBase)
{
    struct WBTileSnapshotNode *node;
    ULONG i;
    LockWorkbench();sweepSnapshots();
    for (node=snapshots;node;node=node->next)
        if (node->handle==handle && node->owner==FindTask(NULL)) break;
    if (!node) { UnlockWorkbench();return FALSE; }
    for (i=0;i<node->count;++i) {
        struct AppIcon *icon;
        struct AppIconSnapshotEntry *entry=&node->entries[i];
        struct BitMap *source=node->fallbacks[i];
        struct GfxBase *GfxBase=(struct GfxBase *)node->graphics;
        UQUAD frame=0;
        ForeachNode(&WorkbenchBase->wb_AppIcons,icon) {
            struct WBTile *tile=icon->ai_Tile;
            if (icon!=entry->ais_Icon || !tile || tile->registrationGeneration!=entry->ais_Generation) continue;
            if (dynamicSnapshot(tile) && tile->placementValid && tile->placementScreen==node->screen &&
                tile->placementLeft==entry->ais_Left && tile->placementTop==entry->ais_Top &&
                (ULONG)(WBTileClock(&tile->timer)-tile->placementTick)<1000) {
                source=tile->bitmap;frame=tile->frameGeneration;
            }
            break;
        }
        if (node->frames[i]!=frame) {
            BltBitMap(source,0,0,entry->ais_Bitmap,0,0,entry->ais_Width,
                entry->ais_Height,0xc0,0xff,NULL);
            WaitBlit();node->frames[i]=frame;
        }
    }
    UnlockWorkbench();return TRUE;
}
