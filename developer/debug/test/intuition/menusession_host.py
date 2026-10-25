#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Menu session lifetime: the actual start/recovery code under ASan/UBSan.

Compiled from the actual sources: StartMenus, FireMenuMessage,
MenuHandlerReady, MenuHandlerGone, AbandonMenuHandler and
CloseOrphanMenuWindows (inputhandler_support.c), the menu message functions
(menus.c, menus.h) and the whole menu generation (menuscale.c:
PrepareMenuScale, CommitMenuScaleState, DisposeMenuScale).

Modelled boundaries: an Exec scheduler on one CPU with input.device (pri 20),
the menu Task (pri 10) and Wanderer (pri 0) as cooperative continuations, the
SignalSemaphore hand-off, PutMsg/GetMsg, Intuition actions (none run while a
system gadget is active), the menu Task's message loop (prepare, read the
borrowed strings as RenderMenuTitle/RenderItem do, OpenWindow/ShowWindow as
an action, commit, dispose, IESUBCLASS_MENUSTOP) and Zune's
MUIM_Window_UpdateMenu (ClearMenuStrip, FreeMenus, CreateMenus,
SetMenuStrip). The rest of the input handler is not compiled.

The parent variant runs the parent's actual MENUDOWN start statements and
FireMenuMessage (ce2e6ff82a): ASan reports the heap use after free, the
system gadget case deadlocks and a crashed menu Task leaves the session
active for good.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, runpy, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[4]
PARENT = 'ce2e6ff82a'
SCENARIOS = ['session', 'race', 'reopen', 'scales', 'sysgadget', 'crashed',
             'crashed-layer-lock', 'crashed-ibase-lock', 'live-waiter']

menuscale = runpy.run_path(str(Path(__file__).with_name('menuscale_host.py')), run_name='menusession_adapter')
function = menuscale['function']

def replace(text, old, new):
    assert old in text, old
    return text.replace(old, new, 1)

TYPES = r'''
#include <unistd.h>
#define TS_WAIT 4
#define IECLASS_EVENT 0x16
#define IECLASS_RAWMOUSE 2
#define IECODE_NOBUTTON 0xff
#define MENUDOWN 0x69
#define MENUUP 0xe9
#define SELECTUP 0xe8
#define MENUNULL 0xffff
#define IDCMP_MENUPICK 0x100
#define IDCMP_CHANGESCALE 0x40000000
#define RESOURCE_WINDOW 1
struct Node { struct Node *ln_Succ, *ln_Pred; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; };
struct Task { UBYTE tc_State; ULONG tc_SigWait; const char *name; };
struct SignalSemaphore { struct Task *ss_Owner; WORD ss_NestCount; };
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; };
struct MsgPort { struct Task *mp_SigTask; struct List mp_MsgList; };
struct Interrupt { APTR is_Data; };
struct Layer_Info { struct SignalSemaphore Lock; };
struct aros_timeval { ULONG tv_secs, tv_micro; };
struct InputEvent { struct aros_timeval ie_TimeStamp; UWORD ie_Class, ie_Code; };
struct Window;
struct IntuiMessage { struct Message ExecMessage; ULONG Class; UWORD Code; struct Window *IDCMPWindow; };
struct IIHData { struct Gadget *ActiveGadget, *ActiveSysGadget; struct Window *MenuWindow; struct Task *OrphanMenuTask; };
'''

INTUITIONBASE = r'''struct IntuitionBase { struct SignalSemaphore MenuLock, GadgetLock, WindowLock, IntuiActionLock, ViewLordLock,
 PubScrListLock, ScreenNotificationListLock, WinDecorSem, ScrDecorSem, MenuDecorSem;
 struct SignalSemaphore *IBaseLock; struct MsgPort *MenuHandlerPort; BOOL MenusActive; struct Interrupt *InputHandler;
 struct Screen *FirstScreen; struct Library *UtilityBase; struct GfxBase *GfxBase; struct LayersBase *LayersBase; };
#define IntIntuitionBase IntuitionBase
'''

