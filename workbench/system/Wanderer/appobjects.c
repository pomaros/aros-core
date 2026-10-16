/*
    Copyright (C) 2011, The AROS Development Team. All rights reserved.
*/

#include "appobjects.h"

#include <proto/workbench.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/utility.h>
#include <aros/symbolsets.h>

/*
 * Wanderer needs access to internals of workbench.library to provided needed functionality.
 * In general, "workbench replacements" needs to provide their own workbench.library or patch the
 * existing one. All Wanderer code should treat the internals of workbench.library as READ-ONLY.
 * Only this compilation unit may access the workbench.library internals.
 */

#include "../../libs/workbench/workbench_intern.h"
#include "../../libs/workbench/tile.h"
#include <proto/graphics.h>
#include <string.h>


static struct MsgPort * replyport = NULL;

/*
 * Assumes lock in already acquired. Returns pointer to AppIcon from workbench.library list, if the passed
 * pointer is actually on the list.
 */
STATIC struct AppIcon * FindAppIconByPtr(struct AppIcon * appicon)
{
    struct AppIcon * entry = NULL;
    ForeachNode(&LB(WorkbenchBase)->wb_AppIcons, entry)
    {
        if (entry == appicon)
            return entry;
    }

    return NULL;
}

BOOL SendAppIconMessage(struct AppIcon * appicon, LONG numargs, STRPTR args)
{
    BOOL success = FALSE;

    if (!appicon)
        return success;

    if ((numargs == 0) && (args == NULL))
        return SendAppIconMenuMessage(appicon, AMCLASSICON_Open);

    /* TODO: Handle the case when icons are dropped on appicon */
    return success;
}

BOOL SendAppIconMenuMessage(struct AppIcon * appicon, UWORD class)
{
    struct AppMessage * msg = NULL;
    BOOL success = FALSE;
    struct AppIcon * entry = NULL;

    if (!appicon)
        return success;

    LockWorkbench();

    /* Check is passed entry is valid */
    if ((entry = FindAppIconByPtr(appicon)) != NULL)
    {
        struct MsgPort *port = entry->ai_MsgPort;
        if (entry->ai_Tile)
        {
            success = AppIcon_SendTileMessage(entry, class, 0, 0, NULL);
            UnlockWorkbench();
            return success;
        }

        msg = AllocVec(sizeof(struct AppMessage), MEMF_CLEAR);

        if (msg)
        {
            msg->am_Type = AMTYPE_APPICON;
            msg->am_ArgList = NULL;
            msg->am_NumArgs = 0;
            msg->am_ID = entry->ai_ID;
            msg->am_UserData = entry->ai_UserData;
            msg->am_Version = AM_VERSION;
            msg->am_Class = class;

            msg->am_Message.mn_ReplyPort = replyport;
            PutMsg(port, (struct Message *) msg);
            success = TRUE;
        }
    }

    UnlockWorkbench();

    return success;
}

VOID CleanAppIconReplyMessages()
{
    struct Message * msg = NULL;

    while ((msg = GetMsg(replyport))) FreeVec(msg);
}

APTR AppObjectsLock()
{
    static LONG lock = 1;

    LockWorkbench();

    return (APTR)&lock;
}

VOID AppObjectsUnlock(APTR lock)
{
    UnlockWorkbench();
}

/* Assumes lock is already held */
struct AppIcon * GetNextAppIconLocked(struct AppIcon * lastappicon, APTR lock)
{
    struct List *appiconlist = &LB(WorkbenchBase)->wb_AppIcons;

    /* return NULL if list empty or argument dob is already from last appicon in list*/
    if (!IsListEmpty(appiconlist))
    {
        struct AppIcon * _return = NULL;

        /* return NULL if last entry reached */
        if (lastappicon == (struct AppIcon *)appiconlist->lh_TailPred)
            return NULL;

        _return = FindAppIconByPtr(lastappicon);

        if (_return)
        {
            /* Return next */
            return (struct AppIcon *)GetSucc((struct Node *)_return);
        }
        else
        {
            /* Return first */
            return (struct AppIcon *)appiconlist->lh_Head;
        }
    }

    return NULL;
}

