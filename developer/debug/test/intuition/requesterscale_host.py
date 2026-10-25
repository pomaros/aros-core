#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual OS-owned requester preparation/install/paint/lifetime under sanitizers.
Exec, Intuition publication and raster/font services are external adapters.
This is not a native font, input scheduler or display pixel qualification.
"""
from pathlib import Path
import subprocess,tempfile,re,json,sys
root=Path(__file__).resolve().parents[4]
production=(root/'rom/intuition/systemrequestscale.c').read_text()
production=re.sub(r'^#include[^\n]*\n','',production,flags=re.M)
for name in ['OpenDiskFont','OpenLibrary','OpenFont','NewScaledDiskFont']:
    assert name not in production
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <setjmp.h>
typedef int32_t LONG;typedef uint32_t ULONG;typedef int64_t QUAD;typedef uintptr_t IPTR;typedef unsigned short UWORD;typedef short WORD;typedef unsigned char UBYTE;typedef char BYTE;typedef int BOOL;typedef void*Object;typedef char*STRPTR;typedef const char*CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_ANY 0
#define MEMF_CLEAR 1
#define NT_PROCESS 13
#define TAG_DONE 0
#define FRBUTTONCLASS "frbuttonclass"
#define JAM1 1
#define JAM2 2
#define BACKGROUNDPEN 0
#define TEXTPEN 1
#define SHINEPEN 2
#define SHADOWPEN 3
#define GFLG_SELECTED 128
#define GFLG_GADGIMAGE 4
#define GFLG_GADGHIMAGE 2
#define IDCMP_GADGETUP 1
#define IDCMP_RAWKEY 2
#define IDCMP_CHANGESCALE 4
#define IDCMP_REFRESHWINDOW 8
#define IDCMP_VANILLAKEY 16
#define SA_RenderContext 164
#define WDA_PreparedScaleWindow 5
#define IS(screen) (&providerScreen)
static struct {Object WinDecorObj;} providerScreen;
#define ExtGadget Gadget
enum{WA_CustomScreen=1,WA_Title,WA_Width,WA_Height,WA_Left,WA_Top,WA_DragBar,WA_DepthGadget,WA_RMBTrap,WA_Activate,WA_Hidden,WA_ScaleAware,WA_RenderContext,WA_ScaleGeometry,WA_IDCMP,WA_AutoAdjust,GA_ID,GA_Text,GA_RelVerify};
struct TagItem{IPTR ti_Tag,ti_Data;};
struct Rectangle{WORD MinX,MinY,MaxX,MaxY;};
struct TextFont{UWORD tf_YSize,tf_Baseline;};
struct TextExtent{int unused;};
struct Layer;struct RastPort{struct TextFont*Font;LONG x,y,w,h;ULONG*pixel;struct Layer*Layer;void*BitMap;};
struct Layer{struct RastPort*rp;};
struct Requester{struct Requester*OlderRequest;struct Layer*ReqLayer;LONG LeftEdge,TopEdge,Width,Height;};
struct Border{WORD LeftEdge,TopEdge;UBYTE FrontPen,BackPen,DrawMode,Count;WORD*XY;struct Border*NextBorder;};
struct Gadget{struct Gadget*NextGadget;LONG LeftEdge,TopEdge,Width,Height,BoundsLeftEdge,BoundsTopEdge,BoundsWidth,BoundsHeight;void*GadgetRender,*SelectRender;UWORD Flags,GadgetID,Activation,GadgetType;CONST_STRPTR label;};
struct Screen;struct DrawInfo{struct Screen*dri_Screen;struct TextFont*dri_Font;UWORD*dri_Pens;};
struct ScreenScaleMetrics{LONG WBorTop,WBorLeft,WBorRight,WBorBottom;};
struct ScreenRenderContext{UWORD src_Scale;struct DrawInfo*src_PhysicalDrawInfo;UWORD src_LegacyScaleFactor;struct DrawInfo*src_LogicalDrawInfo;struct ScreenScaleMetrics metrics;};
struct Screen{LONG Width,Height;const struct ScreenRenderContext*current;LONG LeftEdge,TopEdge,WBorTop,WBorLeft,WBorRight,WBorBottom;struct RastPort RastPort;struct{LONG DWidth,DHeight;}ViewPort;struct Layer*BarLayer;};
struct Window{void*UserPort;struct Screen*WScreen;struct Gadget*FirstGadget;LONG Width,Height,BorderLeft,BorderTop,BorderRight,BorderBottom;BYTE*UserData;struct RastPort*RPort;const struct ScreenRenderContext*expected;BOOL hidden,active,ScaleAware;ULONG Flags;struct RastPort*BorderRPort;struct Requester*FirstRequest;};
struct WindowFitGeometry{LONG left,top,width,height;};
struct ScaleWindowGeometry{LONG Left,Top,Width,Height,MinWidth,MinHeight,MaxWidth,MaxHeight;};
struct IntuitionBase;
struct IntReqDims{UWORD gadgets,offy,offx;};
struct IntRequestUserData{ULONG IDCMP;struct IntReqDims ReqDims;struct Gadget*Gadgets;void*ReqBody;void(*drawreq)(struct IntReqDims*,void*,struct Window*,struct Screen*,struct Gadget*,struct IntuitionBase*);};
struct IntuiText{struct IntuiText*NextText;void*ITextFont;WORD LeftEdge,TopEdge;STRPTR IText;UBYTE FrontPen,BackPen,DrawMode;};
struct IntuiMessage{ULONG Class;void*IAddress;UWORD Code,Qualifier;};
struct Node{int ln_Type;};struct Task{struct Node tc_Node;};
struct GfxBase{int unused;};struct Interrupt{void*is_Data;};
struct IntuitionBase{struct GfxBase*GfxBase;void*ReqFont,*KeymapBase,*UtilityBase;struct Interrupt*InputHandler;struct{char ic_ReqTrue,ic_ReqFalse;}IControlPrefs;};
struct PreparedSystemRequesterScale;
static BOOL HandleScaledSystemRequesterEvent(struct Window*,struct IntuiMessage*,struct IntuitionBase*);
void DetachSystemRequesterScale(struct Window*,struct IntuitionBase*);
#define GetPrivIBase(b) (b)
static struct Task task={{NT_PROCESS}};
static ULONG allocations,failat,live,shown,activated,closed,hiddenopens,paintcalls;
static unsigned locked;
static LONG positioned_x,positioned_y;
static BOOL stale_open,stale_show;
static const struct ScreenRenderContext*raced_context;
static struct TextFont f13={13,10},f26={26,20};
static struct GfxBase gfx;
static struct IntuitionBase base={&gfx,NULL};
static struct Task*FindTask(void*x){(void)x;return &task;}
static void*AllocVec(size_t n,ULONG flags){(void)flags;if(++allocations==failat)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void FreeVec(void*p){if(p){assert(live);live--;free(p);}}
#define CopyMem(s,d,n) memcpy(d,s,n)
static ULONG LockIBase(int x){(void)x;return locked++;}
static void UnlockIBase(ULONG old){assert(locked==old+1);--locked;}
static void InitRastPort(struct RastPort*rp){memset(rp,0,sizeof(*rp));}
static void SetFont(struct RastPort*rp,struct TextFont*f){assert(f);rp->Font=f;}
static LONG TextLength(struct RastPort*rp,CONST_STRPTR t,ULONG n){(void)t;assert(rp->Font);return n*(rp->Font->tf_YSize*6/13);}
static ULONG TextFit(struct RastPort*rp,CONST_STRPTR t,ULONG n,struct TextExtent*e,void*c,int direction,LONG width,LONG height){(void)t;(void)e;(void)c;assert(direction==1&&height>0);ULONG fits=width/(rp->Font->tf_YSize*6/13);return fits<n?fits:n;}
static ULONG LabelWidth(struct RastPort*rp,STRPTR t,ULONG n,struct IntuitionBase*b){(void)b;ULONG count=0;for(ULONG i=0;i<n;i++)if(t[i]!='_')count++;return TextLength(rp,t,count);}
static void SetABPenDrMd(struct RastPort*rp,int a,int b,int d){(void)rp;(void)a;(void)b;(void)d;}
static void SetAPen(struct RastPort*rp,int p){(void)rp;(void)p;}
static void Move(struct RastPort*rp,LONG x,LONG y){rp->x=x;rp->y=y;}
static void Text(struct RastPort*rp,CONST_STRPTR t,ULONG n){if(t&&!strcmp(t,"position")){positioned_x=rp->x;positioned_y=rp->y;}if(rp->pixel){assert(rp->x>=0&&rp->y-(LONG)rp->Font->tf_Baseline>=0);assert(rp->x+TextLength(rp,t,n)<=rp->w);assert(rp->y-(LONG)rp->Font->tf_Baseline+rp->Font->tf_YSize<=rp->h);}paintcalls++;}
static void RenderLabel(struct RastPort*rp,STRPTR t,ULONG n,struct IntuitionBase*b){(void)b;ULONG count=0;for(ULONG i=0;i<n;i++)if(t[i]!='_')count++;Text(rp,t,count);}
static void RectFill(struct RastPort*rp,LONG x,LONG y,LONG r,LONG b){if(rp->pixel)assert(x>=0&&y>=0&&r<rp->w&&b<rp->h&&x<=r&&y<=b);}
static void DrawBorder(struct RastPort*rp,struct Border*b,LONG x,LONG y){for(;b;b=b->NextBorder){assert(b->Count==3);for(int i=0;i<6;i+=2){if(rp->pixel)assert(x+b->XY[i]>=0&&x+b->XY[i]<rp->w&&y+b->XY[i+1]>=0&&y+b->XY[i+1]<rp->h);}}}
static BOOL GetAttr(ULONG a,Object o,IPTR*v){if(a==WDA_PreparedScaleWindow){*v=TRUE;return TRUE;}assert(a==SA_RenderContext);*v=(IPTR)((struct Screen*)o)->current;if(raced_context)((struct Screen*)o)->current=raced_context;return TRUE;}
static const struct ScreenScaleMetrics*ScreenRenderContextLogicalMetrics(const struct ScreenRenderContext*context){return &context->metrics;}
static BOOL ScreenRenderContextBorders(const struct ScreenRenderContext*c,BOOL logical,LONG*l,LONG*t,LONG*r,LONG*b){assert(!logical);*l=*r=*b=c->src_Scale/100;*t=(2*c->src_Scale+50)/100;return TRUE;}
static void native_borders(const struct ScreenRenderContext*c,LONG*l,LONG*t,LONG*r,LONG*b){ScreenRenderContextBorders(c,FALSE,l,t,r,b);*t=c->src_PhysicalDrawInfo->dri_Font->tf_YSize+(3*c->src_Scale+50)/100;}
static IPTR tag(struct TagItem*t,IPTR key){for(;t->ti_Tag;t++)if(t->ti_Tag==key)return t->ti_Data;return 0;}
static struct Window*OpenWindowTagList(void*n,struct TagItem*t){(void)n;struct Screen*s=(void*)tag(t,WA_CustomScreen);const struct ScreenRenderContext*c=(void*)tag(t,WA_RenderContext);assert(tag(t,WA_Hidden)&&!tag(t,WA_Activate)&&tag(t,WA_ScaleAware));assert(tag(t,WA_IDCMP)&IDCMP_CHANGESCALE);assert(c&&c->src_PhysicalDrawInfo->dri_Screen==s);if(stale_open){stale_open=FALSE;return NULL;}if(c!=s->current)return NULL;
struct Window*w=AllocVec(sizeof(*w),0);if(!w)return NULL;w->FirstGadget=AllocVec(sizeof(struct Gadget),0);if(!w->FirstGadget){FreeVec(w);return NULL;}w->FirstGadget->NextGadget=AllocVec(sizeof(struct Gadget),0);if(!w->FirstGadget->NextGadget){FreeVec(w->FirstGadget);FreeVec(w);return NULL;}w->RPort=AllocVec(sizeof(*w->RPort),0);if(!w->RPort){FreeVec(w->FirstGadget->NextGadget);FreeVec(w->FirstGadget);FreeVec(w);return NULL;}
w->WScreen=s;w->expected=c;w->ScaleAware=TRUE;w->Width=tag(t,WA_Width);w->Height=tag(t,WA_Height);native_borders(c,&w->BorderLeft,&w->BorderTop,&w->BorderRight,&w->BorderBottom);w->hidden=TRUE;w->RPort->w=w->Width;w->RPort->h=w->Height;w->RPort->pixel=(void*)1;w->RPort->Layer=(void*)w;w->RPort->BitMap=w;hiddenopens++;return w;}
static void CloseWindow(struct Window*w){assert(w);ULONG closeLock=LockIBase(0);DetachSystemRequesterScale(w,&base);UnlockIBase(closeLock);struct Gadget*g=w->FirstGadget,*n=g->NextGadget;assert(!n->NextGadget);FreeVec(n);FreeVec(g);FreeVec(w->RPort);FreeVec(w);closed++;}
static Object NewObjectA(void*c,CONST_STRPTR name,struct TagItem*t){(void)c;assert(!strcmp(name,FRBUTTONCLASS));struct Gadget*g=AllocVec(sizeof(*g),0);if(g){g->GadgetID=tag(t,GA_ID);g->label=(void*)tag(t,GA_Text);}return g;}
static void DisposeObject(Object o){FreeVec(o);}
static UWORD AddGList(struct Window*w,struct Gadget*g,UWORD pos,UWORD count,void*r){(void)pos;(void)count;(void)r;w->FirstGadget->NextGadget->NextGadget=g;return 2;}
static void RemoveGList(struct Window*w,struct Gadget*g,UWORD count){(void)count;assert(w->FirstGadget->NextGadget->NextGadget==g);w->FirstGadget->NextGadget->NextGadget=NULL;}
static BOOL ShowWindow(struct Window*w,void*other){assert(!other&&w->hidden);if(stale_show){stale_show=FALSE;return FALSE;}if(w->expected!=w->WScreen->current)return FALSE;w->hidden=FALSE;shown++;return TRUE;}
static void BeginRefresh(struct Window*w){assert(w&&locked);}
static void EndRefresh(struct Window*w,BOOL complete){assert(w&&locked&&complete);}
static void ActivateWindow(struct Window*w){assert(!w->hidden);w->active=TRUE;activated++;}
'''
# Production event helper has external linkage, as declared in its header.
prefix=prefix.replace('static BOOL HandleScaledSystemRequesterEvent','BOOL HandleScaledSystemRequesterEvent')
# Instrument store boundaries in the actual install bodies. Fault callbacks
# exist only in the host translation unit; production performs plain copies.
storeprefix=r"""
static jmp_buf store_return;
static unsigned store_count,store_fail;
static void store_cut(void){++store_count;if(store_fail&&store_count==store_fail)longjmp(store_return,1);}
"""
a=production.index('static void systemreq_install(');b=production.index('void FinishSystemRequesterScale(',a)
segment=production[a:b]
segment=segment.replace('prepared->oldActiveDrawInfo : prepared->newActiveDrawInfo;', 'prepared->oldActiveDrawInfo : prepared->newActiveDrawInfo; store_cut();')
segment=re.sub(r'(?m)^(.*(?:gadget->(?:LeftEdge|TopEdge|Width|Height|GadgetRender|SelectRender)|owner->current =|prepared->tail->NextGadget =|window->FirstGadget =|prepared->activeInfo->gi_Domain =|\*prepared->activeRaster =).*;)$',r'\1 store_cut();',segment)
production=production[:a]+segment+production[b:]
prefix+=storeprefix
inputsource=(root/'rom/intuition/buttongclass.c').read_text()
a=inputsource.index('IPTR ButtonGClass__GM_HANDLEINPUT(');b=inputsource.index('{',a);i=b+1;depth=1
while depth:depth+=(inputsource[i]=='{')-(inputsource[i]=='}');i+=1
actualinput=inputsource[a:i]
a=inputsource.index('IPTR ButtonGClass__GM_GOACTIVE(');b=inputsource.index('{',a);i=b+1;depth=1
while depth:depth+=(inputsource[i]=='{')-(inputsource[i]=='}');i+=1
actualinput=inputsource[a:i]+'\n'+actualinput
inputprefix=r"""
typedef struct Class{IPTR cl_UserData;}Class;
typedef void*Msg;
struct Point{WORD X,Y;};
struct IBox{LONG Left,Top,Width,Height;};
struct GadgetInfo{struct Window*gi_Window;struct RastPort*gi_RastPort;struct DrawInfo*gi_DrInfo;struct IBox gi_Domain;struct Screen*gi_Screen;struct Requester*gi_Requester;struct Layer*gi_Layer;};
struct IIHData{struct Gadget*ActiveGadget;struct GadgetInfo GadgetInfo;struct RastPort GadgetInfoRastPort;struct Gadget*ActiveSysGadget;};
struct InputEvent{ULONG ie_Class,ie_Code,ie_SubClass,ie_Qualifier;void*ie_EventAddress;};
struct gpInput{struct GadgetInfo*gpi_GInfo;struct InputEvent*gpi_IEvent;ULONG*gpi_Termination;struct Point gpi_Mouse;};
struct gpRender{ULONG MethodID;struct GadgetInfo*gpr_GInfo;struct RastPort*gpr_RPort;ULONG gpr_Redraw;};
struct gpHitTest{ULONG MethodID;struct GadgetInfo*gpht_GInfo;struct Point gpht_Mouse;};
#define IECLASS_RAWMOUSE 1
#define IECLASS_TIMER 2
#define SELECTUP 1
#define IECODE_NOBUTTON 2
#define GM_RENDER 1
#define GM_HITTEST 2
#define GREDRAW_UPDATE 1
#define GREDRAW_REDRAW 2
#define GACT_TOGGLESELECT 1
#define GMR_GADGETHIT 1
#define GMR_MEACTIVE 4
#define GMR_NOREUSE 1
#define GMR_VERIFY 2
#define GMR_REUSE 8
#define OPUF_INTERIM 1
static Object rendered_gadget;
static struct TextFont*rendered_font;
static struct RastPort*ObtainGIRPort(struct GadgetInfo*gi){return gi->gi_RastPort;}
static void ReleaseGIRPort(struct RastPort*rp){assert(rp);}
static IPTR DoMethodA(Object object,Msg message){assert(object&&((struct gpRender*)message)->MethodID==GM_RENDER);rendered_gadget=object;rendered_font=((struct gpRender*)message)->gpr_GInfo->gi_DrInfo->dri_Font;return 0;}
static void notifypressed(Class*cl,struct Gadget*g,struct GadgetInfo*gi,ULONG interim){(void)interim;assert(cl->cl_UserData==(IPTR)&base&&g&&gi->gi_Window);}
"""
handler_source=(root/'rom/intuition/sysrequest_intern.c').read_text()
def actual_function(source,name):
    m=re.search(r'(?m)^.*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',source);assert m,name
    a=m.start();b=source.index('{',a);i=b+1;depth=1
    while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
    return source[a:i]
actual_handler=actual_function(handler_source,'sysreqhandler_getmsg')+'\n'+actual_function(handler_source,'sysreqhandler_intern')
handler_prefix=r"""
#define DEBUG_SYSREQHANDLER(x)
#define IDCMP_IDCMPUPDATE 32
#define IECLASS_RAWKEY 3
#define IEQUALIFIER_LCOMMAND 1
#define APTR void*
struct Message{int unused;};
struct Library{int unused;};
static struct IntuiMessage*queued[4];static unsigned queue_read,queue_count,replies;
static void WaitPort(void*p){(void)p;}
static void*GetMsg(void*p){(void)p;return queue_read<queue_count?queued[queue_read++]:NULL;}
static void ReplyMsg(struct Message*m){assert(m);replies++;}
static int MapRawKey(struct InputEvent*ie,char*buffer,int len,void*map){(void)map;assert(len==1);buffer[0]=ie->ie_Code;return 1;}
static char ToUpper(char c){return c>='a'&&c<='z'?c-'a'+'A':c;}
static IPTR GetTagData(IPTR tag,IPTR fallback,struct TagItem*t){for(;t&&t->ti_Tag;t++)if(t->ti_Tag==tag)return t->ti_Data;return fallback;}
static void enqueue(struct IntuiMessage*message){queue_read=0;queue_count=1;queued[0]=message;}
"""
general_header=re.sub(r'^#include[^\n]*\n','',(root/'rom/intuition/activegadgetscale.h').read_text(),flags=re.M)
general_production=re.sub(r'^#include[^\n]*\n','',(root/'rom/intuition/activegadgetscale.c').read_text(),flags=re.M)
domain=actual_function((root/'rom/intuition/gadgets.c').read_text(encoding='latin1'),'GetGadgetDomain')
general_prefix=r'''
#define IW(w) (w)
#define GTYP_GADGETTYPE 0xfc00
#define GTYP_REQGADGET (1<<12)
#define GTYP_GZZGADGET (1<<13)
#define GTYP_SCRGADGET (1<<14)
#define GTYP_SYSGADGET (1<<15)
#define WFLG_GIMMEZEROZERO 1024
#define IS_SCREEN_GADGET(g) ((g)->GadgetType&GTYP_SCRGADGET)
#define IS_REQ_GADGET(g) ((g)->GadgetType&GTYP_REQGADGET)
#define GACT_RIGHTBORDER (1<<4)
#define GACT_LEFTBORDER (1<<5)
#define GACT_TOPBORDER (1<<6)
#define GACT_BOTTOMBORDER (1<<7)
#define IS_BORDER_GADGET(g) (((g)->GadgetType&GTYP_GZZGADGET)||((g)->Activation&(GACT_RIGHTBORDER|GACT_LEFTBORDER|GACT_TOPBORDER|GACT_BOTTOMBORDER)))
#define SET_GI_RPORT(gi,w,req,gad) ((gi)->gi_RastPort=IS_SCREEN_GADGET(gad)?((gi)->gi_Screen->BarLayer?(gi)->gi_Screen->BarLayer->rp:NULL):(IS_BORDER_GADGET(gad)?(w)->BorderRPort:((req)?(req)->ReqLayer->rp:(w)->RPort)))
'''
tests=r'''
static void setupcontext(struct Screen*s,struct ScreenRenderContext*c,struct DrawInfo*d,struct TextFont*f,ULONG scale,UWORD*pens){*d=(struct DrawInfo){s,f,pens};*c=(struct ScreenRenderContext){.src_Scale=scale,.src_PhysicalDrawInfo=d,.src_LegacyScaleFactor=(scale+50)/100,.metrics={2,1,1,1}};}
/* Native111/106 metadata install is external to this requester module. */
static void external_frame_install(struct Window*w,const struct WindowFitGeometry*g,LONG l,LONG t,LONG r,LONG b,struct DrawInfo*drawinfo){struct PreparedActiveGadgetScale active;assert(PrepareActiveGadgetScale(w,drawinfo,g,l,t,r,b,&active,&base));w->Width=w->RPort->w=g->width;w->Height=w->RPort->h=g->height;w->BorderLeft=l;w->BorderTop=t;w->BorderRight=r;w->BorderBottom=b;InstallActiveGadgetScale(&active,FALSE);}
int main(void){UWORD pens[]={0,1,2,3};struct Screen screen={2560,1600},other={2560,1600};struct DrawInfo d13,d26;struct ScreenRenderContext c100,c200;setupcontext(&screen,&c100,&d13,&f13,100,pens);setupcontext(&screen,&c200,&d26,&f26,200,pens);screen.current=&c100;other.current=&c100;
STRPTR labels[]={"_One","_Two","Cancel",NULL};BOOL handled;
/* Every initial allocation refusal closes hidden candidates and owns no state. */
ULONG failures=0;
for(ULONG fault=1;fault<=64;fault++){allocations=0;failat=fault;shown=activated=0;struct Window*w=OpenScaledSystemRequester(&screen,"Test","Hello\nSecond line",NULL,labels,3,0,0,0,&handled,&base);assert(handled);if(w){assert(shown==1&&activated==1);assert(FreeScaledSystemRequester(w,&base));assert(!live&&!SystemRequesters);continue;}assert(!live&&!SystemRequesters&&!shown&&!activated);failures++;}
assert(failures>5);failat=0;allocations=0;
/* Unsupported non-Process and explicit font routes preserve logical caller. */
task.tc_Node.ln_Type=0;assert(!OpenScaledSystemRequester(&screen,"x","x",NULL,labels,3,0,0,0,&handled,&base)&&!handled);task.tc_Node.ln_Type=NT_PROCESS;struct IntuiText custom={.ITextFont=(void*)1,.IText="custom"};assert(!OpenScaledSystemRequester(&screen,"x",NULL,&custom,labels,3,0,0,0,&handled,&base)&&!handled);
stale_open=stale_show=TRUE;struct Window*w=OpenScaledSystemRequester(&screen,"Test","Hello\nSecond line",NULL,labels,3,0,0,0,&handled,&base);assert(w&&handled&&!w->hidden);struct SystemRequesterOwner*owner=SystemRequesters;assert(owner->window==w);for(ULONG i=0;i<owner->count+1;i++)assert(owner->gadgets[i]->NextGadget==owner->gadgets[i+1]);
struct Window*identity=w;struct Gadget*first=w->FirstGadget,*tail=first->NextGadget,*button=owner->gadgets[0];struct Gadget*next=button->NextGadget;UWORD id=button->GadgetID;UWORD flags=button->Flags;const struct ScreenRenderContext*expected=w->expected;
struct Screen otherScreen={2560,1600};struct DrawInfo otherDI;struct ScreenRenderContext otherCTX;setupcontext(&otherScreen,&otherCTX,&otherDI,&f13,100,pens);otherScreen.current=&otherCTX;struct Window*control=OpenScaledSystemRequester(&otherScreen,"Control","Unchanged",NULL,labels,3,0,0,0,&handled,&base);struct SystemRequesterOwner*otherOwner=SystemRequesters;struct SystemRequesterGeneration*otherGeneration=otherOwner->current;
struct TextFont retainedFonts[13];struct DrawInfo retainedDI[13];struct ScreenRenderContext retainedContexts[13];
struct IIHData activeInput={.ActiveGadget=button};activeInput.GadgetInfoRastPort=*w->RPort;
activeInput.GadgetInfo=(struct GadgetInfo){w,&activeInput.GadgetInfoRastPort,&d13,{0,0,w->Width,w->Height},w->WScreen,NULL,w->RPort->Layer};
struct Interrupt inputhandler={&activeInput};base.InputHandler=&inputhandler;
Class pressclass={(IPTR)&base};struct gpInput press={.gpi_GInfo=&activeInput.GadgetInfo};
assert(ButtonGClass__GM_GOACTIVE(&pressclass,button,&press)==GMR_MEACTIVE);flags=button->Flags;assert(flags&GFLG_SELECTED);
for(ULONG scale=100;scale<=400;scale+=25){struct TextFont font={(UWORD)((13*scale+50)/100),(UWORD)((10*scale+50)/100)};struct DrawInfo dri;struct ScreenRenderContext context;setupcontext(&screen,&context,&dri,&font,scale,pens);ULONG slot=(scale-100)/25;retainedFonts[slot]=font;retainedDI[slot]=dri;retainedDI[slot].dri_Font=&retainedFonts[slot];retainedContexts[slot]=context;retainedContexts[slot].src_PhysicalDrawInfo=&retainedDI[slot];const struct ScreenRenderContext*chosen=&retainedContexts[slot];struct WindowFitGeometry geometry={.width=(LONG)(700*scale/100),.height=(LONG)(400*scale/100)};if(geometry.width>2560)geometry.width=2560;LONG l,t,r,b;native_borders(&context,&l,&t,&r,&b);
 struct SystemRequesterGeneration*old=owner->current;ULONG before=live;
 struct WindowFitGeometry oldgeometry={.width=w->Width,.height=w->Height};LONG oldl=w->BorderLeft,oldt=w->BorderTop,oldr=w->BorderRight,oldb=w->BorderBottom;
 for(ULONG fault=1;fault<=2;fault++){allocations=0;failat=fault;struct PreparedSystemRequesterScale*p=(void*)1;assert(!PrepareSystemRequesterScale(w,chosen,&geometry,l,t,r,b,&p,&base)&&!p);assert(owner->current==old&&live==before&&button->Flags==flags);}
 failat=0;struct PreparedSystemRequesterScale*p;assert(PrepareSystemRequesterScale(w,chosen,&geometry,l,t,r,b,&p,&base)&&p);struct RastPort rp={.w=geometry.width,.h=geometry.height,.pixel=(void*)1};assert(PaintSystemRequesterScale(p,&rp,&base));assert(rp.Font==&retainedFonts[slot]);struct Rectangle bounds;assert(SystemRequesterScaleBounds(p,&bounds)&&bounds.MinX==l&&bounds.MaxY==geometry.height-b-1);
 external_frame_install(w,&geometry,l,t,r,b,chosen->src_PhysicalDrawInfo);InstallSystemRequesterScale(p,FALSE);assert(activeInput.GadgetInfo.gi_DrInfo==chosen->src_PhysicalDrawInfo&&activeInput.GadgetInfoRastPort.Font==chosen->src_PhysicalDrawInfo->dri_Font&&activeInput.GadgetInfo.gi_Domain.Width==geometry.width);assert(w==identity&&w->expected==expected&&w->FirstGadget==first&&tail->NextGadget==button&&button->NextGadget==next&&button->GadgetID==id&&button->Flags==flags);assert(otherOwner->current==otherGeneration);external_frame_install(w,&oldgeometry,oldl,oldt,oldr,oldb,old->context->src_PhysicalDrawInfo);InstallSystemRequesterScale(p,TRUE);assert(owner->current==old&&button->Flags==flags);external_frame_install(w,&geometry,l,t,r,b,chosen->src_PhysicalDrawInfo);InstallSystemRequesterScale(p,FALSE);FinishSystemRequesterScale(p,TRUE,&base);assert(owner->current->context==chosen);
}
/* Roll back every scalar/pointer store, including the two system-gadget
   chain boundary controls; selected state and live identities must survive. */
struct GadgetInfo savedInputInfo=activeInput.GadgetInfo;struct RastPort savedInputRaster=activeInput.GadgetInfoRastPort;
struct Gadget savedButtons[5],savedSystem[2];for(unsigned i=0;i<5;i++)savedButtons[i]=*owner->gadgets[i];savedSystem[0]=*first;savedSystem[1]=*tail;
struct WindowFitGeometry cutgeometry={.width=1400,.height=800};struct PreparedSystemRequesterScale*cutplan;
struct SystemRequesterGeneration*beforecuts=owner->current;
assert(PrepareSystemRequesterScale(w,&c200,&cutgeometry,2,32,2,2,&cutplan,&base));
store_count=0;InstallSystemRequesterScale(cutplan,FALSE);unsigned stores=store_count;assert(stores>=33);
InstallSystemRequesterScale(cutplan,TRUE);
for(unsigned cut=1;cut<=stores;cut++){store_count=0;store_fail=cut;
 if(!setjmp(store_return)){InstallSystemRequesterScale(cutplan,FALSE);assert(0);}
 store_fail=0;InstallSystemRequesterScale(cutplan,TRUE);
 assert(owner->current==beforecuts&&w->FirstGadget==first);
 assert(!memcmp(&activeInput.GadgetInfo,&savedInputInfo,sizeof(savedInputInfo))&&!memcmp(&activeInput.GadgetInfoRastPort,&savedInputRaster,sizeof(savedInputRaster))&&activeInput.ActiveGadget==button);
 assert(!memcmp(first,&savedSystem[0],sizeof(*first))&&!memcmp(tail,&savedSystem[1],sizeof(*tail)));
 for(unsigned i=0;i<5;i++)assert(!memcmp(owner->gadgets[i],&savedButtons[i],sizeof(savedButtons[i])));
}
FinishSystemRequesterScale(cutplan,FALSE,&base);
struct IntuiMessage wrong={IDCMP_CHANGESCALE,&otherScreen};ULONG beforepaint=paintcalls;
assert(HandleScaledSystemRequesterEvent(w,&wrong,&base)&&paintcalls==beforepaint);
struct IntuiMessage own={IDCMP_CHANGESCALE,&screen};
assert(HandleScaledSystemRequesterEvent(w,&own,&base)&&paintcalls>beforepaint);
struct TextFont changed={34,26};struct DrawInfo changedDI;struct ScreenRenderContext changedCTX;
setupcontext(&screen,&changedCTX,&changedDI,&changed,200,pens);
struct WindowFitGeometry same={.width=1400,.height=800};struct PreparedSystemRequesterScale*changedPlan;
assert(PrepareSystemRequesterScale(w,&changedCTX,&same,2,40,2,2,&changedPlan,&base));
struct RastPort changedRP={0};assert(PaintSystemRequesterScale(changedPlan,&changedRP,&base)&&changedRP.Font==&changed);
external_frame_install(w,&same,2,40,2,2,changedCTX.src_PhysicalDrawInfo);InstallSystemRequesterScale(changedPlan,FALSE);FinishSystemRequesterScale(changedPlan,TRUE,&base);
struct IntuiMessage primary={IDCMP_GADGETUP,button};assert(!HandleScaledSystemRequesterEvent(w,&primary,&base));struct InputEvent release={IECLASS_RAWMOUSE,SELECTUP};ULONG termination=0;
Class inputclass={(IPTR)&base};struct gpInput input={&activeInput.GadgetInfo,&release,&termination,{0,0}};
assert(ButtonGClass__GM_HANDLEINPUT(&inputclass,button,&input)==(GMR_NOREUSE|GMR_VERIFY));
assert(termination==IDCMP_GADGETUP&&rendered_gadget==button&&rendered_font==&changed&&!(button->Flags&GFLG_SELECTED));activeInput.ActiveGadget=NULL;
assert(button==owner->gadgets[0]&&button->GadgetID==id&&button->NextGadget==next);
struct IntuiMessage clicked={IDCMP_GADGETUP,button};enqueue(&clicked);
assert(sysreqhandler_intern(w,NULL,FALSE,&base)==id);
clicked.IAddress=owner->gadgets[owner->count-1];enqueue(&clicked);
assert(sysreqhandler_intern(w,NULL,FALSE,&base)==0);
base.KeymapBase=(void*)1;base.IControlPrefs.ic_ReqTrue='Y';base.IControlPrefs.ic_ReqFalse='N';
IPTR rawaddress=0;struct IntuiMessage raw={IDCMP_RAWKEY,&rawaddress,'Y',IEQUALIFIER_LCOMMAND};enqueue(&raw);
assert(sysreqhandler_intern(w,NULL,FALSE,&base)==1);raw.Code='N';enqueue(&raw);assert(sysreqhandler_intern(w,NULL,FALSE,&base)==0);
owner->common.IDCMP=IDCMP_REFRESHWINDOW;struct IntuiMessage refresh={IDCMP_REFRESHWINDOW,w->WScreen};ULONG returnedIDCMP=0;enqueue(&refresh);
assert(sysreqhandler_intern(w,&returnedIDCMP,FALSE,&base)==-1&&returnedIDCMP==IDCMP_REFRESHWINDOW);owner->common.IDCMP=0;
assert(sysreqhandler_intern(NULL,NULL,FALSE,&base)==0&&sysreqhandler_intern((void*)1,NULL,FALSE,&base)==1);
struct WindowFitGeometry reset={.width=700,.height=400};struct PreparedSystemRequesterScale*resetPlan;
assert(PrepareSystemRequesterScale(w,&c100,&reset,1,16,1,1,&resetPlan,&base));struct RastPort resetRP={.w=700,.h=400,.pixel=(void*)1};assert(PaintSystemRequesterScale(resetPlan,&resetRP,&base)&&resetRP.Font==&f13);
external_frame_install(w,&reset,1,16,1,1,c100.src_PhysicalDrawInfo);InstallSystemRequesterScale(resetPlan,FALSE);FinishSystemRequesterScale(resetPlan,TRUE,&base);assert(owner->current->context==&c100&&button->Width==52&&!(button->Flags&GFLG_SELECTED));
/* Context identity (not just font height) keys retained generations. */
ULONG cachelive=live;
for(unsigned revisit=0;revisit<10;revisit++){
 const struct ScreenRenderContext*cached=revisit&1?&c100:&retainedContexts[0];
 struct SystemRequesterGeneration*previous=owner->current;struct PreparedSystemRequesterScale*reuse;
 allocations=0;assert(PrepareSystemRequesterScale(w,cached,&reset,1,16,1,1,&reuse,&base));assert(!reuse->created&&allocations==1);
 struct RastPort cachedRP={.w=700,.h=400,.pixel=(void*)1};assert(PaintSystemRequesterScale(reuse,&cachedRP,&base)&&cachedRP.Font==cached->src_PhysicalDrawInfo->dri_Font);
 InstallSystemRequesterScale(reuse,FALSE);
 if(revisit==3){InstallSystemRequesterScale(reuse,TRUE);assert(owner->current==previous);FinishSystemRequesterScale(reuse,FALSE,&base);}
 else FinishSystemRequesterScale(reuse,TRUE,&base);
 assert(live==cachelive);
}
struct Window unowned={.UserData=(void*)1};struct PreparedSystemRequesterScale*none=(void*)1;
assert(PrepareSystemRequesterScale(&unowned,NULL,NULL,0,0,0,0,&none,&base)&&!none);
assert(FreeScaledSystemRequester(control,&base));assert(FreeScaledSystemRequester(w,&base));assert(!live&&!SystemRequesters&&!locked);
/* Genuine overflowing text at all13 scales and reset. Reserved controls
   must remain navigation even if the caller subscribes GADGETUP/RAWKEY. */
struct Screen small={2560,1600};struct DrawInfo smallDI;struct ScreenRenderContext smallCTX;setupcontext(&small,&smallCTX,&smallDI,&f13,100,pens);small.current=&smallCTX;
char longbody[2401];for(int i=0;i<400;i++)memcpy(longbody+i*6,"line!\n",6);longbody[2400]=0;
struct Window*scrollwindow=OpenScaledSystemRequester(&small,"Scroll",longbody,NULL,labels,3,
 IDCMP_GADGETUP|IDCMP_RAWKEY|IDCMP_REFRESHWINDOW|IDCMP_CHANGESCALE,0,0,&handled,&base);assert(scrollwindow&&handled);struct SystemRequesterOwner*scrollowner=SystemRequesters;
struct TextFont scrollFonts[13];struct DrawInfo scrollDI[13];struct ScreenRenderContext scrollContexts[13];
struct Window*scrollidentity=scrollwindow;struct Gadget*upidentity=scrollowner->gadgets[3],*downidentity=scrollowner->gadgets[4];
for(unsigned step=0;step<14;step++){
 unsigned slot=step==13?0:step,scale=100+slot*25;
 if(step<13){scrollFonts[slot]=(struct TextFont){(13*scale+50)/100,(10*scale+50)/100};setupcontext(&small,&scrollContexts[slot],&scrollDI[slot],&scrollFonts[slot],scale,pens);}
 const struct ScreenRenderContext*ctx=&scrollContexts[slot];small.current=ctx;
 LONG l,t,r,b;native_borders(ctx,&l,&t,&r,&b);struct WindowFitGeometry g={.width=900,.height=1000};struct PreparedSystemRequesterScale*plan;
 assert(PrepareSystemRequesterScale(scrollwindow,ctx,&g,l,t,r,b,&plan,&base));
 struct RastPort proposed={.w=900,.h=1000,.pixel=(void*)1};assert(PaintSystemRequesterScale(plan,&proposed,&base));
 external_frame_install(scrollwindow,&g,l,t,r,b,ctx->src_PhysicalDrawInfo);InstallSystemRequesterScale(plan,FALSE);FinishSystemRequesterScale(plan,TRUE,&base);
 assert(scrollwindow==scrollidentity&&scrollowner->gadgets[3]==upidentity&&scrollowner->gadgets[4]==downidentity&&scrollowner->current->rows>scrollowner->current->visible);
 for(unsigned direction=0;direction<2;direction++){
  struct Gadget*navigation=direction?upidentity:downidentity;
  struct GadgetInfo gi={scrollwindow,scrollwindow->RPort,ctx->src_PhysicalDrawInfo,{0,0,900,1000}};
  struct gpInput pressed={.gpi_GInfo=&gi};assert(ButtonGClass__GM_GOACTIVE(&pressclass,navigation,&pressed)==GMR_MEACTIVE);
  assert(navigation->Flags&GFLG_SELECTED);struct InputEvent released={IECLASS_RAWMOUSE,SELECTUP};ULONG done=0;
  struct gpInput releasedMessage={&gi,&released,&done,{0,0}};
  assert(ButtonGClass__GM_HANDLEINPUT(&pressclass,navigation,&releasedMessage)==(GMR_NOREUSE|GMR_VERIFY));assert(done==IDCMP_GADGETUP&&!(navigation->Flags&GFLG_SELECTED)&&rendered_font==ctx->src_PhysicalDrawInfo->dri_Font);
  struct IntuiMessage clickedNavigation={IDCMP_GADGETUP,navigation};ULONG navIDCMP=0;enqueue(&clickedNavigation);
  assert(sysreqhandler_intern(scrollwindow,&navIDCMP,FALSE,&base)==-2&&!navIDCMP&&scrollowner->scroll==(direction?0:1));
 }
 /* Wheel raw codes, without Command, are ignored by the existing response
    handler. They are not newly mapped to a response or navigation shortcut. */
 IPTR wheelAddress=0;struct IntuiMessage wheel={IDCMP_RAWKEY,&wheelAddress,0x7a,0};enqueue(&wheel);assert(sysreqhandler_intern(scrollwindow,NULL,FALSE,&base)==-2);
 wheel.Code=0x7b;enqueue(&wheel);assert(sysreqhandler_intern(scrollwindow,NULL,FALSE,&base)==-2);
 struct IntuiMessage ordinary={IDCMP_GADGETUP,scrollowner->gadgets[0]};enqueue(&ordinary);assert(sysreqhandler_intern(scrollwindow,NULL,FALSE,&base)==1);
 ordinary.IAddress=scrollowner->gadgets[2];enqueue(&ordinary);assert(sysreqhandler_intern(scrollwindow,NULL,FALSE,&base)==0);
 ULONG eventClass=0;struct IntuiMessage explicitEvent={IDCMP_REFRESHWINDOW,&small};enqueue(&explicitEvent);assert(sysreqhandler_intern(scrollwindow,&eventClass,FALSE,&base)==-1&&eventClass==IDCMP_REFRESHWINDOW);
 explicitEvent.Class=IDCMP_CHANGESCALE;eventClass=0;enqueue(&explicitEvent);assert(sysreqhandler_intern(scrollwindow,&eventClass,FALSE,&base)==-1&&eventClass==IDCMP_CHANGESCALE);
}
assert(FreeScaledSystemRequester(scrollwindow,&base));
struct IntuiText positioned={.LeftEdge=10,.TopEdge=7,.IText="position",.FrontPen=1,.DrawMode=JAM1};
struct Window*syswindow=OpenScaledSystemRequester(&screen,"Position",NULL,&positioned,labels,3,0,0,0,&handled,&base);assert(syswindow&&handled);
assert(positioned_x==syswindow->BorderLeft+8+10&&positioned_y==syswindow->BorderTop+8+7+10);
assert(FreeScaledSystemRequester(syswindow,&base));assert(!live&&!SystemRequesters&&!locked);
/* Logical fallback captures font, metrics and integer viewport together,
   even if another generation publishes immediately after GetAttr. */
struct TextFont logical17={17,12};struct DrawInfo logical17DI={&screen,&logical17,pens};
struct DrawInfo physical52DI={&screen,&f26,pens};struct ScreenRenderContext raced;
setupcontext(&screen,&raced,&physical52DI,&f26,400,pens);raced.src_LogicalDrawInfo=&logical17DI;raced.metrics.WBorTop=9;
c200.src_LogicalDrawInfo=&d13;screen.current=&c200;screen.RastPort.Font=&f26;screen.WBorTop=99;screen.LeftEdge=-3;screen.TopEdge=5;
raced_context=&raced;struct Screen logicalview;const struct ScreenRenderContext*captured;
assert(LogicalSystemRequesterScreen(&screen,&logicalview,&captured,&base));
assert(captured==&c200&&logicalview.RastPort.Font==&f13&&logicalview.WBorTop==2&&logicalview.Width==1280&&logicalview.Height==800&&logicalview.LeftEdge==-2&&logicalview.TopEdge==2);
raced_context=NULL;
printf("requesterscale-host PASS initialfaults=%u hidden-context-retry Process/custom fallback;13scales prepared/rollback/paint; stable Window+selected Gadget+links+IDs; other-screen; reset; stock-input release/response/cancel/rawkey/IDCMP; scroll; exact IntuiText offsets; no leaks\n",failures);}
'''
variants=[('production',production,True),
 ('reserved-scroll-answer',production.replace('(message->Class == IDCMP_REFRESHWINDOW ||\n            message->Class == IDCMP_CHANGESCALE) &&','TRUE &&'),False),
 ('visible-before-validated',production.replace('{WA_Hidden,TRUE}','{WA_Hidden,FALSE}'),False),
 ('native-window-legacy',production.replace('{WA_ScaleAware,TRUE}','{WA_ScaleAware,FALSE}'),False),
 ('reset-selected',production.replace('gadget->GadgetRender = button->normal;','gadget->Flags=0; gadget->GadgetRender = button->normal;'),False),
 ('break-client-chain',production.replace('gadget->SelectRender = button->selected;','gadget->NextGadget=NULL; gadget->SelectRender = button->selected;'),False),
 ('wrong-render-generation',production.replace('generation->context = context;', 'generation->context = owner->window->WScreen->current;'),False),
 ('other-screen-redraw',production.replace('draw = message->IAddress == window->WScreen;', 'draw = TRUE;'),False),
 ('wrong-cached-font',production.replace('prepared->proposed->context == context &&','TRUE &&'),False),
 ('free-borrowed-cache',production.replace('!committed && prepared->created','!committed'),False),
 ('legacy-fresh-generation',production.replace('context = (const struct ScreenRenderContext *)value;', 'context = screen->current;'),False),
 ('legacy-physical-font',production.replace('SetFont(&view->RastPort, context->src_LogicalDrawInfo->dri_Font);','SetFont(&view->RastPort, screen->RastPort.Font);'),False),
 ('legacy-full-viewport',production.replace('view->Width = screen->Width / factor;', 'view->Width = screen->Width;'),False),
 ('legacy-fresh-border',production.replace('view->WBorTop = metrics->WBorTop;', 'view->WBorTop = screen->WBorTop;'),False)]
with tempfile.TemporaryDirectory(prefix='requesterscale-') as directory:
    out=Path(directory);unit=out/'test.c';binary=out/'test'
    for name,actual,passes in variants:
        unit.write_text(prefix+inputprefix+handler_prefix+general_prefix+general_header+domain+general_production+actual+actualinput+actual_handler+tests)
        subprocess.run(['clang','-std=c11','-g','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        assert (result.returncode==0)==passes,(name,result.stdout,result.stderr)
        print(result.stdout if passes else 'REJECTED '+name,end='' if passes else '\n')
# Cross the real stock gadget hit boundary with a Border whose bytes would
# otherwise appear to be a CUSTOMIMAGEDEPTH object. No fake renderer oracle.
fr=(root/'rom/intuition/frbuttonclass.c').read_text()
a=fr.index('IPTR FrButtonClass__GM_HITTEST(');b=fr.index('{',a);i=b+1;depth=1
while depth:depth+=(fr[i]=='{')-(fr[i]=='}');i+=1
hit=fr[a:i]
hitprefix=r'''
typedef struct Class{int unused;}Class;
struct Image{WORD Depth;};
struct Point{WORD X,Y;};
struct gpHitTest{struct Point gpht_Mouse;};
struct impHitTest{ULONG MethodID;struct Point imp_Point;};
typedef void*Msg;
#define DEBUG_FRBUTTON(x)
#define GMR_GADGETHIT 1
#define CUSTOMIMAGEDEPTH -1
#define IM_HITFRAME 2
static unsigned imagecalls;
static IPTR DoMethodA(Object object,Msg message){assert(object&&((struct impHitTest*)message)->MethodID==IM_HITFRAME);imagecalls++;return 1;}
'''
hittests=r'''
int main(void){Class cl={0};struct gpHitTest message={{3,4}};
struct Image poison={CUSTOMIMAGEDEPTH};struct Gadget border={.GadgetRender=&poison};
assert(FrButtonClass__GM_HITTEST(&cl,&border,&message)==GMR_GADGETHIT&&!imagecalls);
struct Gadget custom={.GadgetRender=&poison,.Flags=GFLG_GADGIMAGE};
assert(FrButtonClass__GM_HITTEST(&cl,&custom,&message)==GMR_GADGETHIT&&imagecalls==1);
struct Image normal={1};custom.GadgetRender=&normal;
assert(FrButtonClass__GM_HITTEST(&cl,&custom,&message)==GMR_GADGETHIT&&imagecalls==1);
border.GadgetRender=NULL;assert(FrButtonClass__GM_HITTEST(&cl,&border,&message)==GMR_GADGETHIT&&imagecalls==1);
puts("requester stock GM_HITTEST PASS Border/NULL/ordinary image/custom image; old missing-type-guard mutant rejected");}
'''
with tempfile.TemporaryDirectory(prefix='requesterhit-') as directory:
    out=Path(directory);unit=out/'hit.c';binary=out/'hit'
    for name,actual,passes in [('production',hit,True),('old-type-confusion',hit.replace('(g->Flags & GFLG_GADGIMAGE) && image','image'),False)]:
        unit.write_text(prefix+hitprefix+actual+hittests)
        subprocess.run(['clang','-std=c11','-g','-O1','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        assert (result.returncode==0)==passes,(name,result.stdout,result.stderr)
        if passes:print(result.stdout,end='')

# Exercise the actual legacy cleanup entry as well: the first gadget can fail
# before a list exists, while a populated list owns exactly one shared image.
cleanup=(root/'rom/intuition/requesters.c').read_text()
a=cleanup.index('void intrequest_freegadgets(');b=cleanup.index('{',a);i=b+1;depth=1
while depth:depth+=(cleanup[i]=='{')-(cleanup[i]=='}');i+=1
cleanup=cleanup[a:i]
cleanup_prefix=r'''
#define GA_Next 900
struct Image{int unused;};
static BOOL CleanupGetAttr(ULONG a,Object o,IPTR*v){assert(a==GA_Next&&o);*v=(IPTR)((struct Gadget*)o)->NextGadget;return TRUE;}
#define GetAttr CleanupGetAttr
'''
cleanup_test=r'''
int main(void){intrequest_freegadgets(NULL,&base);assert(!live);
struct Gadget*a=AllocVec(sizeof(*a),0),*b=AllocVec(sizeof(*b),0);
struct Image*image=AllocVec(sizeof(*image),0);a->GadgetRender=image;b->GadgetRender=image;a->NextGadget=b;
intrequest_freegadgets(a,&base);assert(!live);
puts("requester legacy cleanup PASS absent list / two clients / shared image exactly once");}
'''
with tempfile.TemporaryDirectory(prefix='requestercleanup-') as directory:
    out=Path(directory);unit=out/'cleanup.c';binary=out/'cleanup'
    for name,actual,passes in [('production',cleanup,True),('missing-null-guard',cleanup.replace('if (!gadgets) return;',''),False)]:
        unit.write_text(prefix+cleanup_prefix+actual+cleanup_test)
        subprocess.run(['clang','-std=c11','-g','-O1','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        assert (result.returncode==0)==passes,(name,result.stdout,result.stderr)
        if passes:print(result.stdout,end='')