SCHEDULER = r'''
static struct Task tInput={.name="input.device"}, tWanderer={.name="Wanderer"}, tOther={.name="other"};
static struct Task menuTasks[4]; static int menuTaskCount;
static struct Task *cur=&tInput;
static int deadlock_exit(const char *what){fprintf(stderr,"DEADLOCK: %s waits forever\n",what);fflush(stderr);_exit(3);}
static void NEWLIST(struct List *l){l->lh_Head=(struct Node*)&l->lh_Tail;l->lh_Tail=NULL;l->lh_TailPred=(struct Node*)&l->lh_Head;}
static void AddTail(struct List *l,struct Node *n){n->ln_Succ=(struct Node*)&l->lh_Tail;n->ln_Pred=l->lh_TailPred;l->lh_TailPred->ln_Succ=n;l->lh_TailPred=n;}
static void Remove(struct Node *n){n->ln_Pred->ln_Succ=n->ln_Succ;n->ln_Succ->ln_Pred=n->ln_Pred;}
static int IsEmpty(struct List *l){return l->lh_TailPred==(struct Node*)&l->lh_Head;}
static struct Message *GetMsg(struct MsgPort *p){if(IsEmpty(&p->mp_MsgList))return NULL;struct Node *n=p->mp_MsgList.lh_Head;Remove(n);return (struct Message*)n;}
static void Forbid(void){} static void Permit(void){}
static int replied;
static void ReplyMsg(struct Message *m){(void)m;replied++;}
static int run_other(void);
static void ObtainSemaphore(struct SignalSemaphore *s){
 while(s->ss_Owner&&s->ss_Owner!=cur) if(!run_other()) deadlock_exit(cur->name);
 s->ss_Owner=cur;s->ss_NestCount++;}
static BOOL AttemptSemaphore(struct SignalSemaphore *s){if(s->ss_Owner&&s->ss_Owner!=cur)return FALSE;s->ss_Owner=cur;s->ss_NestCount++;return TRUE;}
static void ReleaseSemaphore(struct SignalSemaphore *s){assert(s->ss_Owner==cur&&s->ss_NestCount>0);if(!--s->ss_NestCount)s->ss_Owner=NULL;}
static struct SignalSemaphore ibaselock;
static struct IntuitionBase ib; static struct IIHData iih; static struct Interrupt handler={&iih};
static struct Screen screen;
static struct MsgPort menuPorts[4], scalePorts[4];
static int handlersCreated;
static void handler_started(void);
static BOOL InitDefaultMenuHandler(struct IntuitionBase *base){
 assert(base==&ib&&ib.IBaseLock->ss_Owner==cur&&menuTaskCount<4);
 struct Task *t=&menuTasks[menuTaskCount];t->tc_State=TS_WAIT;t->tc_SigWait=0x300;t->name="menu handler";
 menuPorts[menuTaskCount].mp_SigTask=t;NEWLIST(&menuPorts[menuTaskCount].mp_MsgList);
 scalePorts[menuTaskCount].mp_SigTask=t;NEWLIST(&scalePorts[menuTaskCount].mp_MsgList);
 ib.MenuHandlerPort=&menuPorts[menuTaskCount++];handlersCreated++;handler_started();return TRUE;}
static void PutMsg(struct MsgPort *p,struct Message *m){assert(p);AddTail(&p->mp_MsgList,&m->mn_Node);}
static int picks; static UWORD lastPick; static struct Window *lastPickWindow;
static BOOL ih_fire_intuimessage(struct Window *w,ULONG cls,UWORD code,APTR address,struct IntuitionBase *base){
 (void)address;(void)base;assert(cur==&tInput);if(cls==IDCMP_MENUPICK){picks++;lastPick=code;lastPickWindow=w;}return TRUE;}
'''

WINDOWS = r'''
#define WLAYER(w) ((w)->wlayer)
#define BLAYER(w) ((w)->borderlayer)
static struct Window *openWindows[16]; static int openCount, closedCount;
static BOOL ResourceExisting(APTR r,UWORD type,struct IntuitionBase *base){(void)base;assert(type==RESOURCE_WINDOW);
 for(int i=0;i<openCount;i++)if(openWindows[i]==r)return TRUE;return FALSE;}
static struct Layer_Info *WindowCompositionLayerInfo(struct Window *w){return &w->WScreen->LayerInfo;}
static void link_window(struct Window *w){w->WScreen=&screen;w->NextWindow=screen.FirstWindow;screen.FirstWindow=w;openWindows[openCount++]=w;}
static BOOL ModifyIDCMP(struct Window *w,ULONG flags){assert(cur==&tInput&&!flags);w->IDCMPFlags=0;return TRUE;}
static void CloseWindow(struct Window *w){
 assert(cur==&tInput);
 /* CloseWindow() replies and deletes window->UserPort from the calling Task. */
 assert(!w->UserPort);
 /* DeleteLayer() would wait for these locks forever. */
 assert(!w->wlayer||!w->wlayer->Lock.ss_Owner||w->wlayer->Lock.ss_Owner==cur);
 assert(!screen.LayerInfo.Lock.ss_Owner&&!ib.IBaseLock->ss_Owner);
 struct Window **at=&screen.FirstWindow;while(*at!=w)at=&(*at)->NextWindow;*at=w->NextWindow;
 for(int i=0;i<openCount;i++)if(openWindows[i]==w){openWindows[i]=openWindows[--openCount];break;}
 closedCount++;}
'''