struct DiskObject * AppIcon_GetDiskObject(struct AppIcon * appicon)
{
    struct AppIcon * entry = NULL;
    struct DiskObject * _return = NULL;

    LockWorkbench();

    entry = FindAppIconByPtr(appicon);

    if (entry)
        _return = entry->ai_DiskObject;

    UnlockWorkbench();

    return _return;
}

CONST_STRPTR AppIcon_GetLabel(struct AppIcon * appicon)
{
    struct AppIcon * entry = NULL;
    CONST_STRPTR _return = NULL;

    LockWorkbench();

    entry = FindAppIconByPtr(appicon);

    if (entry)
        _return = entry->ai_Text;

    UnlockWorkbench();

    return _return;
}

BOOL AppIcon_Supports(struct AppIcon * appicon, ULONG tag)
{
    BOOL _return = FALSE;
    LockWorkbench();
    appicon = FindAppIconByPtr(appicon);
    if (!appicon) {
        UnlockWorkbench();
        return FALSE;
    }
    switch(tag)
    {
    case(WBAPPICONA_SupportsOpen):          _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsOpen); break;
    case(WBAPPICONA_SupportsCopy):          _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsCopy); break;
    case(WBAPPICONA_SupportsRename):        _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsRename); break;
    case(WBAPPICONA_SupportsInformation):   _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsInformation); break;
    case(WBAPPICONA_SupportsSnapshot):      _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsSnapshot); break;
    case(WBAPPICONA_SupportsUnSnapshot):    _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsUnSnapshot); break;
    case(WBAPPICONA_SupportsLeaveOut):      _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsLeaveOut); break;
    case(WBAPPICONA_SupportsPutAway):       _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsPutAway); break;
    case(WBAPPICONA_SupportsDelete):        _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsDelete); break;
    case(WBAPPICONA_SupportsFormatDisk):    _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsFormatDisk); break;
    case(WBAPPICONA_SupportsEmptyTrash):    _return = !!(appicon->ai_Flags & WBAPPICONF_SupportsEmptyTrash); break;
    case(WBAPPICONA_PropagatePosition):     _return = !!(appicon->ai_Flags & WBAPPICONF_PropagatePosition); break;
    case(WBAPPICONA_RenderHook):            _return = (appicon->ai_RenderHook != NULL); break;
    }
    UnlockWorkbench();
    return _return;
}

BOOL AppIcon_CallRenderHook(struct AppIcon * appicon, struct AppIconRenderMsg * msg)
{
    struct AppIcon * entry = NULL;
    BOOL called = FALSE;

    LockWorkbench();
    entry = FindAppIconByPtr(appicon);
    if (entry && entry->ai_RenderHook) {
        CallHookA(entry->ai_RenderHook, NULL, msg);
        called = TRUE;
    }
    UnlockWorkbench();
    return called;
}

LONG AppObjects_Init()
{
    replyport = CreateMsgPort();

    return 1;
}

ADD2INIT(AppObjects_Init, 0);

/* Called with the Workbench registry mutex held; the task-exit hook and
 * explicit detach invalidate the port under that same mutex before teardown. */
BOOL AppIcon_SendTileMessage(struct AppIcon *icon, UWORD class, LONG x, LONG y,
                             const char *region)
{
    struct WorkbenchBase *wb = LB(WorkbenchBase);
    WBTileCollectReplies(wb);
    return WBTileSendMessage(icon, class, x, y, region, wb) != NULL;
}

/* All tile reads stay under the registration lock, including the blit. */
BOOL AppIcon_TileSize(struct AppIcon *icon, ULONG *width, ULONG *height)
{
    BOOL result = FALSE;
    LockWorkbench();
    icon = FindAppIconByPtr(icon);
    if (icon && icon->ai_Tile) {
        *width = icon->ai_Tile->state.width;
        *height = icon->ai_Tile->state.height;
        result = TRUE;
    }
    UnlockWorkbench();
    return result;
}

BOOL AppIcon_DrawTile(struct AppIcon *icon, struct RastPort *rp, LONG x, LONG y)
{
    BOOL result = FALSE;
    LockWorkbench();
    icon = FindAppIconByPtr(icon);
    if (icon && icon->ai_Tile && WBTileStateFresh(&icon->ai_Tile->state, WBTileClock(&icon->ai_Tile->timer))) {
        struct WBTile *tile = icon->ai_Tile;
        BltBitMapRastPort(tile->bitmap, 0, 0, rp, x, y,
                         tile->state.width, tile->state.height, 0xc0);
        WaitBlit();
        result = TRUE;
    }
    UnlockWorkbench();
    return result;
}

