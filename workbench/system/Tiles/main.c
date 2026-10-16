/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>. All rights reserved.
    SPDX-License-Identifier: APL-1.1

    One resident declarative tile task. Source requests, notifications and
    visibility changes share one wait loop and one coalesced timer.
*/
#include "tiles_config.h"
#include "tiles_render.h"
#include "source.h"
#include "tiles_group.h"
#include "lift.h"
#include "input.h"
#include <dos/dosextens.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/notify.h>
#include <dos/var.h>
#include <exec/memory.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
#include <workbench/workbench.h>
#include <rexx/rxslib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <graphics/layers.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/workbench.h>
#include <proto/rexxsyslib.h>
#include <proto/timer.h>
#include <proto/arossupport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_LIMIT 40
#define GROUP_LIMIT 8
#define DRAW_INTERVAL 500UL
#define FAST_INTERVAL 33UL
#define HEARTBEAT_INTERVAL 2000UL
#define TILE_LOG(...) do { Forbid(); kprintf(__VA_ARGS__); Permit(); } while (0)
struct Device *TimerBase;
struct RxsLib *RexxSysBase;
struct Library *CommoditiesBase;
struct TileEntry {
    struct TileConfig config;
    struct TileValue value;
    struct DiskObject *fallback;
    struct AppIcon *icon;
    struct BitMap *bitmap, *staticBitmap;
    int group;
    struct NotifyRequest notify;
    char id[128], envPath[132];
    char definitionPath[512];
    BOOL active, seen;
    BOOL valid, healthy, dirty, live, watching, logged, refreshAgain;
    BOOL envTogglePending, envToggleValue, publishPending;
    ULONG lastPoll, lastHeartbeat, lastFrame, lastPublishAttempt;
};
struct ServiceGroup {
    struct TileGroupConfig config;
    struct TileGroupLayout layout;
    char id[128];
    unsigned first, count;
    LONG preferredX, preferredY;
    unsigned members[TILE_LIMIT];
    char path[512];
    struct DiskObject *fallback;
    struct AppIcon *icon;
    struct BitMap *bitmap;
    struct NotifyRequest notify;
    struct TileLift *panel;
    BOOL watching, dirty, edgeShown, traced;
    ULONG lastPublish;
    LONG left,top,liftLeft,liftTop,tracedLeft,tracedTop;

};
struct TileService {
    struct TileEntry tiles[TILE_LIMIT];
    unsigned count, cursor;
    struct ServiceGroup groups[GROUP_LIMIT];
    unsigned groupCount;
    struct TileLift *lift;
    struct TileInput *input;
    int liftedGroup;
    BOOL liftedAll;
    BOOL liftedTiles[TILE_LIMIT];
    APTR snapshot;
    struct AppIconSnapshotEntry snapshotEntries[APPICON_SNAPSHOT_MAX];
    ULONG snapshotCount, snapshotTick, liftRefreshTick;
    struct Screen *screen;
    struct MsgPort *apps, *commands, *notifications, *timerPort;
    struct timerequest *timer;
    struct TileSources sources;
    BOOL timerOpen, timerPending, stopping, trace, quitRequested;
    ULONG lastDraw, lastFastDraw, draws, wakes;
    struct NotifyRequest catalogNotify, changeNotify;
    BOOL catalogWatching, changeWatching, reloadPending;
    int argc; char **argv;
    struct MsgPort *screenPort; APTR screenNotify;
    ULONG reloadTick,generation,cleanupRetry;

};
static ULONG now_ms(void)
{
    struct EClockVal clock;
    ULONG frequency = ReadEClock(&clock);
    unsigned long long ticks = ((unsigned long long)clock.ev_hi << 32) | clock.ev_lo;
    return frequency ? (ULONG)((ticks / frequency) * 1000 +
                              ((ticks % frequency) * 1000) / frequency) : 0;
}
static void problem(struct TileEntry *tile, const char *reason)
{
    if (!tile->logged) {
        printf("Tiles: %s: %s; using static image\n", tile->id, reason);
        TILE_LOG("Tiles: %s: %s; using static image\n", tile->id, reason);
        tile->logged = TRUE;
    }
}
static void request_value(struct TileService *service, struct TileEntry *tile, ULONG now);
static void source_result(APTR owner, BOOL setting, BOOL success, CONST_STRPTR text, APTR context)
{
    struct TileEntry *tile = owner;
    struct TileService *service = context;
    struct TileValue parsed;
    if (service->stopping || !tile->active) return;
    if (!setting && tile->config.env[0]) tile->envTogglePending=FALSE;
    if (setting && success) { request_value(service, tile, now_ms()); return; }
    if (!setting && success && TileValueParse(tile->config.templateKind, text, &parsed)) {
        BOOL changed = !tile->live;
        if (tile->config.templateKind == TILE_TOGGLE) changed |= parsed.boolean != tile->value.boolean;
        else if (tile->config.templateKind == TILE_GAUGE || tile->config.templateKind == TILE_SLIDER)
            changed |= parsed.number != tile->value.number;
        else changed |= strcmp(parsed.text,tile->value.text) != 0 || parsed.number != tile->value.number;
        tile->value = parsed;
        tile->healthy = TRUE;
        tile->dirty |= changed;
        if (tile->refreshAgain) { tile->refreshAgain=FALSE; request_value(service,tile,now_ms()); }
        if (service->trace) TILE_LOG("[TILE2] source id=%s value=%s tick=%lu\n", tile->id, tile->value.text, (unsigned long)now_ms());
    } else {
        tile->healthy = FALSE;
        tile->dirty = FALSE;
        problem(tile, "source unavailable or invalid");
    }
    if(tile->group>=0)service->groups[tile->group].dirty=TRUE;
}
static void request_value(struct TileService *service, struct TileEntry *tile, ULONG now)
{
    char value[256];
    if (!tile->active || !tile->valid || service->stopping) return;
    if (tile->config.templateKind == TILE_EYES) {
        if (!tile->healthy) source_result(tile,FALSE,TRUE,"",service);
        tile->lastPoll=now;
        return;
    }
    if (tile->config.env[0]) {
        LONG length = GetVar(tile->config.env, value, sizeof(value), GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
        LONG total = IoErr();
        BOOL valid = length >= 0 && length < sizeof(value) && total < sizeof(value);
        if (valid && memchr(value, 0, length)) valid = FALSE;
        if (valid) value[length] = 0;
        tile->lastPoll = now;
        source_result(tile, FALSE, valid, valid ? value : "", service);
    } else if (TileSourcePending(&service->sources, tile)) {
        tile->refreshAgain=TRUE;
    } else {
        tile->lastPoll = now;
        if (!TileSourceSend(&service->sources, tile, FALSE, tile->config.getPort,
                            tile->config.getCommand, now))
            source_result(tile, FALSE, FALSE, "", service);
    }
}
static BOOL set_value(struct TileService *service, struct TileEntry *tile, const char *text)
{
    char command[512], weather[64];
    const char *normalized;
    struct TileValue checked;
    if (service->stopping || !tile->active || !tile->valid || !TileValueParse(tile->config.templateKind, text, &checked)) return FALSE;
    normalized=checked.text;
    if (tile->config.templateKind==TILE_WEATHER) {
        snprintf(weather,sizeof(weather),"%ld|%s",checked.number,checked.text); normalized=weather;
    }
    if (tile->config.env[0]) {
        /* Every valid ENV tile owns an active notification subscription.
         * Let that single path refresh the value, coalescing repeated writes. */
        if (!tile->watching || !SetVar(tile->config.env, normalized, -1, GVF_GLOBAL_ONLY)) return FALSE;
        if (tile->config.templateKind==TILE_TOGGLE) {
            tile->envTogglePending=TRUE; tile->envToggleValue=checked.boolean;
        }
        return TRUE;
    }
    if (!tile->config.setPort[0] ||
        !TileSetCommand(tile->config.setCommand, normalized, command, sizeof(command))) return FALSE;
    return TileSourceSend(&service->sources, tile, TRUE, tile->config.setPort, command, now_ms());
}
static ULONG presentation(struct TileService *service,struct TileEntry *tile)
{
    ULONG flags=0;LONG x=0,y=0;
    if(!tile->active || !service->screen)return 0;
    if(tile->group>=0) {
        struct ServiceGroup *group=&service->groups[tile->group];
        unsigned member=0;while(member<group->count && group->members[member]!=(unsigned)(tile-service->tiles))++member;
        if(member==group->count)return 0;
        if(service->lift && (service->liftedAll || service->liftedGroup==tile->group)) {
            flags=APPICONQF_VISIBLE;
            x=service->screen->MouseX-group->liftLeft-group->layout.members[member].x;
            y=service->screen->MouseY-group->liftTop-group->layout.members[member].y;
        } else if(group->panel) {
            flags=APPICONQF_VISIBLE;
            x=service->screen->MouseX-group->left-group->layout.members[member].x;
            y=service->screen->MouseY-group->top-group->layout.members[member].y;
        } else if(group->icon) {
            struct AppIconQuery query={group->icon,0,0,0};
            struct TagItem tags[]={{WBCTRLA_QueryAppIcon,(IPTR)&query},{TAG_DONE,0}};
            if(WorkbenchControlA(NULL,tags))flags=query.aiq_Flags;
            x=query.aiq_MouseX-group->layout.members[member].x;
            y=query.aiq_MouseY-group->layout.members[member].y;
            if(!(flags&APPICONQF_HOVERED))x=y=-1;
        }
        if((flags&APPICONQF_VISIBLE) && x>=0 && y>=0 && x<(LONG)tile->config.cellsW*64 && y<(LONG)tile->config.cellsH*64)flags|=APPICONQF_HOVERED;
        else flags&=~APPICONQF_HOVERED;
    } else if(service->lift && service->liftedTiles[tile-service->tiles]) {
        for(unsigned n=0;n<service->snapshotCount;++n)if(service->snapshotEntries[n].ais_Icon==tile->icon) {
            struct AppIconSnapshotEntry *entry=&service->snapshotEntries[n];
            x=service->screen->MouseX-entry->ais_Left;y=service->screen->MouseY-entry->ais_Top;
            flags=APPICONQF_VISIBLE;
            if(x>=0 && y>=0 && x<(LONG)entry->ais_Width && y<(LONG)entry->ais_Height)flags|=APPICONQF_HOVERED;
            break;
        }
    } else if(tile->icon) {
        struct AppIconQuery query={tile->icon,0,0,0};
        struct TagItem tags[]={{WBCTRLA_QueryAppIcon,(IPTR)&query},{TAG_DONE,0}};
        if(WorkbenchControlA(NULL,tags))flags=query.aiq_Flags;
        x=query.aiq_MouseX;y=query.aiq_MouseY;
    }
    BOOL hovered=(flags&(APPICONQF_VISIBLE|APPICONQF_HOVERED))==(APPICONQF_VISIBLE|APPICONQF_HOVERED);
    if(!hovered)x=y=0;
    if(tile->config.templateKind==TILE_EYES && (tile->value.hovered!=hovered || tile->value.pointerX!=x || tile->value.pointerY!=y)) {
        tile->value.hovered=hovered;tile->value.pointerX=x;tile->value.pointerY=y;tile->dirty=TRUE;
    }
    if(!tile->live)flags|=APPICONQF_VISIBLE;
    return flags;
}
/* Desktop publication may be throttled while a lifted presenter owns hover.
 * Retain the latest rendered cache for a later copy; never render it again just
 * because Workbench's underlying, covered icon is not eligible for fast mode. */
static BOOL publish_tile_cache(struct TileEntry *tile,ULONG now)
{
    struct AppIconPublish publish={tile->icon,tile->bitmap};
    struct TagItem tags[]={{WBCTRLA_PublishAppIcon,(IPTR)&publish},{TAG_DONE,0}};
    tile->lastPublishAttempt=now;
    tile->publishPending=!WorkbenchControlA(NULL,tags);
    if(!tile->publishPending)tile->lastHeartbeat=now;
    return !tile->publishPending;
}
/* A global lift may borrow this service's caches until drop/reload/close, all
 * of which close the presenter before freeing them. Foreign owners keep the
 * independent snapshot copy and token-validated activation path. */
static struct BitMap *snapshot_lift_bitmap(struct TileService *service,
                                           const struct AppIconSnapshotEntry *entry)
{
    for(unsigned t=0;t<service->count;++t) {
        struct TileEntry *tile=&service->tiles[t];
        if(tile->active && tile->icon && tile->icon==entry->ais_Icon) {
            struct BitMap *bitmap=tile->healthy&&tile->bitmap?tile->bitmap:tile->staticBitmap;
            if(bitmap)return bitmap;
        }
    }
    for(unsigned g=0;g<service->groupCount;++g)
        if(service->groups[g].icon && service->groups[g].icon==entry->ais_Icon && service->groups[g].bitmap)
            return service->groups[g].bitmap;
    return entry->ais_Bitmap;
}
/* -1 resource failure, 1 rendered (even if desktop publication is deferred). */
static int draw_tile(struct TileService *service, struct TileEntry *tile, ULONG now, BOOL fast)
{
    struct RastPort rp;
    unsigned width = tile->config.cellsW * 64, height = tile->config.cellsH * 64;
    if (!tile->bitmap)
        tile->bitmap = AllocBitMap(width, height,
            GetBitMapAttr(service->screen->RastPort.BitMap, BMA_DEPTH), BMF_CLEAR,
            service->screen->RastPort.BitMap);
    if (!tile->bitmap) return -1;
    InitRastPort(&rp); rp.BitMap = tile->bitmap;
    if (!TileRender(&rp, &tile->config, &tile->value, width, height)) return -1;
    /* Charge every successful render, before any publication can defer/fail. */
    now=now_ms();tile->lastFrame=now;
    if(fast)service->lastFastDraw=now;else service->lastDraw=now;
    ++service->draws;
    if (tile->group >= 0) {tile->live=TRUE;service->groups[tile->group].dirty=TRUE;}
    else if (!tile->live) {
        struct AppIconRegions regions = {1, {{0,0,width,height,"activate"}}};
        struct TagItem tags[] = {
            {WBAPPICONA_Cells,WBAPPICON_CELLS(tile->config.cellsW,tile->config.cellsH)},
            {WBAPPICONA_Surface,(IPTR)tile->bitmap},
            {WBAPPICONA_DefinitionPath,(IPTR)tile->definitionPath},
            {WBAPPICONA_PropagatePosition,TRUE},
            {WBAPPICONA_Regions,(IPTR)&regions},
            {WBAPPICONA_Cadence,DRAW_INTERVAL},
            {WBAPPICONA_Pointer,tile->config.pointerEnabled},
            {WBAPPICONA_HoverCadence,tile->config.hoverCadence==33?FAST_INTERVAL:0}, {TAG_DONE,0}
        };
        if (tile->icon) RemoveAppIcon(tile->icon);
        tile->icon = AddAppIconA(tile-service->tiles+1, 0,
            tile->config.label[0] ? tile->config.label : tile->id,
            service->apps, 0, tile->fallback, tags);
        if (!tile->icon) {
            tile->icon = AddAppIconA(tile-service->tiles+1,0,tile->id,service->apps,0,tile->fallback,NULL);
            return -1;
        }
        tile->live = TRUE;tile->publishPending=FALSE;tile->lastHeartbeat=now;
    } else {
        publish_tile_cache(tile,now);
    }
    tile->dirty = FALSE;
    if (service->lift) {
        TileLiftUpdateBitmap(service->lift,tile-service->tiles+1,tile->bitmap);
        TileLiftRefreshItem(service->lift,tile-service->tiles+1);
    }
    if (service->trace) TILE_LOG("[TILE2] draw id=%s tick=%lu count=%lu value=%s\n",
        tile->id, (unsigned long)now, (unsigned long)service->draws, tile->value.text);
    if (service->trace && tile->config.pointerEnabled) TILE_LOG("[TILEH] draw id=%s fast=%u tick=%lu x=%ld y=%ld hovered=%u\n",
        tile->id,fast,(unsigned long)now,tile->value.pointerX,tile->value.pointerY,tile->value.hovered);
    return 1;
}
static void activate_tile(struct TileService *service, struct TileEntry *tile, LONG x)
{
    char value[32];
    if (!tile->healthy) return;
    if (tile->config.templateKind==TILE_TOGGLE)
        set_value(service,tile,(tile->envTogglePending ? tile->envToggleValue : tile->value.boolean) ? "off" : "on");
    else if (tile->config.templateKind==TILE_SLIDER) {
        LONG number=x*100/(tile->config.cellsW*64-1);
        if (number<0) number=0; if (number>100) number=100;
        snprintf(value,sizeof(value),"%ld",(long)number);set_value(service,tile,value);
    }
}
static void close_panels(struct TileService *service);
static void group_click(struct TileService *service,unsigned index,LONG x,LONG y);
static void free_snapshot(struct TileService *service)
{
    if(service->snapshot) {
        struct TagItem tags[]={{WBCTRLA_FreeAppIconSnapshot,(IPTR)service->snapshot},{TAG_DONE,0}};
        WorkbenchControlA(NULL,tags);service->snapshot=NULL;service->snapshotCount=0;
    }
}
static void drop_tiles(struct TileService *service)
{
    if (!service->lift) return;
    ULONG begin=now_ms();
    TileLiftClose(service->lift);service->lift=NULL;
    TileInputSetLifted(service->input,FALSE);
    free_snapshot(service);
    if (service->trace) TILE_LOG("[TILEL] drop tick=%lu elapsed=%lu draws=%lu\n",(unsigned long)now_ms(),(unsigned long)(now_ms()-begin),(unsigned long)service->draws);
}
/* Run in the service task, never inside the commodities callback. A screen
 * change dismisses the old presenter before queued clicks can activate it. */
static BOOL lift_screen_front(struct TileService *service)
{
    if(!service->screen)return FALSE;
    ULONG token=LockIBase(0);
    BOOL front=IntuitionBase->FirstScreen==service->screen;
    UnlockIBase(token);
    return front;
}
static BOOL lift_tiles(struct TileService *service, int group)
{
    struct TileLiftItem items[TILE_LIMIT];
    unsigned i,count=0;
    ULONG begin=now_ms();
    if (service->lift) { drop_tiles(service);return TRUE; }
    if (!service->screen || !lift_screen_front(service)) return FALSE;
    close_panels(service);
    if(service->trace)TILE_LOG("[TILEL] begin group=%s tick=%lu draws=%lu\n",group<0?"all":service->groups[group].id,(unsigned long)begin,(unsigned long)service->draws);
    service->liftedGroup=group;service->liftedAll=group<0;
    memset(service->liftedTiles,0,sizeof(service->liftedTiles));
    for (i=0;i<service->count;++i) {
        struct TileEntry *tile=&service->tiles[i];
        LONG x,y;
        unsigned w=tile->config.cellsW*64,h=tile->config.cellsH*64;
        if (!tile->active || !tile->fallback || !w || !h) continue;
        if (tile->group>=0) {
            struct ServiceGroup *g=&service->groups[tile->group];
            int32_t gx,gy;
            if (group>=0 && tile->group!=group) continue;
            if (group<0 && g->config.show==TILE_GROUP_ALWAYS){g->liftLeft=g->left;g->liftTop=g->top;continue;}
            if (!TileGroupPlace(&g->config,&g->layout,service->screen->Width,service->screen->Height,
                g->preferredX,g->preferredY,1,1,service->screen->MouseX,service->screen->MouseY,&gx,&gy)) continue;
            unsigned member=0;while(member<g->count && g->members[member]!=i)++member;
            if(member==g->count)continue;
            if(g->config.show!=TILE_GROUP_LIFTED){gx=g->left;gy=g->top;}
            g->liftLeft=gx;g->liftTop=gy;
            x=gx+g->layout.members[member].x;y=gy+g->layout.members[member].y;
        } else continue;
        items[count].id=i+1;items[count].left=x;items[count].top=y;
        items[count].width=w;items[count].height=h;
        items[count].bitmap=tile->healthy&&tile->bitmap ? tile->bitmap:tile->staticBitmap;
        if (!items[count].bitmap) continue;
        if (service->trace) TILE_LOG("[TILEL] slot id=%s x=%ld y=%ld w=%u h=%u frame=%lu\n",tile->id,(long)x,(long)y,w,h,(unsigned long)tile->lastFrame);
        service->liftedTiles[i]=TRUE;
        ++count;
    }
    if(group<0) {
        struct AppIconSnapshotRequest request={service->screen,service->snapshotEntries,APPICON_SNAPSHOT_MAX,NULL,0,0,0};
        struct TagItem tags[]={{WBCTRLA_SnapshotAppIcons,(IPTR)&request},{TAG_DONE,0}};
        if(!WorkbenchControlA(NULL,tags))return FALSE;
        service->snapshot=request.aisr_Handle;service->snapshotCount=request.aisr_Count;
        if(request.aisr_Flags || count+request.aisr_Count>TILE_LIMIT) {free_snapshot(service);return FALSE;}
        for(unsigned n=0;n<request.aisr_Count;++n) {
            struct AppIconSnapshotEntry *entry=&service->snapshotEntries[n];
            items[count].id=TILE_LIMIT+1+n;items[count].bitmap=snapshot_lift_bitmap(service,entry);
            items[count].left=entry->ais_Left;items[count].top=entry->ais_Top;
            items[count].width=entry->ais_Width;items[count].height=entry->ais_Height;
            for(unsigned t=0;t<service->count;++t)
                if(service->tiles[t].icon==entry->ais_Icon)service->liftedTiles[t]=TRUE;
            for(unsigned g=0;g<service->groupCount;++g)
                if(service->groups[g].icon==entry->ais_Icon) {
                    service->groups[g].liftLeft=entry->ais_Left;
                    service->groups[g].liftTop=entry->ais_Top;
                }
            if(service->trace)TILE_LOG("[TILEL] slot id=%s x=%ld y=%ld w=%lu h=%lu frame=0\n",entry->ais_Label,(long)entry->ais_Left,(long)entry->ais_Top,(unsigned long)entry->ais_Width,(unsigned long)entry->ais_Height);
            ++count;
        }
    }
    if (!count) {free_snapshot(service);return FALSE;}
    service->snapshotTick=service->liftRefreshTick=now_ms();
    TileInputSetLifted(service->input,TRUE);
    service->lift=TileLiftOpen(service->screen,items,count);
    if (!service->lift) {TileInputSetLifted(service->input,FALSE);free_snapshot(service);return FALSE;}
    if (service->trace) TILE_LOG("[TILEL] open group=%s count=%u tick=%lu elapsed=%lu draws=%lu\n",group<0?"all":service->groups[group].id,count,(unsigned long)now_ms(),(unsigned long)(now_ms()-begin),(unsigned long)service->draws);
    return TRUE;
}
static void discard_app_messages(struct TileService *service)
{
    struct Message *message;
    if(!service->apps)return;
    while((message=GetMsg(service->apps)))ReplyMsg(message);
}
static void app_messages(struct TileService *service)
{
    struct AppMessage *message;
    while ((message = (struct AppMessage *)GetMsg(service->apps))) {
        if(!service->stopping && message->am_ID>TILE_LIMIT && message->am_ID<=TILE_LIMIT+service->groupCount) {
            unsigned group=message->am_ID-TILE_LIMIT-1;
            if(message->am_Class==AMCLASSICON_Region)group_click(service,group,message->am_MouseX,message->am_MouseY);
            else if(message->am_Class==AMCLASSICON_Open)OpenWorkbenchObjectA(service->groups[group].path,NULL);
        }
        if (!service->stopping && message->am_ID && message->am_ID <= service->count && service->tiles[message->am_ID-1].active) {
            struct TileEntry *tile = &service->tiles[message->am_ID-1];
            if (message->am_Class == AMCLASSICON_Region && tile->healthy) {
                activate_tile(service,tile,message->am_MouseX);
            }
            if (service->trace && message->am_Class>=AMCLASSICON_PointerEnter && message->am_Class<=AMCLASSICON_PointerLeave)
                TILE_LOG("[TILEH] pointer id=%s class=%lu x=%ld y=%ld\n",tile->id,(unsigned long)message->am_Class,(long)message->am_MouseX,(long)message->am_MouseY);
            /* Visibility events wake this loop; the authoritative query below
             * observes the current presenter state before generating pixels. */
        }
        ReplyMsg(&message->am_Message);
    }
}
static struct TileEntry *find_tile(struct TileService *service, const char *id)
{
    unsigned i;
    for (i=0; i<service->count; ++i) if (service->tiles[i].active && !strcmp(service->tiles[i].id,id)) return &service->tiles[i];
    return NULL;
}
static void command_messages(struct TileService *service)
{
    struct RexxMsg *message;
    while ((message = (struct RexxMsg *)GetMsg(service->commands))) {
        char command[512], *verb, *id, *argument;
        const char *answer = "Unsupported TILE command";
        char result[256]; LONG error = 10;
        if (!IsRexxMsg(message) || !message->rm_Args[0]) { ReplyMsg((struct Message *)message); continue; }
        if (service->stopping) {
            message->rm_Result1=10; message->rm_Result2=0;
            ReplyMsg((struct Message *)message); continue;
        }
        if (strlen((const char *)message->rm_Args[0]) < sizeof(command)) {
            strcpy(command,(const char *)message->rm_Args[0]);
            verb = strtok(command," ");
            if (verb && !strcmp(verb,"TILE")) {
                verb = strtok(NULL," "); id = strtok(NULL," "); argument = strtok(NULL,"");
                if (verb && !strcmp(verb,"STATS")) {
                    snprintf(result,sizeof(result),"draws=%lu wakes=%lu tiles=%u lifted=%u",(unsigned long)service->draws,(unsigned long)service->wakes,service->count,service->lift!=NULL);
                    answer=result; error=0;
                } else if (verb && !strcmp(verb,"QUIT")) { service->quitRequested=TRUE; answer="Stop requested; pending replies retain the service"; error=0; }
                else if (verb && !strcmp(verb,"DROP")) { drop_tiles(service);answer="Dropped";error=0; }
                else if (verb && !strcmp(verb,"LIFT")) {
                    int group=-1;unsigned g;
                    if (id) for(g=0;g<service->groupCount;++g) if(!strcmp(id,service->groups[g].id)) {group=g;break;}
                    if ((!id || group>=0) && lift_tiles(service,group)) {answer="Lifted";error=0;}
                }
                else if (verb && id) {
                    struct TileEntry *tile=find_tile(service,id);
                    if (tile && !strcmp(verb,"CHANGED")) { request_value(service,tile,now_ms()); answer="Accepted"; error=0; }
                    else if (tile && !strcmp(verb,"GET") && tile->healthy) {
                        answer=tile->value.text;
                        if (tile->config.templateKind==TILE_WEATHER) {
                            snprintf(result,sizeof(result),"%ld|%s",tile->value.number,tile->value.text);answer=result;
                        }
                        error=0;
                    }
                    else if (tile && !strcmp(verb,"SET") && argument && set_value(service,tile,argument)) { answer="Accepted"; error=0; }
                    else if (tile && !strcmp(verb,"PANEL")) answer="Panel resolver unavailable";
                }
            }
        }
        message->rm_Result1=error; message->rm_Result2=0;
        if (!error && (message->rm_Action & RXFF_RESULT))
            message->rm_Result2=(IPTR)CreateArgstring(answer,strlen(answer));
        ReplyMsg((struct Message *)message);
    }
}
static void notifications(struct TileService *service)
{
    struct NotifyMessage *message;
    while ((message=(struct NotifyMessage *)GetMsg(service->notifications))) {
        struct TileEntry *tile=(struct TileEntry *)message->nm_NReq->nr_UserData;
        if(message->nm_NReq==&service->catalogNotify || message->nm_NReq==&service->changeNotify || !tile)
            {service->reloadPending=TRUE;service->reloadTick=now_ms();}
        else if (!service->stopping && tile->active) request_value(service,tile,now_ms());
        ReplyMsg((struct Message *)message);
    }
}
static BOOL cache_static(struct TileService *service,struct TileEntry *tile)
{
        struct Layer_Info *info=NewLayerInfo();
        struct Layer *layer=NULL;
        unsigned width=tile->config.cellsW*64,height=tile->config.cellsH*64;
        tile->staticBitmap=AllocBitMap(width,height,
            GetBitMapAttr(service->screen->RastPort.BitMap,BMA_DEPTH),BMF_CLEAR,service->screen->RastPort.BitMap);
        if(info && tile->staticBitmap)
            layer=CreateUpfrontLayer(info,tile->staticBitmap,0,0,width-1,height-1,LAYERSIMPLE,NULL);
        if(layer) {DrawIconStateA(layer->rp,tile->fallback,NULL,0,0,IDS_NORMAL,NULL);WaitBlit();DeleteLayer(0,layer);}
        if(info)DisposeLayerInfo(info);
        if(!layer && tile->staticBitmap) {FreeBitMap(tile->staticBitmap);tile->staticBitmap=NULL;}
        return layer!=NULL;
}
static BOOL definition_path(const char *path,char *output,ULONG capacity)
{
    char parent[512];BPTR lock;const char *name;
    if(strlen(path)>=sizeof(parent))return FALSE;
    strcpy(parent,path);
    if(strlen(parent)>5 && !strcmp(parent+strlen(parent)-5,".info"))parent[strlen(parent)-5]=0;
    name=FilePart(parent);
    char leaf[128];if(strlen(name)>=sizeof(leaf))return FALSE;strcpy(leaf,name);
    parent[name-parent]=0;
    lock=Lock(parent[0]?parent:"",ACCESS_READ);if(!lock)return FALSE;
    BOOL ok=NameFromLock(lock,output,capacity);UnLock(lock);
    return ok && AddPart(output,leaf,capacity);
}
static void release_tile(struct TileService *service,struct TileEntry *tile)
{
    if(tile->watching){EndNotify(&tile->notify);tile->watching=FALSE;}
    if(tile->icon){RemoveAppIcon(tile->icon);tile->icon=NULL;}
    if(tile->bitmap){FreeBitMap(tile->bitmap);tile->bitmap=NULL;}
    if(tile->staticBitmap){FreeBitMap(tile->staticBitmap);tile->staticBitmap=NULL;}
    if(tile->fallback){FreeDiskObject(tile->fallback);tile->fallback=NULL;}
    tile->active=tile->valid=tile->healthy=FALSE;
}
static void load_tile(struct TileService *service,const char *path,int group)
{
    char filename[512],error[128],qualified[128];
    struct DiskObject *fallback;
    struct TileConfig config;
    struct TileEntry *tile=NULL;
    BOOL valid,reused=FALSE;
    if(!definition_path(path,filename,sizeof(filename)))return;
    const char *name=FilePart(filename);
    if(snprintf(qualified,sizeof(qualified),group>=0?"%s/%s":"%s",group>=0?service->groups[group].id:name,name)>=(int)sizeof(qualified))return;
    for(unsigned i=0;i<service->count;++i)
        if(service->tiles[i].active && !strcmp(service->tiles[i].definitionPath,filename))return;
    fallback=GetDiskObject(filename);if(!fallback)return;
    if(group>=0 && fallback->do_Type==WBDRAWER){FreeDiskObject(fallback);return;}
    valid=TileConfigParse((const char *const *)fallback->do_ToolTypes,&config,error,sizeof(error));
    /* Stable slots let an outstanding provider retain its exact owner address.
     * A moved definition can retain that slot only when its entire contract is
     * unchanged. No pending slot is recycled for a different definition. */
    for(unsigned pass=0;pass<2 && !tile;++pass)for(unsigned i=0;i<service->count;++i) {
        struct TileEntry *candidate=&service->tiles[i];
        if(candidate->active || !candidate->fallback || !valid || !candidate->valid)continue;
        if(strcmp(pass?(const char *)FilePart(candidate->definitionPath):candidate->definitionPath,pass?name:filename))continue;
        if(!memcmp(&candidate->config,&config,sizeof(config))){tile=candidate;reused=TRUE;break;}
    }
    if(!tile) {
        for(unsigned i=0;i<service->count;++i)if(!service->tiles[i].active && !TileSourcePending(&service->sources,&service->tiles[i])) {tile=&service->tiles[i];break;}
        if(!tile && service->count<TILE_LIMIT)tile=&service->tiles[service->count++];
        if(!tile){FreeDiskObject(fallback);return;}
        release_tile(service,tile);notifications(service);memset(tile,0,sizeof(*tile));
        tile->config=config;tile->valid=valid;
    } else {
        if(tile->fallback)FreeDiskObject(tile->fallback);
        if(tile->staticBitmap){FreeBitMap(tile->staticBitmap);tile->staticBitmap=NULL;}
    }
    tile->fallback=fallback;tile->active=TRUE;tile->group=group;
    strcpy(tile->definitionPath,filename);strcpy(tile->id,qualified);
    if(!valid){tile->config.cellsW=tile->config.cellsH=1;problem(tile,error);}
    if(service->screen && !cache_static(service,tile)){tile->valid=FALSE;problem(tile,"static fallback cache unavailable");}
    if(group>=0) {
        struct ServiceGroup *g=&service->groups[group];g->members[g->count++]=(unsigned)(tile-service->tiles);
    }
    else if(service->screen && tile->staticBitmap) {
        struct TagItem tags[]={{WBAPPICONA_Cells,WBAPPICON_CELLS(tile->config.cellsW,tile->config.cellsH)},
            {WBAPPICONA_Surface,(IPTR)tile->staticBitmap},{WBAPPICONA_DefinitionPath,(IPTR)tile->definitionPath},
            {WBAPPICONA_PropagatePosition,TRUE},{TAG_DONE,0}};
        tile->icon=AddAppIconA(tile-service->tiles+1,0,tile->id,service->apps,0,tile->fallback,tags);
    }
    if(tile->valid && !reused && tile->config.env[0]) {
        snprintf(tile->envPath,sizeof(tile->envPath),"ENV:%s",tile->config.env);
        tile->notify.nr_Name=tile->envPath;tile->notify.nr_UserData=(IPTR)tile;
        tile->notify.nr_Flags=NRF_SEND_MESSAGE|NRF_WAIT_REPLY;
        tile->notify.nr_stuff.nr_Msg.nr_Port=service->notifications;
        tile->watching=StartNotify(&tile->notify);
        if(!tile->watching){tile->valid=FALSE;problem(tile,"ENV notification unavailable");}
    }
    if(tile->valid) {
        if(!reused)request_value(service,tile,now_ms());
        else if(group<0){tile->live=FALSE;tile->dirty=TRUE;}
    }
}
static void load_path(struct TileService *service,const char *path)
{
    BPTR lock=Lock(path,ACCESS_READ);
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    struct DiskObject *icon=NULL;
    struct TileGroupMember members[TILE_LIMIT];
    struct ServiceGroup *group;
    unsigned index,candidates=0,active=0;
    BOOL complete=TRUE;
    char error[128],child[512],canonical[512];
    if(!lock || !fib || !Examine(lock,fib) || fib->fib_DirEntryType<=0) {
        if(lock)UnLock(lock);if(fib)FreeDosObject(DOS_FIB,fib);load_tile(service,path,-1);return;
    }
    if(service->groupCount==GROUP_LIMIT || !NameFromLock(lock,canonical,sizeof(canonical)))goto done;
    icon=GetDiskObject(canonical);if(!icon)goto done;
    index=service->groupCount;group=&service->groups[index];
    if(strlen(FilePart(canonical))>=sizeof(group->id))goto done;
    strcpy(group->id,FilePart(canonical));strcpy(group->path,canonical);
    if(!TileGroupConfigParse((const char *const *)icon->do_ToolTypes,group->id,&group->config,error,sizeof(error)))goto done;
    for(unsigned g=0;g<service->groupCount;++g)if(!strcmp(group->id,service->groups[g].id))goto done;
    for(unsigned t=0;t<service->count;++t)active+=!!service->tiles[t].active;
    while(ExNext(lock,fib)) {
        size_t n=strlen(fib->fib_FileName);
        if(fib->fib_FileName[0]=='.' || fib->fib_DirEntryType>0 || n<6 || strcmp(fib->fib_FileName+n-5,".info"))continue;
        if(++candidates>TILE_LIMIT-active || strlen(canonical)+1+n>=sizeof(child))goto done;
    }
    if(IoErr()!=ERROR_NO_MORE_ENTRIES || !Examine(lock,fib))goto done;
    group->preferredX=icon->do_CurrentX==NO_ICON_POSITION?0:icon->do_CurrentX;
    group->preferredY=icon->do_CurrentY==NO_ICON_POSITION?0:icon->do_CurrentY;
    while(ExNext(lock,fib)) {
        size_t n=strlen(fib->fib_FileName);
        if(fib->fib_FileName[0]=='.' || fib->fib_DirEntryType>0 || n<6 || strcmp(fib->fib_FileName+n-5,".info"))continue;
        if(group->count==TILE_LIMIT || snprintf(child,sizeof(child),"%s/%s",canonical,fib->fib_FileName)>=(int)sizeof(child)){complete=FALSE;break;}
        load_tile(service,child,index);
    }
    if(complete && IoErr()!=ERROR_NO_MORE_ENTRIES)complete=FALSE;
    memset(members,0,sizeof(members));
    for(unsigned j=0;j<group->count;++j) {
        struct TileEntry *tile=&service->tiles[group->members[j]];
        members[j].width=tile->config.cellsW*64;members[j].height=tile->config.cellsH*64;
        members[j].x=tile->fallback->do_CurrentX;members[j].y=tile->fallback->do_CurrentY;
        members[j].explicitPosition=members[j].x!=NO_ICON_POSITION && members[j].y!=NO_ICON_POSITION;
    }
    if(!complete || group->count!=candidates || !TileGroupArrange(&group->config,members,group->count,64,64,&group->layout) || group->layout.width>2048 || group->layout.height>2048) {
        for(unsigned j=0;j<group->count;++j)service->tiles[group->members[j]].active=FALSE;
        memset(group,0,sizeof(*group));goto done;
    }
    group->fallback=icon;icon=NULL;group->dirty=TRUE;
    group->notify.nr_Name=group->path;group->notify.nr_Flags=NRF_SEND_MESSAGE|NRF_WAIT_REPLY;
    group->notify.nr_stuff.nr_Msg.nr_Port=service->notifications;group->watching=StartNotify(&group->notify);
    ++service->groupCount;
done:
    if(icon)FreeDiskObject(icon);if(fib)FreeDosObject(DOS_FIB,fib);if(lock)UnLock(lock);
}
static void scan_catalog(struct TileService *service)
{
    BPTR lock=Lock("HOME:Tiles",ACCESS_READ);
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    if(lock && fib && Examine(lock,fib))while(ExNext(lock,fib)) {
        char path[512];size_t n=strlen(fib->fib_FileName);
        if(fib->fib_FileName[0]=='.')continue;
        if(fib->fib_DirEntryType<=0 && (n<6 || strcmp(fib->fib_FileName+n-5,".info")))continue;
        snprintf(path,sizeof(path),"HOME:Tiles/%s",fib->fib_FileName);
        if(fib->fib_DirEntryType<=0) {
            path[strlen(path)-5]=0;BPTR object=Lock(path,ACCESS_READ);
            if(object){struct FileInfoBlock other;BOOL directory=Examine(object,&other) && other.fib_DirEntryType>0;UnLock(object);if(directory)continue;}
        }
        load_path(service,path);
    }
    if(fib)FreeDosObject(DOS_FIB,fib);if(lock)UnLock(lock);
}
static void reload_catalog(struct TileService *service)
{
    ++service->generation;
    drop_tiles(service);
    if(service->input){TileInputClose(service->input);service->input=NULL;}
    for(unsigned g=0;g<service->groupCount;++g) {
        struct ServiceGroup *group=&service->groups[g];
        if(group->watching)EndNotify(&group->notify);
        if(group->panel)TileLiftClose(group->panel);
        if(group->icon)RemoveAppIcon(group->icon);
        if(group->bitmap)FreeBitMap(group->bitmap);
        if(group->fallback)FreeDiskObject(group->fallback);
    }
    notifications(service);
    memset(service->groups,0,sizeof(service->groups));service->groupCount=0;
    for(unsigned t=0;t<service->count;++t) {
        struct TileEntry *tile=&service->tiles[t];tile->active=FALSE;
        if(tile->icon){RemoveAppIcon(tile->icon);tile->icon=NULL;}
    }
    discard_app_messages(service);
    scan_catalog(service);
    for(int i=1;i<service->argc;++i)if(strcmp(service->argv[i],"TRACE"))load_path(service,service->argv[i]);
    if(!service->groupCount && service->argc<=2)load_path(service,"SYS:Tiles/Control");
    for(unsigned t=0;t<service->count;++t)if(!service->tiles[t].active)release_tile(service,&service->tiles[t]);
    notifications(service);
    if(service->screen) {
        const char *keys[GROUP_LIMIT];for(unsigned g=0;g<service->groupCount;++g)keys[g]=service->groups[g].config.lift;
        service->input=TileInputOpen(service->screen,keys,service->groupCount);
    }
    if(service->catalogWatching) {
        EndNotify(&service->catalogNotify);service->catalogWatching=FALSE;
        notifications(service);
        service->catalogWatching=StartNotify(&service->catalogNotify);
    }
    service->reloadPending=FALSE;
}
static void group_click(struct TileService *service,unsigned index,LONG x,LONG y)
{
    if(index>=service->groupCount)return;
    struct ServiceGroup *group=&service->groups[index];
    for(unsigned n=group->count;n;--n) {
        unsigned j=n-1;struct TileEntry *tile=&service->tiles[group->members[j]];
        LONG dx=x-group->layout.members[j].x,dy=y-group->layout.members[j].y;
        if(tile->active && dx>=0 && dy>=0 && dx<(LONG)tile->config.cellsW*64 && dy<(LONG)tile->config.cellsH*64) {
            activate_tile(service,tile,dx);return;
        }
    }
}
static void close_panels(struct TileService *service)
{
    TileInputSetPanels(service->input,NULL,0);
    for(unsigned g=0;g<service->groupCount;++g)if(service->groups[g].panel) {
        TileLiftClose(service->groups[g].panel);service->groups[g].panel=NULL;
        service->groups[g].edgeShown=FALSE;
    }
}
static void group_work(struct TileService *service,ULONG now)
{
    struct TileInputPanel panels[GROUP_LIMIT];unsigned panelCount=0;
    if(!service->screen)return;
    BOOL front=lift_screen_front(service);
    for(unsigned g=0;g<service->groupCount;++g) {
        struct ServiceGroup *group=&service->groups[g];
        unsigned width=(group->layout.width+63)&~63u,height=(group->layout.height+63)&~63u;
        if(!width || !height)continue;
        int32_t x,y;
        struct TileGroupLayout extent=group->layout;extent.width=width;extent.height=height;
        if(!TileGroupPlace(&group->config,&extent,service->screen->Width,service->screen->Height,
            group->preferredX,group->preferredY,0,0,0,0,&x,&y))continue;
        group->left=x;group->top=y;
        if(!group->bitmap) {
            group->bitmap=AllocBitMap(width,height,GetBitMapAttr(service->screen->RastPort.BitMap,BMA_DEPTH),BMF_CLEAR,service->screen->RastPort.BitMap);
            group->dirty=TRUE;
        }
        if(!group->bitmap)continue;
        ULONG interval=DRAW_INTERVAL;
        for(unsigned j=0;j<group->count;++j)if(service->tiles[group->members[j]].config.hoverCadence==33 && service->tiles[group->members[j]].value.hovered)interval=FAST_INTERVAL;
        if(group->dirty && now-group->lastPublish>=interval) {
            struct RastPort rp;InitRastPort(&rp);rp.BitMap=group->bitmap;
            SetAPen(&rp,0);RectFill(&rp,0,0,width-1,height-1);
            for(unsigned j=0;j<group->count;++j) {
                struct TileEntry *tile=&service->tiles[group->members[j]];
                struct BitMap *bitmap=tile->healthy && tile->bitmap?tile->bitmap:tile->staticBitmap;
                if(bitmap)BltBitMap(bitmap,0,0,group->bitmap,group->layout.members[j].x,group->layout.members[j].y,
                    tile->config.cellsW*64,tile->config.cellsH*64,0xc0,0xff,NULL);
            }
            WaitBlit();
            if(group->icon) {
                struct AppIconPublish frame={group->icon,group->bitmap};
                struct TagItem tags[]={{WBCTRLA_PublishAppIcon,(IPTR)&frame},{TAG_DONE,0}};
                if(WorkbenchControlA(NULL,tags)){group->dirty=FALSE;group->lastPublish=now;}
            } else {group->dirty=FALSE;group->lastPublish=now;}
            if(group->panel)TileLiftRefreshItem(group->panel,TILE_LIMIT+1+g);
        }
        if(group->config.show==TILE_GROUP_ALWAYS && !group->icon) {
            struct AppIconRegions regions={1,{{0,0,width,height,"group"}}};
            struct TagItem tags[]={{WBAPPICONA_Cells,WBAPPICON_CELLS(width/64,height/64)},
                {WBAPPICONA_Surface,(IPTR)group->bitmap},{WBAPPICONA_Regions,(IPTR)&regions},
                {WBAPPICONA_Cadence,DRAW_INTERVAL},{WBAPPICONA_Group,TRUE},
                {WBAPPICONA_Pointer,TRUE},{WBAPPICONA_HoverCadence,FAST_INTERVAL},
                {WBAPPICONA_GroupAnchor,group->config.anchor},{WBAPPICONA_DefinitionPath,(IPTR)group->path},
                {WBAPPICONA_SupportsOpen,TRUE},{WBAPPICONA_PropagatePosition,TRUE},{TAG_DONE,0}};
            group->fallback->do_CurrentX=group->preferredX;group->fallback->do_CurrentY=group->preferredY;
            group->icon=AddAppIconA(TILE_LIMIT+1+g,0,group->id,service->apps,0,group->fallback,tags);
        }
        BOOL placed=group->config.show!=TILE_GROUP_ALWAYS;
        if(group->icon) {
            struct AppIconGeometry geometry={group->icon,0,0,0,0,0};
            struct TagItem query[]={{WBCTRLA_QueryAppIconGeometry,(IPTR)&geometry},{TAG_DONE,0}};
            if(WorkbenchControlA(NULL,query) && (geometry.aig_Flags&APPICONGF_VALID)) {
                group->left=geometry.aig_Left;group->top=geometry.aig_Top;placed=TRUE;
            }
            if(now-group->lastPublish>=HEARTBEAT_INTERVAL) {
                struct AppIconPublish heartbeat={group->icon,NULL};
                struct TagItem tags[]={{WBCTRLA_PublishAppIcon,(IPTR)&heartbeat},{TAG_DONE,0}};
                WorkbenchControlA(NULL,tags);group->lastPublish=now;
            }
        }
        if(group->config.show==TILE_GROUP_EDGE) {
            LONG mx=service->screen->MouseX,my=service->screen->MouseY;
            BOOL edge=(group->config.anchor==TILE_GROUP_TOP && my<=0) ||
                (group->config.anchor==TILE_GROUP_BOTTOM && my>=service->screen->Height-1) ||
                (group->config.anchor==TILE_GROUP_LEFT && mx<=0) ||
                (group->config.anchor==TILE_GROUP_RIGHT && mx>=service->screen->Width-1);
            BOOL inside=mx>=x && my>=y && mx<x+(LONG)width && my<y+(LONG)height;
            BOOL show=front && !service->lift && (edge || (group->panel && inside));
            if(show && !group->panel) {
                struct TileLiftItem item={TILE_LIMIT+1+g,group->bitmap,0,0,width,height};
                group->panel=TilePanelOpen(service->screen,&item,1,x,y,width,height);
            } else if(!show && group->panel) {
                TileLiftClose(group->panel);group->panel=NULL;
            }
            if(group->panel) {
                struct TileLiftEvent event;TileLiftPump(group->panel,&event);
                panels[panelCount++]=(struct TileInputPanel){g,x,y,width,height};
            }
            if(group->edgeShown!=!!group->panel && service->trace)
                TILE_LOG("[TILEG] edge id=%s shown=%u tick=%lu generation=%lu\n",group->id,group->panel!=NULL,(unsigned long)now,(unsigned long)service->generation);
            group->edgeShown=!!group->panel;
        }
        if(service->trace && placed && (!group->traced ||
            group->tracedLeft!=group->left || group->tracedTop!=group->top)) {
            TILE_LOG("[TILEG] group id=%s show=%u x=%ld y=%ld w=%u h=%u screenw=%u screenh=%u members=%u tick=%lu generation=%lu\n",
                group->id,group->config.show,(long)group->left,(long)group->top,width,height,service->screen->Width,service->screen->Height,group->count,(unsigned long)now,(unsigned long)service->generation);
            for(unsigned j=0;j<group->count;++j) {
                struct TileEntry *tile=&service->tiles[group->members[j]];
                TILE_LOG("[TILEG] member group=%s id=%s path=%s x=%ld y=%ld tick=%lu generation=%lu\n",group->id,tile->id,tile->definitionPath,
                    (long)group->layout.members[j].x,(long)group->layout.members[j].y,(unsigned long)now,(unsigned long)service->generation);
            }
            group->traced=TRUE;group->tracedLeft=group->left;group->tracedTop=group->top;
        }
    }
    TileInputSetPanels(service->input,panels,panelCount);
}
static ULONG service_work(struct TileService *service, ULONG now)
{
    ULONG delay=HEARTBEAT_INTERVAL;
    unsigned i;
    if(service->stopping)return 20;
    if(service->lift && !lift_screen_front(service))drop_tiles(service);
    TileSourcesExpire(&service->sources,now,source_result,service);
    for (i=0; i<service->count; ++i) {
        struct TileEntry *tile=&service->tiles[i];
        ULONG elapsed;
        if (!tile->active || !tile->valid) continue;
        if(tile->publishPending && tile->live && tile->healthy && tile->icon &&
           !TileSourcePending(&service->sources,tile)) {
            ULONG elapsed=now-tile->lastPublishAttempt;
            if(elapsed>=DRAW_INTERVAL)publish_tile_cache(tile,now);
            else if(DRAW_INTERVAL-elapsed<delay)delay=DRAW_INTERVAL-elapsed;
        }
        if (!tile->config.env[0] && tile->config.templateKind!=TILE_EYES) {
            ULONG interval=tile->config.every>2147483UL ? 2147483000UL : tile->config.every*1000;
            elapsed=now-tile->lastPoll;
            if (elapsed>=interval) request_value(service,tile,now);
            else if (interval-elapsed<delay) delay=interval-elapsed;
        }
        if (tile->icon && tile->live && tile->healthy && !TileSourcePending(&service->sources,tile)) {
            elapsed=now-tile->lastHeartbeat;
            if (elapsed>=HEARTBEAT_INTERVAL) {
                struct AppIconPublish publish={tile->icon,NULL};
                struct TagItem tags[]={{WBCTRLA_PublishAppIcon,(IPTR)&publish},{TAG_DONE,0}};
                WorkbenchControlA(NULL,tags); tile->lastHeartbeat=now;
            } else if (HEARTBEAT_INTERVAL-elapsed<delay) delay=HEARTBEAT_INTERVAL-elapsed;
        }
    }
    if(service->lift) {
        for(i=0;i<service->count;++i) {
            struct TileEntry *tile=&service->tiles[i];
            struct BitMap *bitmap=tile->healthy&&tile->bitmap?tile->bitmap:tile->staticBitmap;
            if(bitmap && TileLiftUpdateBitmap(service->lift,i+1,bitmap))TileLiftRefreshItem(service->lift,i+1);
        }
    }
    unsigned first=service->cursor;
    for (i=0; i<service->count; ++i) {
        unsigned index=(first+i)%service->count;
        struct TileEntry *tile=&service->tiles[index];
        ULONG flags=presentation(service,tile);
        BOOL fast=tile->live && tile->config.hoverCadence==33 &&
            (flags & (APPICONQF_VISIBLE|APPICONQF_HOVERED))==(APPICONQF_VISIBLE|APPICONQF_HOVERED);
        if (tile->dirty && tile->healthy && !TileSourcePending(&service->sources,tile) && (flags & APPICONQF_VISIBLE)) {
            ULONG interval=fast?FAST_INTERVAL:DRAW_INTERVAL;
            ULONG elapsed=now-(fast?service->lastFastDraw:service->lastDraw);
            if (tile->live && now-tile->lastFrame<elapsed) elapsed=now-tile->lastFrame;
            if (elapsed>=interval) {
                int drawn=draw_tile(service,tile,now,fast);
                if (drawn<0) { tile->healthy=FALSE; problem(tile,"drawing unavailable"); }
                service->cursor=(index+1)%service->count;
                if (FAST_INTERVAL<delay) delay=FAST_INTERVAL;
            } else if (interval-elapsed<delay) delay=interval-elapsed;
            /* Scan remaining candidates: a normal tile waiting for its slot
             * must not prevent an eligible hovered tile from using fast mode. */
            now=now_ms();
        }
    }
    group_work(service,now_ms());
    if(service->lift && service->snapshot)
        for(unsigned n=0;n<service->snapshotCount;++n)
            TileLiftUpdateBitmap(service->lift,TILE_LIMIT+1+n,
                snapshot_lift_bitmap(service,&service->snapshotEntries[n]));
    /* Refresh the owned snapshot after rendering/publishing, so an opted-in
     * hovered tile also stays responsive in the global lift. Normal snapshots
     * retain their coalesced cadence; copying caches never renders a tile. */
    ULONG snapshotInterval=DRAW_INTERVAL;
    if(service->lift && service->snapshot) {
        for(i=0;i<service->count;++i) {
            struct TileEntry *tile=&service->tiles[i];
            if(tile->active && tile->config.hoverCadence==FAST_INTERVAL &&
                (presentation(service,tile)&(APPICONQF_VISIBLE|APPICONQF_HOVERED))==
                    (APPICONQF_VISIBLE|APPICONQF_HOVERED))snapshotInterval=FAST_INTERVAL;
        }
        now=now_ms();
        BOOL refresh=FALSE;
        /* Foreign owners retain the normal copy cadence, even when our own
         * lifted caches are using the separately budgeted hover renderer. */
        if((ULONG)(now-service->snapshotTick)>=DRAW_INTERVAL) {
            struct TagItem tags[]={{WBCTRLA_RefreshAppIconSnapshot,(IPTR)service->snapshot},{TAG_DONE,0}};
            service->snapshotTick=now;
            if(!WorkbenchControlA(NULL,tags))drop_tiles(service);
            else refresh=TRUE;
        }
        if(service->lift && (refresh || (ULONG)(now-service->liftRefreshTick)>=snapshotInterval)) {
            service->liftRefreshTick=now;
            if(service->snapshotCount)TileLiftRefreshItem(service->lift,TILE_LIMIT+1);
        }
        if(snapshotInterval<delay)delay=snapshotInterval;
    }
    for(unsigned g=0;g<service->groupCount;++g)if(service->groups[g].config.show==TILE_GROUP_EDGE && delay>FAST_INTERVAL)delay=FAST_INTERVAL;
    return TileSourcesNext(&service->sources,now,delay ? delay : 1);
}
static void release_screen(struct TileService *service)
{
    drop_tiles(service);close_panels(service);
    if(service->input){TileInputClose(service->input);service->input=NULL;}
    for(unsigned g=0;g<service->groupCount;++g) {
        struct ServiceGroup *group=&service->groups[g];
        if(group->icon){RemoveAppIcon(group->icon);group->icon=NULL;}
        if(group->bitmap){FreeBitMap(group->bitmap);group->bitmap=NULL;}
        group->dirty=TRUE;group->traced=FALSE;
    }
    for(unsigned t=0;t<service->count;++t) {
        struct TileEntry *tile=&service->tiles[t];
        if(tile->icon){RemoveAppIcon(tile->icon);tile->icon=NULL;}
        if(tile->bitmap){FreeBitMap(tile->bitmap);tile->bitmap=NULL;}
        if(tile->staticBitmap){FreeBitMap(tile->staticBitmap);tile->staticBitmap=NULL;}
        tile->live=FALSE;tile->dirty=TRUE;
    }
    discard_app_messages(service);
    if(service->screen){UnlockPubScreen(NULL,service->screen);service->screen=NULL;}
}
static void screen_messages(struct TileService *service)
{
    struct ScreenNotifyMessage *message;
    while((message=(struct ScreenNotifyMessage *)GetMsg(service->screenPort))) {
        if(message->snm_Class==SNOTIFY_BEFORE_CLOSEWB)release_screen(service);
        else if(message->snm_Class==SNOTIFY_AFTER_OPENWB && !service->stopping) {
            service->screen=LockPubScreen(NULL);
            if(service->screen)service->reloadPending=TRUE;
        }
        ReplyMsg((struct Message *)message);
    }
}
/* A WAIT_REPLY notifier can hold the subscription lock until our reply.
 * FALSE preserves both handle and port for a later event-loop retry. */
static BOOL unsubscribe_screen(struct TileService *service)
{
    if(service->screenPort)screen_messages(service);
    if(service->screenNotify) {
        if(!EndScreenNotify(service->screenNotify))return FALSE;
        service->screenNotify=NULL;
    }
    if(service->screenPort)screen_messages(service);
    return TRUE;
}
static void stop_timer(struct TileService *service)
{
    if (service->timerPending) {
        if (!CheckIO((struct IORequest *)service->timer)) AbortIO((struct IORequest *)service->timer);
        WaitIO((struct IORequest *)service->timer); service->timerPending=FALSE;
        /* AbortIO can reply after Wait cleared the mask. WaitIO removes an
         * already completed reply without consuming its signal bit. */
        SetSignal(0,1UL<<service->timerPort->mp_SigBit);
    }
}
/* Async SystemTags transfers these handles to the child. Never lend the
 * resident service's streams: an already-owned clock port makes the child
 * exit immediately, and its shell then closes its streams. */
static BOOL start_clock_source(void)
{
    BPTR input=Open("NIL:",MODE_OLDFILE);
    if(!input)return FALSE;
    BPTR output=Open("NIL:",MODE_NEWFILE);
    if(!output) { Close(input);return FALSE; }
    if(SystemTags("SYS:System/TilesControlSource",SYS_Asynch,TRUE,
        SYS_Input,input,SYS_Output,output,TAG_DONE)==-1) {
        Close(output);Close(input);return FALSE;
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    struct TileService *service=AllocVec(sizeof(*service),MEMF_CLEAR);
    int result=RETURN_FAIL, i;
    if (!service) return result;
    service->argc=argc;service->argv=argv;
    RexxSysBase=(struct RxsLib *)OpenLibrary("rexxsyslib.library",0);
    if (!RexxSysBase) goto cleanup;
    CommoditiesBase=OpenLibrary("commodities.library",0);
    if(!CommoditiesBase)goto cleanup;
    service->screenPort=CreateMsgPort();
    service->apps=CreateMsgPort(); service->commands=CreateMsgPort();
    service->notifications=CreateMsgPort(); service->timerPort=CreateMsgPort();
    if (!service->screenPort || !service->apps || !service->commands || !service->notifications || !service->timerPort) goto cleanup;
    if (!TileSourcesInit(&service->sources)) goto cleanup;
    service->timer=(struct timerequest *)CreateIORequest(service->timerPort,sizeof(struct timerequest));
    if (!service->timer || OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest *)service->timer,0)) goto cleanup;
    service->timerOpen=TRUE; TimerBase=service->timer->tr_node.io_Device;
    service->screen=LockPubScreen(NULL); if (!service->screen) goto cleanup;
    service->commands->mp_Node.ln_Name="TILES";
    Forbid();
    if (FindPort("TILES")) { Permit(); service->commands->mp_Node.ln_Name=NULL; goto cleanup; }
    AddPort(service->commands); Permit();
    service->lastDraw=now_ms()-DRAW_INTERVAL; service->lastFastDraw=now_ms()-FAST_INTERVAL;
    for (i=1;i<argc;++i) if (!strcmp(argv[i],"TRACE")) service->trace=TRUE;
    if(argc==1 || (argc==2 && service->trace)) {
        BPTR control=Lock("HOME:Tiles/Control",ACCESS_READ);
        if(control)UnLock(control);
        else SystemTags("SYS:System/TilesControl",SYS_Asynch,FALSE,TAG_DONE);
    }
    Forbid();BOOL clockReady=FindPort("INDICATOR.clock")!=NULL;Permit();
    if(!clockReady)start_clock_source();
    service->screenNotify=StartScreenNotifyTags(SNA_Notify,SNOTIFY_WAIT_REPLY|SNOTIFY_BEFORE_CLOSEWB|SNOTIFY_AFTER_OPENWB,
        SNA_MsgPort,service->screenPort,SNA_Priority,0,TAG_DONE);
    if(!service->screenNotify)goto cleanup;
    reload_catalog(service);
    service->catalogNotify.nr_Name="HOME:Tiles";
    service->catalogNotify.nr_Flags=NRF_SEND_MESSAGE|NRF_WAIT_REPLY;
    service->catalogNotify.nr_stuff.nr_Msg.nr_Port=service->notifications;
    service->catalogWatching=StartNotify(&service->catalogNotify);
    BPTR notifyDir=CreateDir("ENV:Tiles");if(notifyDir)UnLock(notifyDir);
    service->changeNotify.nr_Name="ENV:Tiles/Changed";
    service->changeNotify.nr_Flags=NRF_SEND_MESSAGE|NRF_WAIT_REPLY;
    service->changeNotify.nr_stuff.nr_Msg.nr_Port=service->notifications;
    service->changeWatching=StartNotify(&service->changeNotify);
    if (service->trace) TILE_LOG("[TILE2] ready tiles=%u\n",service->count);
running:
    for (;;) {
        ULONG signals,delay;
        TileSourcesCollect(&service->sources,source_result,service);
        screen_messages(service);
        app_messages(service); notifications(service); command_messages(service);
        if(!service->stopping && service->reloadPending && service->screen && now_ms()-service->reloadTick>=250)reload_catalog(service);
        {
            struct TileInputEvent input;
            struct TileLiftEvent event;
            while(TileInputNext(service->input,&input)) {
                if(!lift_screen_front(service)) {drop_tiles(service);close_panels(service);continue;}
                if(input.kind==TILE_INPUT_PANEL_CLICK || input.kind==TILE_INPUT_PANEL_DROP) {
                    if(input.group<service->groupCount && service->groups[input.group].panel) {
                        struct ServiceGroup *group=&service->groups[input.group];
                        if(input.kind==TILE_INPUT_PANEL_CLICK)group_click(service,input.group,input.x-group->left,input.y-group->top);
                        else {TileInputSetPanels(service->input,NULL,0);TileLiftClose(group->panel);group->panel=NULL;}
                    }
                    continue;
                }
                if(input.kind==TILE_INPUT_ALL)lift_tiles(service,-1);
                else if(input.kind==TILE_INPUT_GROUP && input.group<service->groupCount)lift_tiles(service,input.group);
                else if(service->lift && TileLiftInput(service->lift,input.x,input.y,input.kind==TILE_INPUT_DROP,&event)) {
                    if(event.type==TILE_LIFT_DISMISS)drop_tiles(service);
                    else if(event.type==TILE_LIFT_CLICK && event.id && event.id<=service->count)
                        activate_tile(service,&service->tiles[event.id-1],event.x);
                    else if(event.type==TILE_LIFT_CLICK && service->snapshot && event.id>TILE_LIMIT) {
                        struct AppIconSnapshotActivate action={service->snapshot,event.id-TILE_LIMIT-1,event.x,event.y};
                        struct TagItem tags[]={{WBCTRLA_ActivateAppIconSnapshot,(IPTR)&action},{TAG_DONE,0}};
                        WorkbenchControlA(NULL,tags);
                    }
                }
            }
            while(service->lift && TileLiftPump(service->lift,&event)) {}
        }
        if (service->quitRequested && (LONG)(now_ms()-service->cleanupRetry)>=0) break;
        delay=service_work(service,now_ms());
        if((service->lift || service->reloadPending) && delay>FAST_INTERVAL)delay=FAST_INTERVAL;
        ULONG panelSignals=0;
        for(unsigned g=0;g<service->groupCount;++g)panelSignals|=TileLiftSignals(service->groups[g].panel);
        service->timer->tr_node.io_Command=TR_ADDREQUEST;
        service->timer->tr_time.tv_secs=delay/1000; service->timer->tr_time.tv_micro=(delay%1000)*1000;
        SendIO((struct IORequest *)service->timer); service->timerPending=TRUE;
        signals=Wait((1UL<<service->apps->mp_SigBit)|(1UL<<service->commands->mp_SigBit)|
            (1UL<<service->notifications->mp_SigBit)|(1UL<<service->sources.replies->mp_SigBit)|
            (1UL<<service->timerPort->mp_SigBit)|(1UL<<service->screenPort->mp_SigBit)|panelSignals|TileInputSignals(service->input)|TileLiftSignals(service->lift)|SIGBREAKF_CTRL_C);
        ++service->wakes; stop_timer(service);
        if (signals & SIGBREAKF_CTRL_C) service->quitRequested=TRUE;
    }
    result=RETURN_OK;
cleanup:
    service->stopping=TRUE; stop_timer(service);
    release_screen(service);
    /* A notifier can hold the subscription lock while waiting for our reply.
     * Keep its port alive and drain notifications before retrying removal. */
    if(!unsubscribe_screen(service)) {
        service->quitRequested=TRUE;service->cleanupRetry=now_ms()+20;goto running;
    }
    if(service->sources.replies && !TileSourcesClose(&service->sources)) {
        service->quitRequested=TRUE;service->cleanupRetry=now_ms()+20;goto running;
    }
    if(service->catalogWatching)EndNotify(&service->catalogNotify);
    if(service->changeWatching)EndNotify(&service->changeNotify);
    for(unsigned g=0;g<service->groupCount;++g) {
        if(service->groups[g].watching)EndNotify(&service->groups[g].notify);
        if(service->groups[g].fallback)FreeDiskObject(service->groups[g].fallback);
    }
    if (service->commands && service->commands->mp_Node.ln_Name) RemPort(service->commands);
    for (i=0;i<service->count;++i) {
        struct TileEntry *tile=&service->tiles[i];
        if (tile->watching) EndNotify(&tile->notify);
        if (tile->icon) RemoveAppIcon(tile->icon);
        if (tile->bitmap) FreeBitMap(tile->bitmap);
        if (tile->staticBitmap) FreeBitMap(tile->staticBitmap);
        if (tile->fallback) FreeDiskObject(tile->fallback);
    }
    if (service->apps) app_messages(service);
    if (service->notifications) notifications(service);
    if (service->commands) command_messages(service);
    if (service->screen) UnlockPubScreen(NULL,service->screen);
    if (service->timerOpen) CloseDevice((struct IORequest *)service->timer);
    if (service->timer) DeleteIORequest((struct IORequest *)service->timer);
    if (service->timerPort) DeleteMsgPort(service->timerPort);
    if (service->notifications) DeleteMsgPort(service->notifications);
    if (service->commands) DeleteMsgPort(service->commands);
    if (service->apps) DeleteMsgPort(service->apps);
    if (service->screenPort) DeleteMsgPort(service->screenPort);
    if (CommoditiesBase) CloseLibrary(CommoditiesBase);
    if (RexxSysBase) CloseLibrary((struct Library *)RexxSysBase);
    FreeVec(service); return result;
}
