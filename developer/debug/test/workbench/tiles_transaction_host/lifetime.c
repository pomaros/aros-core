/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdint.h>
#include "tile_transaction.h"
#define CHILD_ACTIVE 4
#define CHILD_EXITED 3
#define MEMF_ANY 0
#define TRUE 1
#define TAG_DONE 0
#define NP_Entry 1
#define NP_Name 2
#define NP_UserData 3
#define NP_StackSize 4
#define NP_NotifyOnDeath 5
#define Stricmp strcasecmp
typedef uintptr_t IPTR;
static unsigned busy,childID,status,waits,frees,delays,starts,failCreate;
static struct TileDropRequest *running;
static struct TileDropRequest saved[128];
static void worker(void) {}
static void *AllocVec(size_t n,int flags){(void)flags;return malloc(n);}
static void FreeVec(void *p){free(p);}
static char *FilePart(const char *p){const char *q=strrchr(p,'/');if(!q)q=strrchr(p,':');return (char *)(q?q+1:p);}
static int CreateNewProcTags(int first,...)
{
    va_list args;int tag=first;struct TileDropRequest *request=NULL;unsigned stack=0,notify=0;
    va_start(args,first);
    while(tag){
        if(tag==NP_StackSize)stack=va_arg(args,int);
        else if(tag==NP_NotifyOnDeath)notify=va_arg(args,int);
        else {IPTR value=va_arg(args,IPTR);if(tag==NP_UserData)request=(void *)value;}
        tag=va_arg(args,int);
    }
    va_end(args);assert(request&&stack>=65536&&notify);
    if(failCreate)return 0;
    assert(!running&&!childID);running=request;++starts;status=CHILD_ACTIVE;return 1;
}
static void complete(void)
{
    assert(running);saved[frees]=*running;FreeVec(running);running=NULL;busy=0;
}
static unsigned ChildStatus(unsigned id){assert(id==17);return status;}
static void ChildWait(unsigned id){assert(id==17);++waits;if(running)complete();status=CHILD_EXITED;}
static void ChildFree(unsigned id){assert(id==17&&status==CHILD_EXITED);++frees;}
static void Delay(unsigned ticks){assert(ticks==1);++delays;if(busy&&!childID){childID=17;status=CHILD_ACTIVE;}else failCreate=0;}
#include "lifetime-functions.inc"
static struct TileDropRequest position(unsigned n)
{
    struct TileDropRequest r={0};r.operation=TILE_TXN_POSITION;
    snprintf(r.source,sizeof(r.source),"HOME:Tiles/Group/Member%u",n);r.x=100+n;r.y=200+n;return r;
}
#define ICONENTRY_FLAG_SELECTED 1
struct IconEntry {struct IconEntry *next;unsigned ie_Flags;long ie_IconX,ie_IconY;unsigned member;};
#define ForeachNode(list,node) for(node=*(list);node;node=node->next)
static void IconList_QueueTileTransaction(void *a,void *b,struct IconEntry *entry,void *unused,unsigned op,long x,long y)
{
    (void)a;(void)b;(void)unused;struct TileDropRequest r=position(entry->member);
    r.operation=op;r.x=x;r.y=y;assert(TileDropStart(&r));
}
static void selected(void)
{
    struct IconEntry c={NULL,1,103,203,3},skip={&c,0,777,777,4},b={&skip,1,102,202,2},a={&b,1,101,201,1};
    struct {struct IconEntry *icld_IconList;} storage={&a},*data=&storage;
    void *obj=NULL;
#include "selection.inc"
}
int main(void)
{
    /* Synchronous IconList notifications arrive before worker exit. */
    struct TileDropRequest a=position(1),b=position(2);
    selected();
    assert(starts==1&&pendingCount==2&&!waits&&!delays);
    b.x=444;assert(TileDropStart(&b));assert(pendingCount==2);
    b.x=999; /* Borrowed caller storage must not escape. */
    childID=17;complete();TileDropPoll();assert(starts==1&&!frees); /* busy0 is NOT death */
    status=CHILD_EXITED;TileDropPoll();assert(starts==2&&frees==1&&pendingCount==1);
    assert(running->x==444);assert(!waits&&!delays);
    TileDropFinish();assert(starts==3&&frees==3&&waits==2&&!pendingCount&&!childID);
    assert(saved[0].x==101&&saved[1].x==444&&saved[2].x==103);
    /* Saturation is explicit; no accepted item is lost. Structural operations
     * cannot overtake positions. Retry process creation without a GUI wait. */
    failCreate=1;
    for(unsigned n=0;n<40;++n){a=position(n);assert(TileDropStart(&a));}
    a=position(40);assert(!TileDropStart(&a));assert(pendingCount==40);
    a.operation=TILE_TXN_EXTRACT;assert(!TileDropStart(&a));
    assert(waits==2);failCreate=0;TileDropFinish();assert(frees==43&&!pendingCount&&!running);
    for(unsigned n=0;n<40;++n)assert(saved[n+3].x==100+n);
    puts("tiles-worker-lifetime PASS");
}
