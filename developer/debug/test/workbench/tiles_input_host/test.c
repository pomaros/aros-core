/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "input.c"
static CxObj brokerObject,customObject;static void (*callback)(CxMsg*,CxObj*);
static unsigned disposed,signals,freed,logged;
void kprintf(const char *fmt,...){va_list args;va_start(args,fmt);unsigned group=va_arg(args,unsigned);va_end(args);assert(strstr(fmt,"hotkey-disabled"));assert(group==0||group==2||group==3);++logged;}
static struct InputEvent currentEvent;
LONG CxMsgType(CxMsg *m){(void)m;return CXM_IEVENT;}
void *CxMsgData(CxMsg *m){(void)m;return &currentEvent;}
void DisposeCxMsg(CxMsg *m){(void)m;++disposed;}
LONG ParseIX(const char *text,IX *ix){memset(ix,0,sizeof(*ix));ix->ix_Class=IECLASS_RAWKEY;
 if(!strcmp(text,"lamiga d"))ix->ix_Code=32;else if(!strcmp(text,"esc"))ix->ix_Code=69;
 else if(!strcmp(text,"LAmiga C"))ix->ix_Code=51;else if(!strcmp(text,"mouse")){ix->ix_Class=IECLASS_RAWMOUSE;ix->ix_Code=IECODE_LBUTTON;}else return 1;return 0;}
