/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiles_config.h"
typedef int BOOL;
typedef long LONG;
typedef unsigned long ULONG;
typedef void *APTR;
typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define GVF_GLOBAL_ONLY 1
#define AMCLASSICON_Region 13
#define AMCLASSICON_PointerEnter 15
#define AMCLASSICON_PointerLeave 17
#define TILE_LOG(...) ((void)0)
struct Message { int replied; };
struct AppMessage { struct Message am_Message; ULONG am_ID,am_Class; LONG am_MouseX,am_MouseY; };
struct Queue { struct AppMessage messages[8]; unsigned count,next; };
struct TileEntry {
    struct TileConfig config;
    struct TileValue value;
    char id[128];
    BOOL valid,healthy,dirty,live,watching,logged,refreshAgain;
    BOOL envTogglePending,envToggleValue;
};
struct TileService { struct TileEntry tiles[1]; unsigned count; struct Queue *apps; int sources; BOOL stopping,trace; };
static unsigned calls, fail_at, problems, refreshes;
static char stored[256], attempts[8][256];
static ULONG now_ms(void) { return 1000; }
static BOOL SetVar(const char *name,const char *value,LONG length,ULONG flags)
{
    assert(!strcmp(name,"toggle") && length==-1 && flags==GVF_GLOBAL_ONLY);
    assert(calls<8); strcpy(attempts[calls++],value);
    if (calls==fail_at) return FALSE;
    strcpy(stored,value); return TRUE;
}
static struct Message *GetMsg(struct Queue *q)
{ return q->next<q->count ? &q->messages[q->next++].am_Message : NULL; }
static void ReplyMsg(struct Message *message) { assert(!message->replied); message->replied=1; }
static void problem(struct TileEntry *tile,const char *reason) { (void)tile;(void)reason; ++problems; }
static void request_value(struct TileService *s,struct TileEntry *t,ULONG now)
{ (void)s;(void)t;(void)now; ++refreshes; }
static BOOL TileSetCommand(const char *a,const char *b,char *c,ULONG d)
{ (void)a;(void)b;(void)c;(void)d; assert(!"unexpected port source"); return FALSE; }
static BOOL TileSourceSend(int *a,APTR b,BOOL c,const char *d,const char *e,ULONG f)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; assert(!"unexpected port source"); return FALSE; }
/* PRODUCTION */
static struct TileService service;
static struct Queue queue;
static struct TileEntry *reset(void)
{
    memset(&service,0,sizeof(service)); memset(&queue,0,sizeof(queue));
    memset(attempts,0,sizeof(attempts)); calls=fail_at=problems=refreshes=0;
    strcpy(stored,"off"); service.count=1;service.apps=&queue;
    struct TileEntry *t=&service.tiles[0];
    t->valid=t->healthy=t->live=t->watching=TRUE;
    t->config.templateKind=TILE_TOGGLE;strcpy(t->config.env,"toggle");
    assert(TileValueParse(TILE_TOGGLE,"off",&t->value));
    return t;
}
static void clicks(unsigned n)
{
    memset(&queue,0,sizeof(queue));queue.count=n;
    for(unsigned i=0;i<n;++i) { queue.messages[i].am_ID=1;queue.messages[i].am_Class=AMCLASSICON_Region; }
    app_messages(&service);
    for(unsigned i=0;i<n;++i) assert(queue.messages[i].am_Message.replied);
}
static void notification(void) { source_result(&service.tiles[0],FALSE,TRUE,stored,&service); }
int main(void)
{
    struct TileEntry *t=reset();
    clicks(2);
    assert(calls==2 && !strcmp(attempts[0],"on") && !strcmp(attempts[1],"off"));
    assert(!strcmp(stored,"off") && t->envTogglePending && !t->envToggleValue);
    assert(!t->value.boolean && !t->dirty && !refreshes);
    notification();assert(!t->envTogglePending && !t->value.boolean && !t->dirty);

    t=reset();assert(set_value(&service,t,"on"));clicks(1);
    assert(calls==2 && !strcmp(stored,"off") && !t->value.boolean && !t->dirty);
    notification();assert(!t->envTogglePending && !t->dirty);

    t=reset();fail_at=2;clicks(2);
    assert(calls==2 && !strcmp(stored,"on") && t->envTogglePending && t->envToggleValue);
    assert(!t->value.boolean && !t->dirty);
    clicks(1);assert(calls==3 && !strcmp(attempts[2],"off") && !strcmp(stored,"off"));
    notification();assert(!t->envTogglePending && !t->value.boolean && !t->dirty);

    t=reset();clicks(1);assert(!t->value.boolean && !t->dirty && t->envTogglePending);
    notification();assert(t->value.boolean && t->dirty && !t->envTogglePending);
    t->dirty=FALSE;notification();assert(!t->dirty); /* repeated notify does not redraw */
    assert(set_value(&service,t,"off"));
    source_result(t,FALSE,FALSE,"",&service);
    assert(!t->envTogglePending && !t->healthy && !t->dirty && problems==1);

    t=reset();fail_at=1;clicks(1);
    assert(!t->envTogglePending && !strcmp(stored,"off") && !t->value.boolean);
    clicks(1);assert(!strcmp(stored,"on") && t->envTogglePending);
    notification();assert(t->value.boolean && t->dirty && !t->envTogglePending);
    puts("PASS actual set_value/app_messages/source_result: queued toggles, explicit SET, failed writes, authoritative notification, dirty state");
    return 0;
}
