/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include "input.h"
#include <intuition/screens.h>
#include <devices/inputevent.h>
#include <libraries/commodities.h>
#include <proto/commodities.h>
#include <proto/exec.h>
#include <proto/arossupport.h>
#include <string.h>
/* AROS commodities_inputhandler.c serializes callbacks with cx_SignalSemaphore;
 * RemoveCxObj (called by DeleteCxObjAll) acquires that same semaphore, so Close
 * joins the callback before releasing its signal/screen lifetime. The ring is
 * single producer/single consumer; acquire/release also covers SMP. */
#define QUEUE_SIZE 16u
#define GROUP_LIMIT 40u
struct TileInput {
    CxObj *broker;
    struct Screen *screen;
    struct Task *task;
    BYTE signal;
    IX all,escape,groups[GROUP_LIMIT];
    unsigned enabled[GROUP_LIMIT],count;
    unsigned head,tail,lifted;
    unsigned char swallowed[128];
    unsigned mouseButtons;
    unsigned panelSequence,panelCount;
    struct TileInputPanel panels[8];
    struct TileInputEvent queue[QUEUE_SIZE];
};
static struct TileInput state;
static BOOL disabled_key(const char *key)
{
    static const char none[]="none";
    unsigned i;
    if(!key||!*key)return TRUE;
    for(i=0;i<4;++i) {
        unsigned c=(unsigned char)key[i];
        if(c>='A'&&c<='Z')c+=32;
        if(c!=(unsigned char)none[i])return FALSE;
    }
    return key[4]==0;
}
static void enqueue_at(enum TileInputKind kind,unsigned group,LONG x,LONG y)
{
    unsigned head=__atomic_load_n(&state.head,__ATOMIC_RELAXED);
    unsigned tail=__atomic_load_n(&state.tail,__ATOMIC_ACQUIRE);
    if(head-tail<QUEUE_SIZE) {
        struct TileInputEvent *event=&state.queue[head%QUEUE_SIZE];
        event->kind=kind;event->group=group;
        event->x=x;event->y=y;
        __atomic_store_n(&state.head,head+1,__ATOMIC_RELEASE);
        Signal(state.task,1UL<<state.signal);
    }
}
static void enqueue(enum TileInputKind kind,unsigned group)
{ enqueue_at(kind,group,state.screen->MouseX,state.screen->MouseY); }
/* All fields use atomic access: a failed snapshot is forwarded, never spun on.
 * Sequential consistency keeps the version checks around the field observations. */