MODEL = r'''
/* ---- Zune owner (Wanderer): MUIM_Window_UpdateMenu ---- */
static struct Window owner;
struct Strip { struct Menu menu; struct MenuItem items[2]; struct IntuiText texts[2]; char *names[3]; };
static int stripGeneration;
static struct Menu *make_strip(void){
 struct Strip *s=calloc(1,sizeof *s);char buffer[32];stripGeneration++;
 snprintf(buffer,sizeof buffer,"Window %d",stripGeneration);s->names[0]=strdup(buffer);
 snprintf(buffer,sizeof buffer,"Open %d",stripGeneration);s->names[1]=strdup(buffer);
 snprintf(buffer,sizeof buffer,"Rename %d",stripGeneration);s->names[2]=strdup(buffer);
 for(int i=0;i<2;i++){s->texts[i].LeftEdge=2;s->texts[i].TopEdge=1;s->texts[i].IText=s->names[i+1];
  s->items[i].TopEdge=16*i;s->items[i].Width=80;s->items[i].Height=16;s->items[i].Flags=ITEMTEXT|ITEMENABLED|CHECKIT;
  s->items[i].ItemFill=&s->texts[i];s->items[i].NextSelect=MENUNULL;}
 s->items[0].NextItem=&s->items[1];
 s->menu.Width=60;s->menu.Height=13;s->menu.MenuName=s->names[0];s->menu.Flags=MENUENABLED;s->menu.FirstItem=&s->items[0];
 return &s->menu;}
static void free_strip(struct Menu *m){if(!m)return;struct Strip *s=(struct Strip*)m;for(int i=0;i<3;i++)free(s->names[i]);free(s);}
static int wanderer; static struct Menu *oldStrip,*newStrip;
static int wanderer_step(void){
 struct Task *saved=cur;int progressed=1;cur=&tWanderer;
 switch(wanderer){
 case 1: /* inside ClearMenuStrip(), holding MenuLock */
  assert(ib.MenuLock.ss_Owner==&tWanderer);oldStrip=owner.MenuStrip;owner.MenuStrip=NULL;ReleaseSemaphore(&ib.MenuLock);wanderer=2;break;
 case 5: /* ClearMenuStrip() waits for MenuLock */
  if(ib.MenuLock.ss_Owner&&ib.MenuLock.ss_Owner!=&tWanderer){progressed=0;break;}
  ObtainSemaphore(&ib.MenuLock);oldStrip=owner.MenuStrip;owner.MenuStrip=NULL;ReleaseSemaphore(&ib.MenuLock);wanderer=2;break;
 case 2: free_strip(oldStrip);oldStrip=NULL;wanderer=3;break;              /* FreeMenus() */
 case 3: newStrip=make_strip();wanderer=4;break;                            /* CreateMenus(), LayoutMenus() */
 case 4: /* SetMenuStrip() */
  if(ib.MenuLock.ss_Owner&&ib.MenuLock.ss_Owner!=&tWanderer){progressed=0;break;}
  ObtainSemaphore(&ib.MenuLock);owner.MenuStrip=newStrip;owner.MenuRenderContext=screen.context;newStrip=NULL;
  ReleaseSemaphore(&ib.MenuLock);wanderer=0;break;
 default: progressed=0;}
 cur=saved;return progressed;}
static void wanderer_inside_clearmenustrip(void){assert(!ib.MenuLock.ss_Owner);ib.MenuLock.ss_Owner=&tWanderer;ib.MenuLock.ss_NestCount=1;wanderer=1;}

/* ---- the menu Task: DefaultMenuHandler()'s message loop ---- */
enum { M_IDLE, M_WAIT_ACTION, M_OPEN, M_DEAD };
static int menuState=M_IDLE; static struct MenuScaleGeneration *generation; static struct Window *sessionWindow;
static int sessions, refused, renders;
/* A new handler Task starts idle; a dead one keeps its generation for good. */
static void handler_started(void){menuState=M_IDLE;generation=NULL;}
static struct MsgPort *menu_port(void){return ib.MenuHandlerPort;}
/* Each handler's menu bar popup; an abandoned handler's (index 0 here) stays. */
static struct Window bars[4];
#define bar (bars[menu_port()?menu_port()-menuPorts:0])
static struct Task *menu_task(void){return menu_port()?menu_port()->mp_SigTask:NULL;}
static void render(void){
 /* RenderMenuTitle()/RenderItem(): strlen() and Text() on the borrowed strings. */
 for(struct Menu *m=MenuScaleView(generation)->menu;m;m=m->NextMenu){
  renders+=(int)strlen(m->MenuName);
  for(struct MenuItem *i=m->FirstItem;i;i=i->NextItem)
   for(struct IntuiText *t=i->ItemFill;t;t=t->NextText)renders+=(int)strlen(t->IText);}}
static struct InputEvent stopEvent; static int stopPending;
static void make_menus_inactive(UWORD pick){
 /* MH2Int_MakeMenusInactive(): IND_WRITEEVENT, input.device runs it next round. */
 assert(cur==menu_task());stopPending=1;stopEvent.ie_Code=pick;}
static void end_session(void){
 /* EndMenuScale(): commit, close the popups, dispose, MENUSTOP. */
 render();CommitMenuScaleState(generation);
 if(ResourceExisting(&bar,RESOURCE_WINDOW,&ib)){bar.UserPort=NULL;cur=&tInput;CloseWindow(&bar);cur=menu_task();}
 DisposeMenuScale(generation);generation=NULL;menuState=M_IDLE;make_menus_inactive(MENUNULL);}
static int menu_step(void){
 struct Task *task=menu_task(),*saved=cur;
 if(!task||menuState==M_DEAD||menuState==M_WAIT_ACTION||cur==task)return 0;
 struct MenuMessage *msg=(struct MenuMessage*)GetMsg(menu_port());
 if(!msg)return 0;
 cur=task;
 if(msg->code==MMCODE_START){
  assert(menuState==M_IDLE&&!generation);sessionWindow=msg->win;
  generation=PrepareMenuScale(msg->win,&ib);
  if(!generation){refused++;make_menus_inactive(MENUNULL);}
  else {sessions++;render();menuState=M_WAIT_ACTION;}   /* MakeMenuBarWin(): OpenWindow() */
 } else if(menuState==M_OPEN&&msg->code==MMCODE_EVENT&&msg->ie.ie_Code==MENUUP){end_session();}
 FreeMenuMessage(msg,&ib);                                 /* ReplyMenuMessage() */
 cur=saved;return 1;}
static int run_other(void){return menu_step()||wanderer_step();}

/* ---- input.device ---- */
static int roundsWaiting;
static void input_round(void){
 assert(cur==&tInput);
 /* HandleIntuiActions(): nothing runs while a system gadget is active. */
 if(menuState==M_WAIT_ACTION){
  if(iih.ActiveSysGadget){if(++roundsWaiting>8)deadlock_exit("menu handler (OpenWindow action)");}
  else {roundsWaiting=0;menuState=M_OPEN;
   bar.UserPort=&scalePorts[menu_port()-menuPorts];bar.IDCMPFlags=IDCMP_CHANGESCALE;link_window(&bar);}}
 CloseOrphanMenuWindows(&iih,&ib);
 if(stopPending){stopPending=0;
  /* IECLASS_MENU / IESUBCLASS_MENUSTOP in IntuiInputHandler() */
  if(ib.MenusActive){iih.MenuWindow=NULL;ib.MenusActive=FALSE;ReleaseSemaphore(&ib.MenuLock);
   ih_fire_intuimessage(&owner,IDCMP_MENUPICK,stopEvent.ie_Code,&owner,&ib);}}
 while(run_other());}
static struct Gadget { int unused; } dragbar;
static void input_event(UWORD code){
 struct InputEvent ie={{1,0},IECLASS_RAWMOUSE,code};
 assert(cur==&tInput);
 if(ib.MenusActive){FireMenuMessage(MMCODE_EVENT,0,&ie,&ib);}
 else if(code==MENUDOWN){START_MENUS(&iih,&owner,NULL,&ib);}
 else if(code==SELECTUP&&iih.ActiveSysGadget){iih.ActiveSysGadget=NULL;}
 input_round();}
static void tick(void){input_round();}
static void settle(void){for(int i=0;i<4;i++)input_round();}

/* ---- scene ---- */
static struct TextFont base={.tf_Message.mn_Node.ln_Name="base.font",.tf_YSize=13,.tf_XSize=7};
static struct TextFont fonts[3];
static struct Image check={.Width=10,.Height=13},key={.Width=14,.Height=14},arrow={.Width=7,.Height=13};
static struct IntuitionCustomize custom={&arrow};static UWORD pens[8];
static struct IntDrawInfo logical,physical[3];
static struct ScreenRenderContext contexts[3];
static void publish_scale(int k){screen.context=&contexts[k];}
static void scene(void){
 NEWLIST(&scalePorts[0].mp_MsgList);ib.IBaseLock=&ibaselock;ib.InputHandler=&handler;ib.FirstScreen=&screen;
 int scales[3]={100,200,300};
 logical=(struct IntDrawInfo){.dri_Font=&base,.dri_CheckMark=&check,.dri_AmigaKey=&key,.dri_Pens=pens,.dri_Customize=&custom,.dri_Screen=&screen};
 for(int k=0;k<3;k++){fonts[k]=base;fonts[k].tf_YSize=13*scales[k]/100;fonts[k].tf_XSize=7*scales[k]/100;
  physical[k]=logical;physical[k].dri_Font=&fonts[k];
  contexts[k]=(struct ScreenRenderContext){.src_Scale=scales[k],.src_LegacyScaleFactor=scales[k]/100,.src_PhysicalBarHeight=17*scales[k]/100,
   .src_LogicalBarVBorder=1,.src_LogicalMenuHBorder=2,.src_LogicalMenuVBorder=1,.src_PhysicalMenuHBorder=2*scales[k]/100,.src_PhysicalMenuVBorder=scales[k]/100,
   .src_LogicalDrawInfo=(struct DrawInfo*)&logical,.src_PhysicalDrawInfo=(struct DrawInfo*)&physical[k]};}
 screen.Width=2560;screen.Height=1600;publish_scale(0);
 owner=(struct Window){.MenuRenderLogical=TRUE,.MenuRenderContext=&contexts[0],.Checkmark=&check,.AmigaKey=&key,.SubMenuImage=&arrow};
 link_window(&owner);owner.MenuStrip=make_strip();
 assert(AttemptSemaphore(ib.IBaseLock));InitDefaultMenuHandler(&ib);ReleaseSemaphore(ib.IBaseLock);}
static void open_and_close(void){
 int before=sessions;input_event(MENUDOWN);tick();assert(ib.MenusActive&&menuState==M_OPEN&&sessions==before+1);
 input_event(MENUUP);settle();assert(!ib.MenusActive&&menuState==M_IDLE&&!ib.MenuLock.ss_Owner&&lastPick==MENUNULL);}
static void clean(void){assert(!generation&&!ib.MenusActive&&!ib.MenuLock.ss_Owner);
 /* Every allocation of the generation and every menu message is gone. */
 assert(live==0);}

static void crash_menu_task(void){struct Task *t=menu_task();t->tc_State=TS_WAIT;t->tc_SigWait=0;menuState=M_DEAD;
 /* A IDCMP_CHANGESCALE for the bar is still queued at its port. */
 static struct IntuiMessage pending;pending.Class=IDCMP_CHANGESCALE;pending.IDCMPWindow=&bar;AddTail(&bar.UserPort->mp_MsgList,&pending.ExecMessage.mn_Node);}
static void crashed_case(int layerlock,int ibaselocked){
 input_event(MENUDOWN);tick();assert(ib.MenusActive&&menuState==M_OPEN);
 struct Task *dead=menu_task();crash_menu_task();
 /* Two events were queued before the crash. */
 int queued=live;
 for(int i=0;i<2;i++){struct MenuMessage *m=AllocMenuMessage(&ib);assert(m);m->code=MMCODE_EVENT;SendMenuMessage(m,&ib);}
 if(layerlock){static struct Layer layer;layer.Lock.ss_Owner=dead;layer.Lock.ss_NestCount=1;bar.wlayer=&layer;}
 if(ibaselocked){ib.IBaseLock->ss_Owner=dead;ib.IBaseLock->ss_NestCount=1;}
 /* Zune rebuilds its strip meanwhile: ClearMenuStrip() waits for MenuLock. */
 wanderer=5;assert(!wanderer_step()&&owner.MenuStrip);
 int r=replied,p=picks;
 input_event(IECODE_NOBUTTON);
 assert(!ib.MenusActive&&!iih.MenuWindow&&picks==p+1&&lastPick==MENUNULL&&lastPickWindow==&owner);
 assert(!ib.MenuHandlerPort&&IsEmpty(&menuPorts[0].mp_MsgList)&&live==queued);
 settle();
 /* The released MenuLock lets the owner install its new strip. */
 assert(wanderer==0&&owner.MenuStrip);
 if(layerlock||ibaselocked){assert(ResourceExisting(&bar,RESOURCE_WINDOW,&ib)&&closedCount==0);}
 else {assert(!ResourceExisting(&bar,RESOURCE_WINDOW,&ib)&&closedCount==1&&replied==r+1);}
 /* The next MENUDOWN starts a new handler, unless the dead one holds IBaseLock. */
 input_event(MENUDOWN);tick();
 if(ibaselocked){assert(!ib.MenusActive&&handlersCreated==1);return;}
 assert(handlersCreated==2&&ib.MenusActive&&menuState==M_OPEN&&menu_task()!=dead);
 input_event(MENUUP);settle();assert(!ib.MenusActive&&!ib.MenuLock.ss_Owner);}

int main(int argc,char **argv){
 const char *which=argc>1?argv[1]:"session";
 scene();
 if(!strcmp(which,"session")){open_and_close();open_and_close();clean();}
 else if(!strcmp(which,"race")){
  /* A left click changed the selection: Wanderer is inside ClearMenuStrip()
   * when the right button goes down. */
  for(int k=0;k<3;k++){publish_scale(k);
   wanderer_inside_clearmenustrip();input_event(MENUDOWN);
   input_event(MENUUP);settle();assert(wanderer==0&&owner.MenuStrip&&!ib.MenusActive);
   open_and_close();}
  assert(sessions>=3);clean();}
 else if(!strcmp(which,"reopen")){
  /* A second MENUDOWN while the first session is closing is an event of it. */
  input_event(MENUDOWN);input_event(MENUUP);input_event(MENUDOWN);input_event(MENUUP);settle();
  assert(sessions==1&&!ib.MenusActive);open_and_close();clean();}
 else if(!strcmp(which,"scales")){
  /* Open at 100 %, a live scale change ends the session, open at 200 % and 300 %. */
  input_event(MENUDOWN);tick();assert(ib.MenusActive&&menuState==M_OPEN);publish_scale(1);
  { struct Task *saved=cur;cur=menu_task();end_session();cur=saved; }
  settle();assert(!ib.MenusActive);
  open_and_close();
  publish_scale(2);wanderer_inside_clearmenustrip();input_event(MENUDOWN);input_event(MENUUP);settle();
  open_and_close();clean();}
 else if(!strcmp(which,"sysgadget")){
  /* MENUVERIFY round: a SELECTDOWN on a drag bar starts the drag before the replies arrive. */
  iih.ActiveSysGadget=(struct Gadget*)&dragbar;
  { struct InputEvent ie={{1,0},IECLASS_EVENT,0};START_MENUS(&iih,&owner,&ie,&ib); }
  input_round();input_event(IECODE_NOBUTTON);input_event(SELECTUP);
  for(int i=0;i<12;i++)input_round();
  assert(!iih.ActiveSysGadget&&!ib.MenusActive);open_and_close();clean();}
 else if(!strcmp(which,"crashed")){crashed_case(0,0);}
 else if(!strcmp(which,"crashed-layer-lock")){crashed_case(1,0);}
 else if(!strcmp(which,"crashed-ibase-lock")){crashed_case(0,1);}
 else if(!strcmp(which,"live-waiter")){
  /* A handler waiting for its ports (or inside DoSyncAction) is alive. */
  input_event(MENUDOWN);tick();struct Task *t=menu_task();t->tc_State=TS_WAIT;t->tc_SigWait=0x20;
  input_event(IECODE_NOBUTTON);assert(ib.MenusActive&&ib.MenuHandlerPort&&menu_task()==t);
  input_event(MENUUP);settle();assert(!ib.MenusActive);clean();}
 else return 2;
 free_strip(owner.MenuStrip);
 printf("PASS %s sessions=%d refused=%d handlers=%d closed=%d\n",which,sessions,refused,handlersCreated,closedCount);
 return 0;}
'''

