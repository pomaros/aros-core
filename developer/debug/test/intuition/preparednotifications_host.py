#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# This file is part of AROS, distributed under the AROS Public License 1.1.
"""Actual notification bodies with allocator faults and guarded enqueue.

Host substitutes model Exec queues/pools, not task scheduling or native rendering.
The public allocation/free bodies are extracted too, retaining ReplyMsg ownership.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[4]
source = (ROOT/'rom/intuition/preparednotifications.c').read_text()
source = '\n'.join(s for s in source.splitlines() if not s.startswith('#include'))
header = (ROOT/'rom/intuition/preparednotifications.h').read_text()
header = '\n'.join(s for s in header.splitlines() if not s.startswith('#include'))
def actual(name):
    text = (ROOT/f'rom/intuition/{name.lower()}.c').read_text()
    return text.split('AROS_LIBFUNC_INIT',1)[1].split('AROS_LIBFUNC_EXIT',1)[0]
prefix = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t ULONG; typedef uintptr_t IPTR; typedef uint16_t UWORD;
typedef uint8_t UBYTE; typedef void *APTR; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define NT_MESSAGE 5
#define IDCMP_CHANGESCALE 1
#define IDCMP_CHANGEWINDOW 2
#define IDCMP_NEWSIZE 4
#define IDCMP_REFRESHWINDOW 8
#define IDCMP_DELTAMOVE 16
#define IDCMP_IDCMPUPDATE 32
#define IECLASS_CHANGEWINDOW 11
#define IECLASS_SIZEWINDOW 12
#define IECLASS_EVENT 13
#define IECLASS_REFRESHWINDOW 14
#define CWCODE_MOVESIZE 21
#define IECODE_NEWSIZE 22
#define IECODE_REFRESH 23
#define IEQUALIFIER_REPEAT 64
#define ASSERT_VALID_PTR_OR_NULL(x)
#define DEBUG_ALLOCINTUIMESSAGE(x)
#define DEBUG_FREEINTUIMESSAGE(x)
#define memclr(p,s) memset(p,0,s)
struct Node { struct Node *next,*prev; UBYTE ln_Type; };
struct List { struct Node *head,*tail; };
#define ForeachNode(l,n) for((n)=(void*)(l)->head;(n);(n)=(void*)((struct Node*)(n))->next)
struct MsgPort { struct List mp_MsgList; unsigned count; struct Message *queue[32]; };
struct Message { struct Node mn_Node; UWORD mn_Length; struct MsgPort *mn_ReplyPort; };
struct Window;
struct IntuiMessage { struct Message ExecMessage; struct Window *IDCMPWindow; ULONG Class;
 UWORD Code,Qualifier; APTR IAddress; int MouseX,MouseY; ULONG Seconds,Micros; };
struct TabletData { void *td_TagList; };
struct ExtIntuiMessage { struct IntuiMessage im; struct TabletData *eim_TabletData; };
struct IntIntuiMessage { struct ExtIntuiMessage eim; APTR prevCodeQuals; };
struct Time { ULONG tv_secs,tv_micro; };
struct Window { ULONG IDCMPFlags; struct MsgPort *UserPort,*WindowPort; int MouseX,MouseY;
 int CompositionMouseDeltaX,CompositionMouseDeltaY; ULONG num_repeatevents;
 struct Time lastmsgsent; struct IntuiMessage *MessageKey; void *WScreen; };
struct InputEvent { struct InputEvent *ie_NextEvent; UBYTE ie_Class; UWORD ie_Code,ie_Qualifier;
 APTR ie_EventAddress; struct Time ie_TimeStamp; };
struct GeneratedInputEvent { struct Node node; struct InputEvent ie; };
struct IIHData { void *InputEventMemPool; struct List NewAllocatedInputEventList;
 struct InputEvent **EndInputEventChain,*FreeInputEvents; UWORD ActQualifier; };
struct Interrupt { APTR is_Data; };
struct Library { int unused; };
struct IntuitionBase { APTR IDCMPPool; struct MsgPort *IntuiReplyPort;
 struct Interrupt *InputHandler; struct Library *UtilityBase; };
#define GetPrivIBase(b) (b)
#define IW(w) (w)
static unsigned allocations,failat,live,hardtrap,put_count,replies,forbidden;
static void *allocate(size_t n) { assert(!hardtrap); if(++allocations==failat)return NULL;
 void*p=calloc(1,n); assert(p);live++;return p; }
static void release(void*p) { assert(!hardtrap); if(p){assert(live);live--;free(p);} }
static APTR AllocVec(IPTR n,ULONG flags){(void)flags;return allocate(n);}
static void FreeVec(APTR p){release(p);}
static APTR AllocPooled(APTR pool,IPTR n){assert(pool);return allocate(n);}
static void FreePooled(APTR pool,APTR p,IPTR n){assert(pool);(void)n;release(p);}
static void FreeTagItems(APTR p){release(p);}
static void CurrentTime(ULONG*s,ULONG*m){*s=123;*m=456;}
static void Forbid(void){forbidden++;}
static void Permit(void){assert(forbidden);forbidden--;}
static void AddTail(struct List*,struct Node*);
static void PutMsg(struct MsgPort*p,struct Message*m){assert(forbidden);p->queue[p->count++]=m;AddTail(&p->mp_MsgList,&m->mn_Node);put_count++;}
static void ReplyMsg(struct Message*m){struct MsgPort*p=m->mn_ReplyPort;assert(p);p->queue[p->count++]=m;replies++;}
static void AddTail(struct List*l,struct Node*n){n->next=NULL;n->prev=l->tail;
 if(l->tail)l->tail->next=n;else l->head=n;l->tail=n;}
'''
wrappers = ('static struct IntuiMessage *test_alloc(struct Window *window, struct IntuitionBase *IntuitionBase){' + actual('AllocIntuiMessage') + '}\n'
 + 'static void test_free(struct IntuiMessage *imsg, struct IntuitionBase *IntuitionBase){' + actual('FreeIntuiMessage') + '}\n'
 + '#define AllocIntuiMessage(w) test_alloc(w,IntuitionBase)\n#define FreeIntuiMessage(m) test_free(m,IntuitionBase)\n')
main = r'''
static void reset(void){assert(!live);allocations=failat=put_count=replies=forbidden=0;}
int main(void){
 struct MsgPort user={0},reply={0}; struct IIHData handler={0}; struct Interrupt interrupt={&handler};
 struct IntuitionBase base={(APTR)1,&reply,&interrupt,0};
 struct Window w={15,&user,&reply,10,20,-3,4,0,{0},0};
 w.WScreen=(void*)0x1234;
 struct InputEvent *chain=NULL,reuse={0};
 handler.InputEventMemPool=(APTR)2;handler.EndInputEventChain=&chain;
 handler.FreeInputEvents=&reuse;handler.ActQualifier=IEQUALIFIER_REPEAT;
 struct PreparedNotificationSpec specs[]={{&w,15,200}};
 unsigned total=0;
 for(unsigned mode=0;mode<3;mode++){
  w.IDCMPFlags=mode==0?15:0;w.UserPort=mode==2?NULL:&user;
  reset();struct PreparedNotifications*b=PreparedNotificationsPrepare(specs,1,&base);assert(b);
  total=allocations;assert(total==(mode==0?7:6));
  assert(!chain&&!put_count&&!replies&&handler.FreeInputEvents==&reuse);
  PreparedNotificationsRelease(b,&base);assert(!live);
  for(unsigned failure=1;failure<=total;failure++){
   reset();failat=failure;
   b=PreparedNotificationsPrepare(specs,1,&base);
   assert(!b&&!live&&!chain&&!put_count&&!replies&&!user.count&&!reply.count);
   assert(!handler.NewAllocatedInputEventList.head&&handler.FreeInputEvents==&reuse);
  }
  reset();b=PreparedNotificationsPrepare(specs,1,&base);assert(b);
  /* Fill reads final state, not the stale state observed during preparation. */
  w.MouseX=31;w.MouseY=42;w.IDCMPFlags|=IDCMP_DELTAMOVE;
  hardtrap=1;PreparedNotificationsEnqueue(b,&base);assert(!put_count&&!chain);
  PreparedNotificationsFill(b,&base);PreparedNotificationsEnqueue(b,&base);
  unsigned published=put_count;PreparedNotificationsEnqueue(b,&base);assert(put_count==published);
  assert(!forbidden&&handler.FreeInputEvents==&reuse);
  hardtrap=0;PreparedNotificationsRelease(b,&base);
  unsigned msgcount=0,eventcount=0;
  for(unsigned i=0;i<user.count;i++){
   struct IntuiMessage*m=(struct IntuiMessage*)user.queue[i];
   assert(m->IDCMPWindow==&w&&m->IAddress==(i==0?w.WScreen:(void*)&w)&&m->Qualifier==IEQUALIFIER_REPEAT);
   assert(m->MouseX==-3&&m->MouseY==4&&m->Seconds==123&&m->Micros==456);
   assert(m->ExecMessage.mn_Length==sizeof(struct ExtIntuiMessage));
   assert(m->ExecMessage.mn_ReplyPort==&reply);
   if(i==0)assert(m->Class==IDCMP_CHANGESCALE&&m->Code==200);
   if(i==1)assert(m->Class==IDCMP_CHANGEWINDOW&&m->Code==CWCODE_MOVESIZE);
   if(i==2)assert(m->Class==IDCMP_NEWSIZE&&m->Code==0);
   if(i==3)assert(m->Class==IDCMP_REFRESHWINDOW&&m->Code==0);
   ReplyMsg(&m->ExecMessage);msgcount++;
  }
  assert(msgcount==(mode==0?4:0));
  assert(w.num_repeatevents==msgcount);
  if(msgcount){ assert(w.MessageKey==(struct IntuiMessage*)user.queue[3]);
   assert(w.lastmsgsent.tv_secs==123&&w.lastmsgsent.tv_micro==456); }
  for(unsigned i=0;i<reply.count;i++)test_free((struct IntuiMessage*)reply.queue[i],&base);
  while(chain){
   struct InputEvent*e=chain;chain=e->ie_NextEvent;
   assert(e->ie_EventAddress==&w&&e->ie_Qualifier==IEQUALIFIER_REPEAT);
   assert(e->ie_TimeStamp.tv_secs==123&&e->ie_TimeStamp.tv_micro==456);
   if(mode==0)assert(e->ie_Class==IECLASS_EVENT&&e->ie_Code==(eventcount?IECODE_REFRESH:IECODE_NEWSIZE));
   else { unsigned expected[]={IECLASS_CHANGEWINDOW,IECLASS_SIZEWINDOW,IECLASS_EVENT,IECLASS_REFRESHWINDOW,IECLASS_EVENT};
    assert(e->ie_Class==expected[eventcount]); }
   eventcount++;
  }
  assert(eventcount==(mode==0?2:5));
  struct Node*n=handler.NewAllocatedInputEventList.head;
  while(n){struct Node*next=n->next;FreePooled(handler.InputEventMemPool,n,sizeof(struct GeneratedInputEvent));n=next;}
  assert(!live);handler.NewAllocatedInputEventList=(struct List){0};handler.EndInputEventChain=&chain;
  user.count=reply.count=0;user.mp_MsgList=(struct List){0};w.num_repeatevents=0;
 }
 /* A pre-existing refresh is retained unchanged and suppresses both refresh
  * deliveries; an unrelated window's refresh does not suppress this window. */
 reset();w.UserPort=&user;w.IDCMPFlags=15;specs[0].flags=PNF_REFRESH;
 struct IntuiMessage pending={0};pending.Class=IDCMP_REFRESHWINDOW;pending.IAddress=&w;
 AddTail(&user.mp_MsgList,&pending.ExecMessage.mn_Node);
 struct PreparedNotifications*coalesced=PreparedNotificationsPrepare(specs,1,&base);
 assert(coalesced&&allocations==1&&coalesced->count==0&&user.mp_MsgList.head==&pending.ExecMessage.mn_Node);
 PreparedNotificationsRelease(coalesced,&base);assert(!live);
 pending.IAddress=NULL;coalesced=PreparedNotificationsPrepare(specs,1,&base);
 assert(coalesced&&coalesced->count==2);PreparedNotificationsRelease(coalesced,&base);assert(!live);
 user.mp_MsgList=(struct List){0};
 /* A port disappearing before publication returns the reserved message through
  * ReplyMsg rather than freeing a client-owned message or dereferencing NULL. */
 reset();w.UserPort=&user;w.IDCMPFlags=15;specs[0].flags=PNF_SCALE;
 struct PreparedNotifications*b=PreparedNotificationsPrepare(specs,1,&base);assert(b);
 w.UserPort=NULL;hardtrap=1;PreparedNotificationsFill(b,&base);PreparedNotificationsEnqueue(b,&base);hardtrap=0;
 assert(reply.count==1&&!put_count);PreparedNotificationsRelease(b,&base);
 test_free((struct IntuiMessage*)reply.queue[0],&base);reply.count=0;assert(!live);
 reset();w.UserPort=&user;w.IDCMPFlags=15;
 struct Window second=w;
 struct PreparedNotificationSpec pair[]={{&w,15,200},{&second,15,200}};
 b=PreparedNotificationsPrepare(pair,2,&base);assert(b);unsigned pairtotal=allocations;
 assert(pairtotal==13);PreparedNotificationsRelease(b,&base);
 for(unsigned f=1;f<=pairtotal;f++){
  reset();failat=f;b=PreparedNotificationsPrepare(pair,2,&base);
  assert(!b&&!live&&!put_count&&!replies&&!chain&&!handler.NewAllocatedInputEventList.head);
 }
 reset();b=PreparedNotificationsPrepare(NULL,0,&base);assert(b);
 hardtrap=1;PreparedNotificationsFill(b,&base);PreparedNotificationsEnqueue(b,&base);hardtrap=0;
 PreparedNotificationsRelease(b,&base);assert(!live);
 struct Interrupt*saved=base.InputHandler;base.InputHandler=NULL;
 assert(!PreparedNotificationsPrepare(specs,1,&base));base.InputHandler=saved;
 reset();assert(!PreparedNotificationsPrepare(NULL,1,&base));
 assert(!PreparedNotificationsPrepare(specs,UINT32_MAX,&base));
 specs[0].flags=0x80000000;assert(!PreparedNotificationsPrepare(specs,1,&base));assert(!live&&!put_count);
 specs[0].flags=15;specs[0].window=NULL;assert(!PreparedNotificationsPrepare(specs,1,&base));assert(!live);
 put_count=0;printf("PASS actual bodies: every allocation failure, final fill, hardtrap enqueue, queues, ReplyMsg ownership\n");return 0;
}
'''
mutations = {
 'enqueue-allocation': source.replace('    Forbid();', '    AllocVec(1, 0);\n    Forbid();'),
 'abort-leak': source.replace('            FreeIntuiMessage(batch->items[i].message);', '            (void)batch->items[i].message;'),
 'lost-newsize-event': source.replace('0, IECODE_NEWSIZE,', '0, 0,'),
 'lost-refresh-event': source.replace('0, IECODE_REFRESH,', '0, 0,'),
 'missing-refresh-coalescing': source.replace('!refresh_pending(window, IntuitionBase)', 'TRUE'),
 'stale-scale': source.replace('IDCMP_CHANGESCALE, specs[i].scale,', 'IDCMP_CHANGESCALE, 100,'),
 'wrong-reply-owner': source.replace('item->message = NULL;', 'item->message = message;'),
 'prepare-publishes': source.replace('    return batch;','    PreparedNotificationsFill(batch, IntuitionBase);\n    PreparedNotificationsEnqueue(batch, IntuitionBase);\n    return batch;'),
}
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp)
    for name,body in [('production',source),*mutations.items()]:
        c=d/f'{name}.c';exe=d/name
        c.write_text(prefix+'\n'+header+'\n'+wrappers+'\n'+body+'\n'+main)
        subprocess.run(['clang','-std=c11','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':
            if result.returncode: raise RuntimeError(result.stdout+result.stderr)
            print(result.stdout.strip())
        elif not result.returncode: raise RuntimeError(f'mutation escaped: {name}')
        else: print(f'REJECT {name}')