/* A hit query keeps SELECTUP armed for rapid clicks on a live region. */
BOOL AppIcon_TileHit(struct AppIcon *icon, LONG x, LONG y)
{
    BOOL result = FALSE;
    LockWorkbench();
    icon = FindAppIconByPtr(icon);
    if (icon && icon->ai_Tile) {
        struct WBTile *tile = icon->ai_Tile;
        result = WBTileStateFresh(&tile->state, WBTileClock(&tile->timer)) &&
                 WBTileStateHit(&tile->state, x, y) >= 0;
    }
    UnlockWorkbench();
    return result;
}

BOOL AppIcon_TileClick(struct AppIcon *icon, LONG x, LONG y)
{
    BOOL result = FALSE;
    LockWorkbench();
    icon = FindAppIconByPtr(icon);
    if (icon && icon->ai_Tile) {
        struct WBTile *tile = icon->ai_Tile;
        LONG region = WBTileStateHit(&tile->state, x, y);
        if (region >= 0 && WBTileStateFresh(&tile->state, WBTileClock(&tile->timer)))
            result = AppIcon_SendTileMessage(icon, AMCLASSICON_Region, x, y,
                         tile->state.regions.air_Regions[region].air_Name);
    }
    UnlockWorkbench();
    return result;
}

/* Visibility observations remain authoritative when the bounded message pool
 * is full. The owner also polls QueryAppIcon; missed messages allocate nothing. */
BOOL AppIcon_TileVisibility(struct AppIcon *icon, BOOL visible)
{
    BOOL result = FALSE;
    LockWorkbench();
    icon = FindAppIconByPtr(icon);
    if (icon && icon->ai_Tile) {
        struct WBTile *tile = icon->ai_Tile;
        BOOL changed = tile->visible != !!visible;
        tile->visible = !!visible;
        if (!visible) { tile->hovered = FALSE; tile->geometryValid = FALSE; }
        tile->visibilityTick = WBTileClock(&tile->timer);
        if (changed)
            AppIcon_SendTileMessage(icon, AMCLASSICON_Visibility, !!visible, 0, NULL);
        result = TRUE;
    }
    WBTileFlushPointers(LB(WorkbenchBase));
    UnlockWorkbench();
    return result;
}

/* Geometry is copied under the registry lock; no window or screen pointer
 * crosses the presenter boundary. The full surface rectangle anchors panels,
 * while the validity bit means at least one part is currently presented. */
/* Placement survives occlusion, but not hidden/list/nonfront presentation.
 * Screen is an identity only: callers hold its lifetime, no screen dereference
 * occurs under the Workbench registry lock. */
BOOL AppIcon_TilePlacement(struct AppIcon *icon, struct Screen *screen, BOOL valid,
                           LONG left, LONG top, ULONG width, ULONG height)
{
    BOOL result=FALSE;
    LockWorkbench();
    icon=FindAppIconByPtr(icon);
    if (icon && icon->ai_Tile) {
        struct WBTile *tile=icon->ai_Tile;
        valid=valid && screen && width==tile->state.width && height==tile->state.height;
        tile->placementValid=!!valid;
        tile->placementScreen=valid?screen:NULL;
        tile->placementLeft=valid?left:0;tile->placementTop=valid?top:0;
        tile->placementTick=WBTileClock(&tile->timer);
        result=TRUE;
    }
    UnlockWorkbench();return result;
}

BOOL AppIcon_TileGeometry(struct AppIcon *icon, BOOL valid, LONG left, LONG top,
                          ULONG width, ULONG height)
{
    BOOL result = FALSE;
    LockWorkbench();
    icon = FindAppIconByPtr(icon);
    if (icon && icon->ai_Tile) {
        struct WBTile *tile = icon->ai_Tile;
        valid = valid && width == tile->state.width && height == tile->state.height;
        tile->geometryValid = !!valid;
        tile->geometryLeft = valid ? left : 0;
        tile->geometryTop = valid ? top : 0;
        tile->geometryWidth = valid ? width : 0;
        tile->geometryHeight = valid ? height : 0;
        tile->geometryTick = WBTileClock(&tile->timer);
        result = TRUE;
    }
    UnlockWorkbench();
    return result;
}