def preamble():
    p = menuscale['PREAMBLE']
    p = replace(p, 'struct IntuitionBase { int MenuLock; struct Library *UtilityBase; struct GfxBase *GfxBase; struct LayersBase *LayersBase; void *IBaseLock; };\n', TYPES + INTUITIONBASE)
    p = replace(p, 'struct Screen { LONG MouseX,MouseY; int LayerInfo;', 'struct Screen { struct Window *FirstWindow; struct Screen *NextScreen; LONG MouseX,MouseY; struct Layer_Info LayerInfo;')
    p = replace(p, 'struct Window { struct Screen *WScreen;', 'struct Layer; struct Window { struct Window *NextWindow,*menulendwindow; struct MsgPort *UserPort; ULONG IDCMPFlags; struct Layer *wlayer,*borderlayer; struct Screen *WScreen;')
    p = replace(p, 'struct Layer { struct Window *Window; }; static struct Layer hit;', 'struct Layer { struct Window *Window; struct SignalSemaphore Lock; }; static struct Layer hit __attribute__((unused));')
    p = replace(p, 'static void ObtainSemaphore(void *s) { (void)s;assert(!iblock&&!lilock);iblock=1; }\n', '')
    p = replace(p, 'static void ReleaseSemaphore(void *s) { (void)s;assert(iblock&&!lilock);iblock=0; }\n', '')
    p = replace(p, 'static void LockLayerInfo(int *li)', 'static void LockLayerInfo(struct Layer_Info *li)')
    p = replace(p, 'static void UnlockLayerInfo(int *li)', 'static void UnlockLayerInfo(struct Layer_Info *li)')
    p = replace(p, 'static struct Layer *WhichLayer(int *li,LONG x,LONG y)', 'static struct Layer * __attribute__((unused)) WhichLayer(struct Layer_Info *li,LONG x,LONG y)')
    p = replace(p, 'static void SetAPen(struct RastPort *r, ULONG p)', 'static void __attribute__((unused)) SetAPen(struct RastPort *r, ULONG p)')
    return p

