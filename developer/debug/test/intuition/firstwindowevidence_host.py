#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL), version 1.1.
"""Actual one-shot snapshots/emitter and OpenWindow source boundaries.
No real kernel logging, GUI rendering or SMP scheduler is exercised here.
"""
import argparse, pathlib, re, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[4]);a=p.parse_args()
s=(a.source/'rom/intuition/openwindow.c').read_text();h=(a.source/'rom/intuition/firstwindowevidence.h').read_text()
def function(text,name):
 m=re.search(r'static VOID '+name+r'\([^;{}]*\)\s*\{',text);assert m,name
 start=m.start();pos=m.end();depth=1
 while depth:
  depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
 return text[start:pos]
helpers=function(s,'CaptureFirstWindowEvidence')+'\n'+function(s,'EmitFirstWindowEvidence')
wrapper=function(s,'int_openwindow')
header='\n'.join(x for x in h.splitlines() if not x.startswith('#include'))
# Check the real callsites, rather than substituting a post-return current-scale
# query. Execute the actual attempt block and actual publication statements.
attempt=re.search(r'    \{\n        LONG evidenceLock = LockIBase\(0\);[^}]+\}',s).group()
allocation=s.index('w  = AllocMem (sizeof(struct IntWindow), MEMF_CLEAR);')
assert s.index(attempt)<allocation
assert 'nw.Screen == NULL' in s[s.index('if (nw.Type == WBENCHSCREEN)'):s.index(attempt)]
publish=re.search(r'            w->NextWindow = w->WScreen->FirstWindow;.*?w->WindowPort = GetPrivIBase\(IntuitionBase\)->IntuiReplyPort;',s,re.S).group()
assert publish.index('FirstWindow = w;')<publish.index('CaptureFirstWindowEvidence(')
assert s.index(publish)<s.index('UnlockIBase (lock);',s.index(publish))
post=s[s.index('DoSyncAction((APTR)int_openwindow'):s.index('driver_init_done = TRUE;',s.index('DoSyncAction((APTR)int_openwindow'))]
assert post.index('ReleaseSemaphore(')<post.index('EmitFirstWindowEvidence(')<post.index('if (!msg.success)')
assert s.count('CaptureFirstWindowEvidence(')==3
for path in ('closescreen.c','closewindow.c','screenclass.c','windowscale.c'):
 assert 'FirstWindowEvidence' not in (a.source/'rom/intuition'/path).read_text(),path
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef int BOOL;typedef void VOID;
#define TRUE 1
#define FALSE 0
#define NT_PROCESS 13
#define SCREENTYPE 15
#define WBENCHSCREEN 1
#define RESOURCE_SCREEN 1
struct Task {struct {int ln_Type;}tc_Node;};
struct SignalSemaphore {struct Task *ss_Owner;unsigned depth;};
struct Layer_Info {struct SignalSemaphore Lock;};
struct TextFont {unsigned tf_YSize;};
struct DrawInfo {struct TextFont *dri_Font;};
struct IntDrawInfo {struct TextFont *dri_Font;};
struct ScreenRenderContext {unsigned src_Scale,src_LegacyScaleFactor,src_PhysicalBarHeight;struct {unsigned tta_YSize;}src_BaseFont;struct DrawInfo *src_PhysicalDrawInfo;};
struct Screen {unsigned Flags;struct Layer_Info LayerInfo;struct Screen *NextScreen;struct Window *FirstWindow;const struct ScreenRenderContext *ctx;};
struct Window {struct Screen *WScreen;struct Window *NextWindow;void *WindowPort;BOOL ScaleAware;struct IntDrawInfo frame;const struct ScreenRenderContext *bound,*framectx;struct Layer_Info privateLI;};
struct IIHData {struct Task *InputDeviceTask;};struct Handler {void *is_Data;};struct LayersBase {int unused;};
'''
base=r'''
struct IntuitionBase {struct SignalSemaphore *IBaseLock;struct SignalSemaphore GadgetLock,WindowLock,IntuiActionLock,InputHandlerLock,MenuLock,ViewLordLock,PubScrListLock,ScreenNotificationListLock,WinDecorSem,ScrDecorSem,MenuDecorSem;struct Handler *InputHandler;struct Screen *FirstScreen;struct FirstWindowEvidence FirstWindowEvidence;struct LayersBase *LayersBase;void *IntuiReplyPort;};
#define IBase IntuitionBase
#define GetPrivIBase(x) (x)
#define IW(x) (x)
struct OpenWindowActionMsg {struct Window *window;BOOL success,invisible;};
static struct Task process={{NT_PROCESS}},input={{1}},*current=&process;
static struct IntuitionBase *global;static unsigned logCount,callbacks,allocations;static char logText[8192];
static struct Task *FindTask(void *p){assert(!p);return current;}
static LONG LockIBase(int n){assert(!n);struct SignalSemaphore *s=global->IBaseLock;assert(!s->ss_Owner||s->ss_Owner==current);s->ss_Owner=current;++s->depth;return 0;}
static void UnlockIBase(LONG n){assert(!n);struct SignalSemaphore *s=global->IBaseLock;assert(s->ss_Owner==current&&s->depth);if(!--s->depth)s->ss_Owner=NULL;}
static const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *s){return s->ctx;}
static struct IntDrawInfo *WindowFrameDrawInfo(struct Window *w){return &w->frame;}
static struct Layer_Info *WindowCompositionLayerInfo(struct Window *w){return &w->privateLI;}
static const struct ScreenRenderContext *WindowBoundRenderContext(struct Window *w){return w->bound;}
static const struct ScreenRenderContext *WindowFrameContext(struct Window *w){return w->framectx;}
static BOOL BoundScreenRenderContext(const struct ScreenRenderContext *c,struct Screen *s){return c==s->ctx;}
static BOOL ResourceExisting(struct Screen *s,int r,struct IntuitionBase *b){assert(r==RESOURCE_SCREEN&&b==global);return s!=NULL;}
#define LOCK_REFRESH(s) do{assert(LayersBase==global->LayersBase);assert(!(s)->LayerInfo.Lock.ss_Owner);(s)->LayerInfo.Lock.ss_Owner=current;}while(0)
#define UNLOCK_REFRESH(s) do{assert((s)->LayerInfo.Lock.ss_Owner==current);(s)->LayerInfo.Lock.ss_Owner=NULL;}while(0)
static void bug(const char *fmt,...){assert(current==&process);assert(!global->IBaseLock->ss_Owner);struct SignalSemaphore *locks[]={&global->GadgetLock,&global->WindowLock,&global->IntuiActionLock,&global->InputHandlerLock,&global->MenuLock,&global->ViewLordLock,&global->PubScrListLock,&global->ScreenNotificationListLock,&global->WinDecorSem,&global->ScrDecorSem,&global->MenuDecorSem};for(unsigned i=0;i<sizeof locks/sizeof *locks;i++)assert(locks[i]->ss_Owner!=current);for(struct Screen *s=global->FirstScreen;s;s=s->NextScreen){assert(s->LayerInfo.Lock.ss_Owner!=current);for(struct Window *w=s->FirstWindow;w;w=w->NextWindow)assert(w->privateLI.Lock.ss_Owner!=current);}va_list v;va_start(v,fmt);vsnprintf(logText+strlen(logText),sizeof logText-strlen(logText),fmt,v);va_end(v);++logCount;}
static void int_openwindow_body(struct OpenWindowActionMsg *,struct IntuitionBase *);
'''
body='''static void int_openwindow_body(struct OpenWindowActionMsg *msg,struct IntuitionBase *IntuitionBase){struct Window *w=msg->window;BOOL invisible=msg->invisible;LONG lock=LockIBase(0);\n'''+publish+'''\nUnlockIBase(lock);msg->success=TRUE;}\n'''
main=r'''
static struct TextFont baseFont={13},physicalFont={26};static struct DrawInfo physical={&physicalFont};
static struct ScreenRenderContext c100={100,1,18,{13},&physical},c200={200,2,36,{13},&physical};
static struct SignalSemaphore iblock;static struct IIHData ih={&input};static struct Handler handler={&ih};static struct IntuitionBase ib;static struct Screen screen,other;static struct LayersBase layers;
static void reset(void){memset(&ib,0,sizeof ib);memset(&iblock,0,sizeof iblock);memset(&screen,0,sizeof screen);memset(&other,0,sizeof other);screen.Flags=WBENCHSCREEN;screen.ctx=&c100;ib.IBaseLock=&iblock;ib.InputHandler=&handler;ib.FirstScreen=&screen;ib.LayersBase=&layers;global=&ib;current=&process;logCount=callbacks=allocations=0;logText[0]=0;}
static void attempt(struct Screen *s,BOOL hidden,BOOL aware){struct {struct Screen *Screen;}nw={s};BOOL windowinvisible=hidden,scaleaware=aware;struct IntuitionBase *IntuitionBase=&ib;
ATTEMPT
}
static void openWindow(struct Window *w,BOOL hidden){struct OpenWindowActionMsg m={w,FALSE,hidden};int_openwindow(&m,&ib);assert(m.success);}
int main(void){
 reset();attempt(&screen,FALSE,FALSE);assert(ib.FirstWindowEvidence.record[0].scale==100&&ib.FirstWindowEvidence.record[0].frameReady==0);/* Allocation fails here: no publication. */
 screen.ctx=&c200;struct Window w={.WScreen=&screen,.frame={&baseFont}};attempt(&screen,FALSE,FALSE);openWindow(&w,FALSE);assert(ib.FirstWindowEvidence.record[0].scale==100&&ib.FirstWindowEvidence.record[1].scale==200&&ib.FirstWindowEvidence.record[1].frameFont==13&&ib.FirstWindowEvidence.record[1].factor==2);
 EmitFirstWindowEvidence(&screen,&w,&ib);assert(logCount==2&&strstr(logText,"phase=attempt wb=1 type=1 hidden=0 aware=0 context=1 scale=100 factor=1")&&strstr(logText,"phase=open wb=1 type=1 hidden=0 aware=0 context=1 scale=200 factor=2"));EmitFirstWindowEvidence(&screen,&w,&ib);assert(logCount==2);
 reset();screen.ctx=&c200;attempt(&screen,TRUE,TRUE);struct Window sharp={.WScreen=&screen,.ScaleAware=TRUE,.frame={&physicalFont},.framectx=&c200,.bound=&c200};/* A stale constructor does not publish. */screen.ctx=&c100;struct OpenWindowActionMsg stale={&sharp,FALSE,TRUE};int_openwindow(&stale,&ib);assert(!stale.success&&ib.FirstWindowEvidence.captured==1);screen.ctx=&c200;openWindow(&sharp,TRUE);assert(ib.FirstWindowEvidence.record[1].frameReady&&ib.FirstWindowEvidence.record[1].frameFont==26&&ib.FirstWindowEvidence.record[1].hidden&&ib.FirstWindowEvidence.record[1].aware);
 struct FirstWindowEvidence saved=ib.FirstWindowEvidence;screen.FirstWindow=NULL;/* Workbench close/reopen, reset, then unrelated custom screen. */screen.ctx=&c100;attempt(&screen,FALSE,FALSE);struct Window reopened={.WScreen=&screen,.frame={&baseFont}};openWindow(&reopened,FALSE);other.ctx=&c100;other.Flags=0;attempt(&other,FALSE,FALSE);assert(!memcmp(saved.record,ib.FirstWindowEvidence.record,sizeof saved.record));
 /* Pending records survive calls in the input task and every relevant owned
  * GUI semaphore. Logging under any of these is a hard assertion failure. */
 current=&input;EmitFirstWindowEvidence(&screen,&reopened,&ib);assert(!logCount&&!ib.FirstWindowEvidence.emitted);current=&process;
 struct SignalSemaphore *locks[]={&iblock,&ib.GadgetLock,&ib.WindowLock,&ib.IntuiActionLock,&ib.InputHandlerLock,&ib.MenuLock,&ib.ViewLordLock,&ib.PubScrListLock,&ib.ScreenNotificationListLock,&ib.WinDecorSem,&ib.ScrDecorSem,&ib.MenuDecorSem,&screen.LayerInfo.Lock,&reopened.privateLI.Lock};
 for(unsigned i=0;i<sizeof locks/sizeof *locks;i++){locks[i]->ss_Owner=current;EmitFirstWindowEvidence(&screen,&reopened,&ib);assert(!logCount&&!ib.FirstWindowEvidence.emitted);locks[i]->ss_Owner=NULL;}
 screen.NextScreen=&other;other.LayerInfo.Lock.ss_Owner=current;EmitFirstWindowEvidence(&screen,&reopened,&ib);assert(!logCount);other.LayerInfo.Lock.ss_Owner=NULL;
 struct Window remote={.WScreen=&other};other.FirstWindow=&remote;remote.privateLI.Lock.ss_Owner=current;EmitFirstWindowEvidence(&screen,&reopened,&ib);assert(!logCount);remote.privateLI.Lock.ss_Owner=NULL;
 current->tc_Node.ln_Type=1;EmitFirstWindowEvidence(&screen,&reopened,&ib);assert(!logCount);current->tc_Node.ln_Type=NT_PROCESS;
 /* Context has changed before emission: printed metrics are the captured200. */
 EmitFirstWindowEvidence(&screen,&reopened,&ib);assert(logCount==2&&strstr(logText,"phase=attempt wb=1 type=1 hidden=1 aware=1 context=1 scale=200 factor=2")&&strstr(logText,"frame_ready=1 frame_font=26"));assert(!allocations&&!callbacks);
 reset();screen.ctx=NULL;attempt(&screen,FALSE,FALSE);assert(!ib.FirstWindowEvidence.record[0].contextReady&&!ib.FirstWindowEvidence.record[0].scale);puts("PASS actual first-window capture/publication/emitter; failed/stale/hidden/reset/reopen/other-screen/lock deferral");return 0;}