BOOL AppIcon_TilePointer(struct AppIcon *icon, BOOL inside, LONG x, LONG y)
{
    BOOL result = FALSE;
    LockWorkbench();
    WBTileCollectReplies(LB(WorkbenchBase));
    icon = FindAppIconByPtr(icon);
    if (icon && icon->ai_Tile && icon->ai_Tile->pointerEnabled) {
        struct WBTile *tile = icon->ai_Tile;
        ULONG now = WBTileClock(&tile->timer);
        tile->hovered = inside && x >= 0 && y >= 0 &&
            x < (LONG)tile->state.width && y < (LONG)tile->state.height && tile->visible &&
            WBTileStateFresh(&tile->state, now);
        tile->pointerX = x; tile->pointerY = y; tile->pointerTick = now;
        result = TRUE;
    }
    WBTileFlushPointers(LB(WorkbenchBase));
    UnlockWorkbench();
    return result;
}

BOOL AppIcon_TilesChanged(void)
{
    struct AppIcon *icon;
    BOOL changed = FALSE;
    ULONG now;
    LockWorkbench();
    ForeachNode(&LB(WorkbenchBase)->wb_AppIcons, icon) {
        struct WBTile *tile = icon->ai_Tile;
        if (!tile) continue;
        now = WBTileClock(&tile->timer);
        /* Reserve two refresh intervals plus drawing time. A delayed task is
         * not a hard real-time guarantee; guest tests measure completed blits. */
        if (tile->state.fresh && (ULONG)(now - tile->state.lastAlive) >= 4250) {
            tile->state.fresh = FALSE;
            tile->dirty = TRUE;
        }
        if (tile->dirty) {
            tile->dirty = FALSE;
            changed = TRUE;
        }
    }
    WBTileCollectReplies(LB(WorkbenchBase));
    WBTileFlushPointers(LB(WorkbenchBase));
    UnlockWorkbench();
    return changed;
}

/* Copy metadata under the registry lock; no borrowed owner or icon strings. */
BOOL AppIcon_CopyTileDefinition(struct AppIcon *icon,char *path,ULONG capacity)
{
    BOOL result=FALSE;
    if(!path || !capacity)return FALSE;
    path[0]=0;LockWorkbench();icon=FindAppIconByPtr(icon);
    if(icon && icon->ai_Tile && icon->ai_Tile->definitionPath[0]) {
        ULONG n=strlen(icon->ai_Tile->definitionPath);
        if(n<capacity){memcpy(path,icon->ai_Tile->definitionPath,n+1);result=TRUE;}
    }
    UnlockWorkbench();return result;
}
BOOL AppIcon_TileAnchor(struct AppIcon *icon,ULONG *anchor)
{
    BOOL result=FALSE;if(!anchor)return FALSE;
    *anchor=0;LockWorkbench();icon=FindAppIconByPtr(icon);
    if(icon && icon->ai_Tile && icon->ai_Tile->group) {
        *anchor=icon->ai_Tile->groupAnchor;result=TRUE;
    }
    UnlockWorkbench();return result;
}

/* Both paths are canonical volume paths: the service resolves the definition's
 * parent with NameFromLock, and ParseBackdrop resolves its file/.info lock.
 * Keep the comparison inside the registry lock: no owner/string escapes it.
 * Freshness is deliberately irrelevant; a registered fallback is a presenter. */
BOOL AppIcon_TileDefinitionRegistered(const char *canonicalPath)
{
    struct AppIcon *icon;BOOL found=FALSE;
    if(!canonicalPath || !canonicalPath[0])return FALSE;
    LockWorkbench();
    ForeachNode(&LB(WorkbenchBase)->wb_AppIcons,icon) {
        if(icon->ai_Tile && icon->ai_Tile->definitionPath[0] &&
           !Stricmp(icon->ai_Tile->definitionPath,canonicalPath)) {found=TRUE;break;}
    }
    UnlockWorkbench();return found;
}