def menu_messages(menus_c, menus_h):
    header = re.sub(r'^#(?:include|ifn?def|endif)[^\n]*\n', '', menus_h, flags=re.M)
    header = header.replace('#define MENUS_H\n', '')
    return header + ''.join(function(menus_c, n) for n in ('AllocMenuMessage', 'FreeMenuMessage', 'SendMenuMessage'))

def menuscale_source():
    source = (ROOT/'rom/intuition/menuscale.c').read_text()
    source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
    header = (ROOT/'rom/intuition/menuscale.h').read_text()
    header = re.sub(r'^#(?:include|ifn?def|endif|define)[^\n]*\n', '', header, flags=re.M)
    return header + source

def production_start():
    support = (ROOT/'rom/intuition/inputhandler_support.c').read_text(encoding='latin1')
    names = ('MenuHandlerGone', 'AbandonMenuHandler', 'MenuHandlerReady', 'StartMenus', 'CloseOrphanMenuWindows')
    protos = 'BOOL FireMenuMessage(WORD code, struct Window *win, struct InputEvent *ie, struct IntuitionBase *IntuitionBase);\n'
    return protos + ''.join(function(support, n) for n in names) + function(support, 'FireMenuMessage') + '#define START_MENUS StartMenus\n'

def parent_start():
    support = subprocess.check_output(['git', '-C', str(ROOT), 'show', PARENT + ':rom/intuition/inputhandler_support.c']).decode('latin1')
    handler = subprocess.check_output(['git', '-C', str(ROOT), 'show', PARENT + ':rom/intuition/inputhandler.c']).decode('latin1')
    begin = handler.index('        else if (FireMenuMessage(MMCODE_START, w, NULL/*ie*/, IntuitionBase))')
    end = handler.index('MENUS_ACTIVE = TRUE;', begin)
    end = handler.index('}', end) + 1
    block = handler[begin:end]
    start = ('static BOOL ParentStart(struct IIHData *iihdata, struct Window *w, struct InputEvent *ie, struct IntuitionBase *IntuitionBase)\n'
             '{ (void)ie; if (0) {}\n' + block + '\n return MENUS_ACTIVE; }\n')
    noop = 'static void CloseOrphanMenuWindows(struct IIHData *iihdata, struct IntuitionBase *IntuitionBase){(void)iihdata;(void)IntuitionBase;}\n'
    return function(support, 'FireMenuMessage') + start + noop + '#define START_MENUS ParentStart\n'