'''.replace('ATTEMPT',attempt)
code=pre+header+base+helpers+body+wrapper+main
mutations=[('overwrite earliest', 'if (!(state->captured & bit))','if (1)'),('wrong captured scale','record.scale = context->src_Scale;','record.scale = 100;'),('hide hidden','record.hidden = !!hidden;','record.hidden = 0;'),('hide awareness','record.aware = !!aware;','record.aware = 0;'),('wrong factor','record.factor = context->src_LegacyScaleFactor;','record.factor = 1;'),('invent pre-frame','record.frameReady = frame && frame->dri_Font;','record.frameReady = 1;'),('repeat log','state->emitted |= pending;','state->emitted |= 0;'),('input task log','me == ((struct IIHData *)IBase->InputHandler->is_Data)->InputDeviceTask ||','0 ||'),('gadget log','IBase->GadgetLock.ss_Owner == me ||','0 ||'),('layer log','screen->LayerInfo.Lock.ss_Owner == me ||','0 ||'),('private layer log','(window && WindowCompositionLayerInfo(window)->Lock.ss_Owner == me)','0'),('stale publication','WindowFrameContext(window) == CurrentScreenRenderContext(screen)','1')]
# Frame-stale negative uses bound check independently: remove bound in this
# mutant as well, then the actual combined guard must still reject frame stale.
with tempfile.TemporaryDirectory(prefix='firstwindow-host-') as td:
 td=pathlib.Path(td)
 def run(c,label):
  src=td/'test.c';binary=td/'test';src.write_text(c)
  build=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-g',str(src),'-o',str(binary)],capture_output=True,text=True)
  if build.returncode:raise RuntimeError(label+' compile: '+build.stderr)
  return subprocess.run([str(binary)],capture_output=True,text=True)
 result=run(code,'production');print(result.stdout,end='');assert result.returncode==0,result.stderr
 for label,old,new in mutations:
  assert old in code,(label,old)
  changed=code.replace(old,new,1)
  if label=='input task log':changed=changed.replace('me->tc_Node.ln_Type != NT_PROCESS ||','0 ||',1)
  if label=='layer log':changed=changed.replace('if (other->LayerInfo.Lock.ss_Owner == me)','if (0)',1)
  if label=='private layer log':changed=changed.replace('if (WindowCompositionLayerInfo(otherWindow)->Lock.ss_Owner == me)','if (0)',1)
  if label=='stale publication':changed=changed.replace('(!expected || BoundScreenRenderContext(expected, screen))','1',1)
  result=run(changed,label);assert result.returncode!=0,'undetected mutation: '+label
  print('REJECTED mutation:',label)
print('PASS source boundary ordering;12 actual-body mutations rejected; no native/GUI/SMP qualification')
