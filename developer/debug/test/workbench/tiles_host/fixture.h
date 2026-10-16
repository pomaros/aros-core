#include <strings.h>
#define Stricmp strcasecmp
/* SPDX-License-Identifier: APL-1.1
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t ULONG;
typedef uint64_t UQUAD;
typedef int64_t QUAD;
typedef int32_t LONG;
typedef uint16_t UWORD;
typedef int16_t WORD;
typedef uintptr_t IPTR;
typedef void *APTR;
typedef const char *CONST_STRPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define WBA_BASE 0x8000a000
#define MEMF_CLEAR 1
#define MEMF_ANY 0
#define BMF_CLEAR 0
#define BMA_WIDTH 0
#define BMA_HEIGHT 1
#define BMA_DEPTH 2
#define ICONDUPA_DuplicateImages 1
#define ICONDUPA_DuplicateImageData 2
#define TAG_DONE 0
#define AMTYPE_APPICON 8
#define AM_VERSION 1
#define WBAPPICONF_PropagatePosition 1
struct Library { int refs; };
struct GfxBase { int unused; };
struct Task { int id; };
struct BitMap { ULONG width, height, depth, pixel; };
struct DiskObject { ULONG pixel; };

#define PA_SIGNAL 0
struct MsgPort { struct Task *mp_SigTask; unsigned mp_Flags; struct Message *messages[128]; unsigned count; };
struct Message { struct MsgPort *mn_ReplyPort; UWORD mn_Length; };
struct AppMessage { struct Message am_Message; UWORD am_Type; IPTR am_UserData, am_ID;
    LONG am_NumArgs; void *am_ArgList; UWORD am_Version, am_Class;
    WORD am_MouseX, am_MouseY; ULONG am_Seconds, am_Micros, am_Reserved[8]; };
struct Device { int unused; };
struct IORequest { struct Message io_Message; struct Device *io_Device; };
struct timerequest { struct IORequest tr_node; };
struct EClockVal { ULONG ev_hi, ev_lo; };
#define TIMERNAME "timer.device"
#define UNIT_ECLOCK 2
struct TagItem { ULONG ti_Tag; IPTR ti_Data; };
struct RastPort { ULONG pixel; struct BitMap *BitMap; };
struct Rectangle { LONG MinX,MinY,MaxX,MaxY; };
struct Layer_Info { int unused; };
struct Layer { struct RastPort *rp; struct RastPort owned; };
#define ICONDRAWA_Frameless 1
#define ICONDRAWA_Borderless 2
#define ICONDRAWA_EraseBackground 3
#define IDS_NORMAL 0
#define LAYERSIMPLE 1
struct AppIcon { struct AppIcon *next; IPTR ai_ID, ai_UserData; struct MsgPort *ai_MsgPort;
    struct DiskObject *ai_DiskObject; ULONG ai_Flags; const char *ai_Text;
    void *ai_RenderHook; struct WBTile *ai_Tile; };
struct WorkbenchBase { ULONG wb_TileFastTick; BOOL wb_TileHaveFastFrame; ULONG wb_TileSerial, wb_TileNotifiedSerial; BOOL wb_TileNotifyPending; APTR wb_TileTaskResource; struct AppIcon *wb_AppIcons; struct MsgPort wb_TileReplyPort; ULONG wb_TileInflight; };
static struct WorkbenchBase base, *WorkbenchBase = &base;
enum { WBNOTIFY_RefreshTiles, WBNOTIFY_AppIcon };
static unsigned refreshMessages;
static BOOL failNotify;
static BOOL NotifyWorkbench(int action, int target, struct WorkbenchBase *wb)
{
    assert(!wb->wb_TileNotifyPending);
    if (failNotify) return FALSE;
    ++refreshMessages;
    return TRUE;
}
static struct Task owner, other, *current = &owner;
static struct Library graphics;
static ULONG now;
static int devices, failGraphics, failDevice;
static int allocations, allocCalls, graphicsCalls, deviceCalls, failAfter = -1, locks;
static void *AllocVec(size_t size, int flags) {
    void *p;
    ++allocCalls;
    if (failAfter == 0) return NULL;
    if (failAfter > 0) --failAfter;
    p = flags & MEMF_CLEAR ? calloc(1,size) : malloc(size);
    if (p) ++allocations;
    return p;
}
static void FreeVec(void *p) { if (p) { --allocations; free(p); } }
static struct Library *OpenLibrary(const char *name, int version) { ++graphicsCalls; if (failGraphics) return NULL; ++graphics.refs; return &graphics; }
static void CloseLibrary(struct Library *lib) { --lib->refs; }
static int OpenDevice(const char *name, int unit, struct IORequest *request, int flags) {
    static struct Device device;
    ++deviceCalls;
    if (failDevice) return -1;
    request->io_Device=&device;
    ++devices;
    return 0;
}
static void CloseDevice(struct IORequest *request) { --devices; }
static ULONG ReadEClock(struct EClockVal *clock) {
    UQUAD ticks=(UQUAD)now*24000;
    clock->ev_hi=ticks>>32; clock->ev_lo=ticks;
    return 24000000;
}
static struct Task *FindTask(void *name) { return current; }
static IPTR GetTagData(ULONG tag, IPTR def, struct TagItem *tags) {
    for (; tags && tags->ti_Tag; ++tags) if (tags->ti_Tag == tag) return tags->ti_Data;
    return def;
}
static ULONG GetBitMapAttr(struct BitMap *b, ULONG attr) {
    return attr == BMA_WIDTH ? b->width : attr == BMA_HEIGHT ? b->height : b->depth;
}
static struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG d, ULONG flags, struct BitMap *friend) {
    struct BitMap *b = AllocVec(sizeof(*b), MEMF_CLEAR);
    if (b) { b->width=w; b->height=h; b->depth=d; }
    return b;
}
static void FreeBitMap(struct BitMap *b) { FreeVec(b); }
static void BltBitMap(struct BitMap *src, int sx, int sy, struct BitMap *dst,
    int dx, int dy, ULONG w, ULONG h, int minterm, int mask, void *tmp) { dst->pixel = src->pixel; }
static void BltBitMapRastPort(struct BitMap *src, int sx, int sy, struct RastPort *dst,
    int dx, int dy, ULONG w, ULONG h, int minterm) { dst->pixel = src->pixel; }
static void WaitBlit(void) {}
static struct DiskObject *DupDiskObjectA(struct DiskObject *src, struct TagItem *tags) {
    struct DiskObject *copy = AllocVec(sizeof(*copy), MEMF_CLEAR);
    if (copy) *copy = *src;
    return copy;
}
static void FreeDiskObject(struct DiskObject *icon) { FreeVec(icon); }
static struct Layer_Info *NewLayerInfo(void) { assert(!locks);return AllocVec(sizeof(struct Layer_Info),MEMF_CLEAR); }
static struct Layer *CreateUpfrontLayer(struct Layer_Info *info,struct BitMap *bitmap,LONG x,LONG y,LONG right,LONG bottom,ULONG flags,void *back) {
    assert(!locks&&right==(LONG)bitmap->width-1&&bottom==(LONG)bitmap->height-1);
    struct Layer *layer=AllocVec(sizeof(*layer),MEMF_CLEAR);
    if(layer){layer->rp=&layer->owned;layer->rp->BitMap=bitmap;}return layer;
}
static void DeleteLayer(ULONG unused,struct Layer *layer){assert(!locks);FreeVec(layer);}
static void DisposeLayerInfo(struct Layer_Info *info){FreeVec(info);}
static BOOL GetIconRectangleA(struct RastPort *rp,struct DiskObject *icon,void *label,struct Rectangle *rect,struct TagItem *tags){
    assert(!locks);*rect=(struct Rectangle){0,0,31,31};return TRUE;
}
static void DrawIconStateA(struct RastPort *rp,struct DiskObject *icon,void *label,LONG x,LONG y,ULONG state,struct TagItem *tags){
    assert(!locks);rp->BitMap->pixel=icon->pixel;
}
static void PutMsg(struct MsgPort *port, struct Message *msg) {
    assert(port->count < 128);
    if (port != &base.wb_TileReplyPort) assert(locks > 0);
    port->messages[port->count++] = msg;
}
static struct Message *GetMsg(struct MsgPort *port) {
    if (!port->count) return NULL;
    struct Message *message=port->messages[0];
    --port->count;
    memmove(port->messages,port->messages+1,port->count*sizeof(port->messages[0]));
    return message;
}
#define LockWorkbench() (++locks)
#define UnlockWorkbench() (--locks)
#define LB(x) (x)
#define ForeachNode(list, icon) for ((icon) = *(list); (icon); (icon) = (icon)->next)
static struct AppIcon *FindAppIconByPtr(struct AppIcon *ptr) {
    struct AppIcon *i;
    for (i=base.wb_AppIcons; i; i=i->next) if (i==ptr) return i;
    return NULL;
}