def build(start):
    menus_c = (ROOT/'rom/intuition/menus.c').read_text(encoding='latin1')
    menus_h = (ROOT/'rom/intuition/menus.h').read_text(encoding='latin1')
    return preamble() + SCHEDULER + WINDOWS + menu_messages(menus_c, menus_h) + menuscale_source() + start + MODEL

MUTATIONS = {
    'lock-after-start': ('    ObtainSemaphore(&ib->MenuLock);\n    if (!FireMenuMessage(MMCODE_START, win, ie, IntuitionBase))\n    {\n        ReleaseSemaphore(&ib->MenuLock);\n        return FALSE;\n    }\n',
                         '    if (!FireMenuMessage(MMCODE_START, win, ie, IntuitionBase))\n        return FALSE;\n    ObtainSemaphore(&ib->MenuLock);\n'),
    'start-under-system-gadget': ('if (MENUS_ACTIVE || !win || iihdata->ActiveSysGadget ||', 'if (MENUS_ACTIVE || !win ||'),
    'any-waiter-is-gone': ('task->tc_State == TS_WAIT && task->tc_SigWait == 0;', 'task->tc_State == TS_WAIT;'),
    'no-event-recovery': ('    if (code != MMCODE_START && MenuHandlerGone(ib->MenuHandlerPort))\n', '    if (0)\n'),
    'lock-kept': ('        MENUS_ACTIVE = FALSE;\n        ReleaseSemaphore(&ib->MenuLock);\n', '        MENUS_ACTIVE = FALSE;\n'),
    'no-menunull': ('            ih_fire_intuimessage(eventwin, IDCMP_MENUPICK, MENUNULL, window, IntuitionBase);\n', ''),
    'queue-not-drained': ('    while ((msg = (struct MenuMessage *)GetMsg(port)))\n        FreeMenuMessage(msg, IntuitionBase);\n', '    (void)msg;\n'),
    'userport-kept': ('        orphans[i]->UserPort = NULL;\n', ''),
    'layer-lock-ignored': ('                (WLAYER(window) && WLAYER(window)->Lock.ss_Owner == task) ||\n', ''),
    'ibase-lock-ignored': ('        ib->IBaseLock, &ib->GadgetLock,', '        &ib->GadgetLock,'),
    'no-replacement': ('            InitDefaultMenuHandler(IntuitionBase);\n', '            ;\n'),
    'blocking-ibase': ('if (!ib->MenuHandlerPort && AttemptSemaphore(ib->IBaseLock))\n    {', 'if (!ib->MenuHandlerPort)\n    {\n        ObtainSemaphore(ib->IBaseLock);'),
    'orphan-not-recorded': ('    iihdata->OrphanMenuTask = port->mp_SigTask;\n', ''),
}