LONG MatchIX(struct InputEvent *e,IX *ix){return e->ie_Class==ix->ix_Class&&e->ie_Code==ix->ix_Code;}
BYTE AllocSignal(LONG n){(void)n;return 4;}void FreeSignal(LONG n){assert(n==4);++freed;}
struct Task *FindTask(void *v){(void)v;return (struct Task*)1;}
void Signal(struct Task *t,ULONG bits){assert(t==(struct Task*)1&&bits==16);++signals;}
ULONG SetSignal(ULONG a,ULONG b){assert(!a&&b==16);return 0;}
CxObj *CxBroker(struct NewBroker *b,LONG *e){(void)e;assert(b->nb_Unique==NBU_UNIQUE&&!b->nb_Port);return &brokerObject;}
CxObj *mock_custom(void (*fn)(CxMsg*,CxObj*)){callback=fn;return &customObject;}
void AttachCxObj(CxObj*a,CxObj*b){assert(a==&brokerObject&&b==&customObject);}
LONG CxObjError(CxObj*c){(void)c;return 0;}
LONG ActivateCxObj(CxObj*c,LONG a){(void)c;(void)a;return 0;}
void DeleteCxObjAll(CxObj*c){assert(c==&brokerObject);callback=NULL;}
static void event(unsigned cls,unsigned code,unsigned qual){CxMsg msg=0;currentEvent=(struct InputEvent){cls,code,qual};callback(&msg,NULL);}
int main(void){struct Screen screen={12,34};const char *keys[]={"LAmiga C","NONE"};struct TileInputEvent out;
 struct TileInput *input=TileInputOpen(&screen,keys,2);assert(input&&TileInputSignals(input)==16);
 assert(!TileInputOpen(&screen,keys,2));
 event(IECLASS_RAWKEY,51,0);assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_GROUP&&out.group==0);
 event(IECLASS_RAWKEY,51,IEQUALIFIER_REPEAT);event(IECLASS_RAWKEY,51|IECODE_UP_PREFIX,0);assert(disposed==3&&!TileInputNext(input,&out));
 event(IECLASS_RAWKEY,69,0);assert(disposed==3&&!TileInputNext(input,&out));
 /* A window's pre-lift press must receive its release while lifted. */
 event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);assert(disposed==3);
 TileInputSetLifted(input,TRUE);
 event(IECLASS_RAWMOUSE,IECODE_LBUTTON|IECODE_UP_PREFIX,0);
 assert(disposed==3&&!TileInputNext(input,&out));
 /* Conversely our lifted press retains its matching release after drop. */
 event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);screen.MouseX=99;screen.MouseY=88;
 assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_CLICK&&out.x==12&&out.y==34);
 TileInputSetLifted(input,FALSE);event(IECLASS_RAWMOUSE,IECODE_LBUTTON|IECODE_UP_PREFIX,0);assert(disposed==5);
 event(IECLASS_RAWMOUSE,0xffff,0);assert(disposed==5); /* motion passes */
 TileInputSetLifted(input,TRUE);event(IECLASS_RAWKEY,69,0);assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_DROP);
 for(unsigned i=0;i<40;++i)event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);
 for(unsigned i=0;i<16;++i)assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_CLICK);
 assert(!TileInputNext(input,&out)&&signals==19&&disposed==46);
 event(IECLASS_RAWKEY,32,0);assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_ALL);
 TileInputClose(input);assert(freed==1&&!TileInputSignals(input)&&!callback);TileInputClose(input);assert(freed==1);
 char oversized[80];memset(oversized,'x',sizeof(oversized)-1);oversized[79]=0;
 const char *mixed[]={"bad key","LAmiga C",oversized,"mouse","NoNe","",NULL};
 input=TileInputOpen(&screen,mixed,7);assert(input&&logged==3&&freed==1);
 assert(!state.enabled[0]&&state.enabled[1]&&!state.enabled[2]&&!state.enabled[3]&&!state.enabled[4]&&!state.enabled[5]&&!state.enabled[6]);
 event(IECLASS_RAWKEY,32,0);assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_ALL);
 event(IECLASS_RAWKEY,32|IECODE_UP_PREFIX,0);
 event(IECLASS_RAWKEY,51,0);assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_GROUP&&out.group==1);
 event(IECLASS_RAWKEY,51|IECODE_UP_PREFIX,0);
 unsigned beforeDisabled=disposed;event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);
 assert(disposed==beforeDisabled&&!TileInputNext(input,&out));
 TileInputClose(input);assert(freed==2);
 const char *disabled[]={"none","NONE",NULL,""};input=TileInputOpen(&screen,disabled,4);assert(input&&logged==3);
 event(IECLASS_RAWKEY,32,0);assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_ALL);
 TileInputClose(input);assert(freed==3);
 input=TileInputOpen(&screen,NULL,0);assert(input);
 struct TileInputPanel panels[]={{1,-10,0,40,40},{2,10,10,40,40}};
 assert(TileInputSetPanels(input,panels,2));
 screen.MouseX=15;screen.MouseY=15;unsigned oldDisposed=disposed;
 event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);
 assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_PANEL_CLICK&&out.group==2&&out.x==15&&out.y==15&&disposed==oldDisposed+1);
 assert(TileInputSetPanels(input,NULL,0));event(IECLASS_RAWMOUSE,IECODE_LBUTTON|IECODE_UP_PREFIX,0);assert(disposed==oldDisposed+2);
 assert(TileInputSetPanels(input,panels,2));screen.MouseX=0;screen.MouseY=5;
 event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);assert(TileInputNext(input,&out)&&out.group==1);
 event(IECLASS_RAWMOUSE,IECODE_LBUTTON|IECODE_UP_PREFIX,0);
 screen.MouseX=50;screen.MouseY=50;oldDisposed=disposed;
 event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);event(IECLASS_RAWMOUSE,IECODE_LBUTTON|IECODE_UP_PREFIX,0);
 assert(disposed==oldDisposed&&!TileInputNext(input,&out)); /* exclusive right/bottom */
 screen.MouseX=15;screen.MouseY=15;
 __atomic_add_fetch(&state.panelSequence,1,__ATOMIC_SEQ_CST); /* publication in progress */
 event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);assert(disposed==oldDisposed&&!TileInputNext(input,&out));
 __atomic_add_fetch(&state.panelSequence,1,__ATOMIC_SEQ_CST);
 panels[0].width=0;assert(!TileInputSetPanels(input,panels,2));
 assert(!TileInputSetPanels(input,panels,9));
 event(IECLASS_RAWMOUSE,IECODE_RBUTTON,0);assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_PANEL_DROP&&out.group==2);
 event(IECLASS_RAWMOUSE,IECODE_RBUTTON|IECODE_UP_PREFIX,0);
 TileInputSetLifted(input,TRUE);event(IECLASS_RAWMOUSE,IECODE_LBUTTON,0);assert(TileInputNext(input,&out)&&out.kind==TILE_INPUT_CLICK);
 TileInputClose(input);assert(freed==4);
 puts("tiles-input-host PASS");}