static BOOL panel_hit(LONG x,LONG y,unsigned *group)
{
    unsigned sequence=__atomic_load_n(&state.panelSequence,__ATOMIC_SEQ_CST);
    if(sequence&1)return FALSE;
    unsigned count=__atomic_load_n(&state.panelCount,__ATOMIC_SEQ_CST);
    BOOL hit=FALSE;unsigned selected=0;
    for(unsigned n=0;n<count && n<8;++n) {
        struct TileInputPanel *p=&state.panels[n];
        LONG left=__atomic_load_n(&p->left,__ATOMIC_SEQ_CST),top=__atomic_load_n(&p->top,__ATOMIC_SEQ_CST);
        ULONG width=__atomic_load_n(&p->width,__ATOMIC_SEQ_CST),height=__atomic_load_n(&p->height,__ATOMIC_SEQ_CST);
        unsigned id=__atomic_load_n(&p->group,__ATOMIC_SEQ_CST);
        if(x>=left && y>=top && x-left<(LONG)width && y-top<(LONG)height){hit=TRUE;selected=id;}
    }
    if(sequence!=__atomic_load_n(&state.panelSequence,__ATOMIC_SEQ_CST))return FALSE;
    if(hit)*group=selected;
    return hit;
}
static void input_action(CxMsg *message,CxObj *object)
{
    struct InputEvent *event;unsigned i;BOOL consume=FALSE;
    unsigned lifted=__atomic_load_n(&state.lifted,__ATOMIC_ACQUIRE);
    (void)object;
    if(CxMsgType(message)!=CXM_IEVENT)return;
    event=(struct InputEvent *)CxMsgData(message);
    if(!event)return;
    if(event->ie_Class==IECLASS_RAWKEY) {
        unsigned code=event->ie_Code&~IECODE_UP_PREFIX;
        if(code<128 && (event->ie_Code&IECODE_UP_PREFIX)) {
            consume=state.swallowed[code];state.swallowed[code]=0;
        } else if(!(event->ie_Qualifier&IEQUALIFIER_REPEAT)) {
            if(MatchIX(event,&state.all)){enqueue(TILE_INPUT_ALL,0);consume=TRUE;}
            else if(lifted&&MatchIX(event,&state.escape)){enqueue(TILE_INPUT_DROP,0);consume=TRUE;}
            else for(i=0;i<state.count;++i)if(state.enabled[i]&&MatchIX(event,&state.groups[i])) {
                enqueue(TILE_INPUT_GROUP,i);consume=TRUE;break;
            }
            if(consume&&code<128)state.swallowed[code]=1;
        } else if(code<128&&state.swallowed[code])consume=TRUE;
    } else if(event->ie_Class==IECLASS_RAWMOUSE) {
        unsigned code=event->ie_Code&~IECODE_UP_PREFIX;
        unsigned button=code==IECODE_LBUTTON?1u:code==IECODE_RBUTTON?2u:code==IECODE_MBUTTON?4u:0u;
        if(button) {
            if(event->ie_Code&IECODE_UP_PREFIX) {
                /* Release belongs to whoever received its matching press.
                 * A press predating lift must still release the old gadget. */
                consume=!!(state.mouseButtons&button);state.mouseButtons&=~button;
            } else if(lifted) {
                state.mouseButtons|=button;consume=TRUE;
                enqueue(button==1?TILE_INPUT_CLICK:TILE_INPUT_DROP,0);
            } else {
                LONG x=state.screen->MouseX,y=state.screen->MouseY;unsigned group;
                if(panel_hit(x,y,&group)) {
                    state.mouseButtons|=button;consume=TRUE;
                    enqueue_at(button==1?TILE_INPUT_PANEL_CLICK:TILE_INPUT_PANEL_DROP,group,x,y);
                }
            }
        }
    }
    if(consume)DisposeCxMsg(message);
}
struct TileInput *TileInputOpen(struct Screen *screen,const char *const *keys,unsigned count)
{
    struct NewBroker broker={NB_VERSION,"Tiles","Tiles lift","Tile group hotkeys",NBU_UNIQUE,0,0,NULL,0};
    CxObj *custom;unsigned i;
    if(state.broker||!screen||count>GROUP_LIMIT||(count&&!keys))return FALSE;
    memset(&state,0,sizeof(state));state.signal=-1;
    if(ParseIX("lamiga d",&state.all)||ParseIX("esc",&state.escape))return FALSE;
    for(i=0;i<count;++i)if(!disabled_key(keys[i])) {
        char copy[64];size_t n=0;
        while(n<sizeof(copy)&&keys[i][n])++n;
        if(n>=sizeof(copy)) {
            kprintf("[TILEI] group=%u hotkey-disabled reason=too-long\n",i);
            continue;
        }
        memcpy(copy,keys[i],n+1);
        if(ParseIX(copy,&state.groups[i])||state.groups[i].ix_Class!=IECLASS_RAWKEY) {
            kprintf("[TILEI] group=%u hotkey-disabled reason=invalid-key\n",i);
            continue;
        }
        state.enabled[i]=1;
    }
    state.signal=AllocSignal(-1);if(state.signal<0)return FALSE;
    state.screen=screen;state.task=FindTask(NULL);state.count=count;
    state.broker=CxBroker(&broker,NULL);
    if(!state.broker){FreeSignal(state.signal);state.signal=-1;return FALSE;}
    custom=CxCustom(input_action,0);
    if(!custom){TileInputClose(&state);return FALSE;}
    AttachCxObj(state.broker,custom);
    if(CxObjError(state.broker)){TileInputClose(&state);return FALSE;}
    ActivateCxObj(state.broker,TRUE);return &state;
}
void TileInputClose(struct TileInput *input)
{
    if(input!=&state||!state.broker)return;
    ActivateCxObj(state.broker,FALSE);DeleteCxObjAll(state.broker);state.broker=NULL;
    SetSignal(0,1UL<<state.signal);FreeSignal(state.signal);state.signal=-1;
    state.screen=NULL;state.task=NULL;
}
ULONG TileInputSignals(struct TileInput *input) { return input==&state&&state.broker?1UL<<state.signal:0; }
void TileInputSetLifted(struct TileInput *input, BOOL lifted) { if(input!=&state)return; __atomic_store_n(&state.lifted,!!lifted,__ATOMIC_RELEASE); }
BOOL TileInputSetPanels(struct TileInput *input,const struct TileInputPanel *panels,unsigned count)
{
    if(input!=&state || !state.broker || count>8 || (count&&!panels))return FALSE;
    for(unsigned n=0;n<count;++n)if(!panels[n].width || !panels[n].height ||
        panels[n].width>4096 || panels[n].height>4096 || panels[n].left < -4096 || panels[n].left>4096 ||
        panels[n].top < -4096 || panels[n].top>4096 || panels[n].group>=GROUP_LIMIT)return FALSE;
    __atomic_add_fetch(&state.panelSequence,1,__ATOMIC_SEQ_CST);
    for(unsigned n=0;n<count;++n) {
        __atomic_store_n(&state.panels[n].group,panels[n].group,__ATOMIC_SEQ_CST);
        __atomic_store_n(&state.panels[n].left,panels[n].left,__ATOMIC_SEQ_CST);
        __atomic_store_n(&state.panels[n].top,panels[n].top,__ATOMIC_SEQ_CST);
        __atomic_store_n(&state.panels[n].width,panels[n].width,__ATOMIC_SEQ_CST);
        __atomic_store_n(&state.panels[n].height,panels[n].height,__ATOMIC_SEQ_CST);
    }
    __atomic_store_n(&state.panelCount,count,__ATOMIC_SEQ_CST);
    __atomic_add_fetch(&state.panelSequence,1,__ATOMIC_SEQ_CST);
    return TRUE;
}
BOOL TileInputNext(struct TileInput *input, struct TileInputEvent *event)
{
    unsigned tail,head;if(input!=&state||!event||!state.broker)return FALSE;
    tail=__atomic_load_n(&state.tail,__ATOMIC_RELAXED);
    head=__atomic_load_n(&state.head,__ATOMIC_ACQUIRE);if(tail==head)return FALSE;
    *event=state.queue[tail%QUEUE_SIZE];
    __atomic_store_n(&state.tail,tail+1,__ATOMIC_RELEASE);return TRUE;
}