def compile_and_run(directory, name, body, scenarios):
    c = directory/(name+'.c'); exe = directory/name; c.write_text(body)
    cmd = [os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
           '-Wno-unused-variable', '-Wno-unused-function', '-Wno-tautological-compare', '-fno-omit-frame-pointer',
           '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(c), '-o', str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    (directory/(name+'.compile.log')).write_text(r.stdout + r.stderr)
    assert r.returncode == 0, name + ' does not compile:\n' + r.stderr[-3000:]
    out = {}
    for s in scenarios:
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=0', UBSAN_OPTIONS='halt_on_error=1')
        p = subprocess.run([str(exe), s], capture_output=True, text=True, env=env, timeout=60)
        (directory/f'{name}.{s}.log').write_text(p.stdout + p.stderr)
        kind = 'pass' if p.returncode == 0 else ('use-after-free' if 'heap-use-after-free' in p.stderr else
               'deadlock' if 'DEADLOCK' in p.stderr else 'assertion' if 'Assertion' in p.stderr else 'failure')
        out[s] = dict(returncode=p.returncode, verdict=kind, stdout=p.stdout.strip(),
                      first_error=next((l for l in p.stderr.splitlines() if 'ERROR' in l or 'Assertion' in l or 'DEADLOCK' in l), ''))
    return dict(name=name, source_sha256=hashlib.sha256(c.read_bytes()).hexdigest(), scenarios=out)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--output', type=Path); ap.add_argument('--mutations', action='store_true')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='menusession-') as temporary:
        directory = args.output or Path(temporary); directory.mkdir(parents=True, exist_ok=True)
        production = build(production_start())
        report = dict(production=compile_and_run(directory, 'production', production, SCENARIOS))
        bad = [s for s, r in report['production']['scenarios'].items() if r['verdict'] != 'pass']
        assert not bad, 'production fails: ' + json.dumps({s: report['production']['scenarios'][s] for s in bad}, indent=1)
        print('PASS actual start/recovery + menu generation: ' + ', '.join(SCENARIOS))
        parent = compile_and_run(directory, 'parent', build(parent_start()), ['race', 'sysgadget', 'crashed'])
        report['parent'] = parent
        assert parent['scenarios']['race']['verdict'] == 'use-after-free', parent['scenarios']['race']
        assert parent['scenarios']['sysgadget']['verdict'] == 'deadlock', parent['scenarios']['sysgadget']
        assert parent['scenarios']['crashed']['verdict'] != 'pass', parent['scenarios']['crashed']
        print('PASS parent %s: race=%s sysgadget=%s crashed=%s' % (PARENT, parent['scenarios']['race']['verdict'],
              parent['scenarios']['sysgadget']['verdict'], parent['scenarios']['crashed']['verdict']))
        if args.mutations:
            report['mutants'] = []
            for label, (old, new) in MUTATIONS.items():
                body = replace(production, old, new)
                r = compile_and_run(directory, 'mutant-' + label, body, SCENARIOS)
                killed = [s for s, v in r['scenarios'].items() if v['verdict'] != 'pass']
                r['killed_by'] = killed; report['mutants'].append(r)
                assert killed, 'mutant survived: ' + label
            print('PASS rejected %d actual-source mutants' % len(MUTATIONS))
        (directory/'results.json').write_text(json.dumps(report, indent=2) + '\n')

if __name__ == '__main__':
    main()
