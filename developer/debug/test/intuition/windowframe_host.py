#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL) Version 1.1.
"""Actual WindowFrame C ownership/cache/install plus actual Open publication guard.
Exec/font/image/provider services are controlled substitutes; not theme rendering.
"""
import argparse,pathlib,re,subprocess,sys,tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from scaleresult_fixture import result_definitions,result_helpers
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[4]);a=p.parse_args();s=(a.source/'rom/intuition/windowframe.c').read_text();s='\n'.join(x for x in s.splitlines() if not x.startswith('#include'))
# Inject a cut after each actual Install statement; longjmp models interruption
# while every already-written scalar is live. Rollback executes the real body.
start=s.index('void InstallWindowFrame(');left=s.index('{',start);depth=1;end=left+1
while depth:
 depth+=(s[end]=='{')-(s[end]=='}');end+=1
body=s[left:end];body='\n'.join(line+' STEP();' if line.rstrip().endswith(';') else line for line in body.splitlines());s=s[:left]+body+s[end:]
# The real receipt contract and producer helpers; the real decorator refusal record.
decor=(a.source/'compiler/include/intuition/windecorclass.h').read_text(encoding='latin1')
refusal=decor[decor.index('struct wdpScaleRefusal\n{'):decor.index('#define WDP_REFUSE_NONE')].replace('STACKED ','')
refusal+=''.join(l.split('/*')[0].rstrip()+'\n' for l in decor.splitlines() if re.match(r'#define\s+WDP_REFUSE_\w+\s',l))
receipt=result_definitions(a.source)+result_helpers(a.source)
# The real CLI/Settings formatter consumes the receipts the real frame code wrote.
from scaleresult_fixture import detail_header
consumer=(a.source/'workbench/include/scaleresult.h').read_text()
formatter=consumer[consumer.index('#define SCALE_DIAGNOSTIC_SIZE'):consumer.index('struct ScaleAttempt\n')]+consumer[consumer.index('struct ScaleAttempt\n'):consumer.index('static inline BOOL ScaleAttemptSucceeded')]
formatter+=consumer[consumer.index('static inline const char *ScaleResultStage'):consumer.rindex('#endif')]
formatter=detail_header(a.source)+formatter
# The actual kernel log line of a detailed refusal (windowscale.c).
scale=(a.source/'rom/intuition/windowscale.c').read_text()
logger=scale[scale.index('static void log_scale_refusal('):scale.index('\nBOOL ApplyPreparedScreenScale(')]
formatter+='static char logline[512];static unsigned loglines;\n#define bug(...) (loglines++,snprintf(logline,sizeof logline,__VA_ARGS__))\n'+logger
pre=r'''
#include <assert.h>
#include <setjmp.h>
static jmp_buf cutJump;static unsigned cutOrdinal,cutAt;
#define STEP() do{if(++cutOrdinal==cutAt)longjmp(cutJump,1);}while(0)
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;typedef uint32_t ULONG;typedef int32_t LONG;typedef uint64_t UQUAD;typedef uintptr_t IPTR;typedef void Object;typedef void* APTR;typedef void* Msg;typedef uint8_t UBYTE;typedef uint16_t UWORD;
#define TRUE 1
#define FALSE 0
#define MEMF_ANY 1
#define MEMF_CLEAR 2
#define DRIF_DIRECTCOLOR 1
#define WFLG_BORDERLESS 1
#define WFLG_DRAGBAR 2
#define WFLG_CLOSEGADGET 4
#define WFLG_DEPTHGADGET 8
#define WFLG_SIZEGADGET 16
#define WFLG_GIMMEZEROZERO 32
#define WFLG_HASZOOM 64
#define WFLG_SIZEBRIGHT 128
#define WFLG_SIZEBBOTTOM 256
#define WFLG_TOOLBOX 512
#define WFLG_BACKDROP 1024
#define GFLG_RELRIGHT 1
#define GFLG_RELBOTTOM 2
#define GFLG_RELWIDTH 4
#define GFLG_RELHEIGHT 8
#define WDF_LBG_SYSTEMGADGET 1
#define WDA_UserBuffer 1
#define WDM_EXITWINDOW 10
#define WDM_GETSCALEDEFSIZE_SYSIMAGE 13
#define WDP_SCALE_UNAVAILABLE 2
#define WDP_SCALE_FAILED 0
#define WDP_SCALE_READY 1
#define SYSISIZE_MEDRES 1
#define TAG_IGNORE 99
#define IA_Height 15
#define IA_Width 18
#define WDM_PREPARESCALEWINDOW 16
#define WDM_LAYOUTSCALEBORDERGADGETS 17
#define SYSIA_Which 1
#define SYSIA_DrawInfo 2
#define SYSIA_ReferenceFont 3
#define SYSIA_RenderContext 4
#define SYSIA_Logical 5
#define TAG_DONE 0
#define SYSICLASS "sysi"
enum {DEPTHGAD,ZOOMGAD,SIZEGAD,CLOSEGAD,ICONIFYGAD,LOCKGAD,MUIGAD,POPUPGAD,SNAPSHOTGAD,JUMPGAD,DRAGBAR,NUM_SYSGADS};
/* The public values from intuition/imageclass.h and extensions.h: DEPTHIMAGE is 0. */
enum {DEPTHIMAGE=0x00,ZOOMIMAGE=0x01,SIZEIMAGE=0x02,CLOSEIMAGE=0x03,ICONIFYIMAGE=0x12,LOCKIMAGE=0x13,MUIIMAGE=0x14,POPUPIMAGE=0x15,SNAPSHOTIMAGE=0x16,JUMPIMAGE=0x17};
struct TextFont {unsigned tf_YSize;int refs;};
struct Screen;
struct IntuitionCustomize{struct Image*submenu;};
struct IntDrawInfo {unsigned dri_Flags;struct TextFont*dri_Font;struct Screen*dri_Screen;struct Image*dri_CheckMark,*dri_AmigaKey;struct IntuitionCustomize*dri_Customize;};
#define DrawInfo IntDrawInfo
struct ScreenRenderContext {unsigned src_Scale;struct IntDrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo;};
struct Image {short Width,Height;};
struct Gadget {struct Gadget*NextGadget;short LeftEdge,TopEdge,Width,Height;unsigned Flags;APTR GadgetRender;};
struct Window {struct Screen*WScreen;ULONG Flags;const char*Title;short BorderLeft,BorderTop,BorderRight,BorderBottom;short Width,Height;};
struct WindowFrameGeneration;
struct IntWindow {struct Window pub;char titlebuffer[64];BOOL ScaleAware;struct WindowFrameGeneration*FrameGeneration,*FrameGenerations;IPTR DecorUserBuffer;ULONG DecorUserBufferSize;ULONG extrabuttons;short sizeimage_width,sizeimage_height;struct Image*Checkmark,*AmigaKey,*SubMenuImage;unsigned FrameDefaultMenuImages;Object *sysgads[NUM_SYSGADS];};
struct Screen {int unused;};struct IntScreen {struct Screen pub;Object*WinDecorObj;IPTR DecorUserBuffer;};
#define IW(w) ((struct IntWindow*)(w))
#define IS(s) ((struct IntScreen*)(s))
#define SYSGAD(w,i) IW(w)->sysgads[i]
struct GfxBase {int unused;};struct IntuitionBase {struct GfxBase*GfxBase;};
#define GetPrivIBase(x) (x)
struct TagItem {IPTR ti_Tag,ti_Data;};
struct wdpGetDefSizeSysImage {ULONG MethodID;BOOL wdp_TrueColor;struct IntDrawInfo*wdp_Dri;struct TextFont*wdp_ReferenceFont;ULONG wdp_Which,wdp_SysiSize;ULONG *wdp_Width,*wdp_Height;};
struct wdpGetScaleDefSizeSysImage {struct wdpGetDefSizeSysImage base;const struct ScreenRenderContext*context;BOOL logical;ULONG requestedWidth,requestedHeight;APTR*prepared;struct wdpScaleRefusal*refusal;};
static BOOL WindowScaleVectorSize(struct wdpGetScaleDefSizeSysImage*m,struct IntuitionBase*b){(void)b;*m->base.wdp_Width=*m->base.wdp_Height=(14*(m->logical?100:m->context->src_Scale)+50)/100;return TRUE;}
struct wdpInitWindow {ULONG MethodID;BOOL wdp_TrueColor;IPTR wdp_UserBuffer;struct Screen*wdp_Screen;IPTR wdp_ScreenUserBuffer;};
struct wdpPrepareScaleWindow {struct wdpInitWindow base;const struct ScreenRenderContext*context;BOOL logical;struct TextFont*font;LONG width,height,borderTop;struct wdpScaleRefusal*refusal;};
struct wdpLayoutBorderGadgets {ULONG MethodID;BOOL wdp_TrueColor;struct IntDrawInfo*wdp_Dri;struct Window*wdp_Window;struct Gadget*wdp_Gadgets;ULONG wdp_Flags,wdp_ExtraButtons;IPTR wdp_UserBuffer;};
struct wdpLayoutScaleBorderGadgets {struct wdpLayoutBorderGadgets base;const struct ScreenRenderContext*context;BOOL logical;struct wdpScaleRefusal*refusal;};
struct wdpExitWindow {ULONG MethodID;BOOL wdp_TrueColor;IPTR wdp_UserBuffer;};
static unsigned ordinal,failAt,allocations,frees,images,disposeImages,providerCalls;static BOOL trap,typedPrepareFails,nonSquare;static unsigned exitCalls;
static BOOL layoutFails,objectFails,queryFails,queryRefuses;static unsigned bordersFail;static ULONG queryWidth,queryHeight;static IPTR bufferSize=8;
static void *allocate(size_t n){assert(!trap);if(++ordinal==failAt)return NULL;void*p=calloc(1,n);assert(p);allocations++;return p;}
static void *AllocMem(size_t n,unsigned flags){(void)flags;return allocate(n);}
static void FreeMem(void*p,size_t n){(void)n;assert(!trap&&p);free(p);frees++;}
static struct TextFont*SafeReopenFont(struct IntuitionBase*b,struct TextFont**f){(void)b;assert(!trap);if(++ordinal==failAt)return NULL;(*f)->refs++;return *f;}
#define CloseFont(f) ((void)GfxBase,(void)assert(!trap),(f)->refs--)
static BOOL ScreenRenderContextBorders(const struct ScreenRenderContext*c,BOOL logical,LONG*l,LONG*t,LONG*r,LONG*b){if(bordersFail==(logical?2u:1u))return FALSE;unsigned scale=logical?100:c->src_Scale;*l=(2*scale+50)/100;*t=(scale+50)/100;*r=*l;*b=*l;return TRUE;}
static void GetAttr(ULONG key,Object*provider,IPTR*out){assert(key==1&&provider==(void*)0x123&&!trap);*out=bufferSize;}
static IPTR DoMethodA(Object*provider,Msg message){assert(provider==(void*)0x123&&!trap);providerCalls++;ULONG id=*(ULONG*)message;if(id==10){exitCalls++;return TRUE;}if(id==13){struct wdpGetScaleDefSizeSysImage*m=message;assert(m->refusal&&!m->refusal->wsr_Code);if(queryRefuses){m->refusal->wsr_Code=WDP_REFUSE_IMAGE_EXTENT;m->refusal->wsr_Value[0]=28;m->refusal->wsr_Value[1]=38;memset(m->refusal->wsr_Name,'N',sizeof m->refusal->wsr_Name);return WDP_SCALE_FAILED;}if(queryFails)return WDP_SCALE_FAILED;if(queryWidth){*m->base.wdp_Width=queryWidth;*m->base.wdp_Height=queryHeight;return 1;}*m->base.wdp_Width=(14*(m->logical?100:m->context->src_Scale)+50)/100;*m->base.wdp_Height=((nonSquare?19:14)*(m->logical?100:m->context->src_Scale)+50)/100;return 1;}if(id==16){struct wdpPrepareScaleWindow*m=message;assert(m->refusal&&!m->refusal->wsr_Code);if(typedPrepareFails){m->refusal->wsr_Code=WDP_REFUSE_CONFIG;m->refusal->wsr_Value[0]=70000;m->refusal->wsr_Value[1]=m->context->src_Scale;strcpy((char*)m->refusal->wsr_Name,"BarJoinTB");return FALSE;}assert(m->base.wdp_UserBuffer);memset((void*)m->base.wdp_UserBuffer,0xa5,8);return TRUE;}assert(id==17);struct wdpLayoutScaleBorderGadgets*m=message;assert(m->refusal&&!m->refusal->wsr_Code);if(layoutFails){m->refusal->wsr_Code=WDP_REFUSE_SUPERCLASS;m->refusal->wsr_Value[0]=WDM_LAYOUTSCALEBORDERGADGETS;return FALSE;}assert(m->base.wdp_Window->Width>0);m->base.wdp_Gadgets->LeftEdge=-(LONG)(m->context->src_Scale/10);m->base.wdp_Gadgets->TopEdge=(m->context->src_Scale+50)/100;m->base.wdp_Gadgets->Flags=GFLG_RELRIGHT;return TRUE;}
static Object*NewObjectA(void*unused,const char*id,struct TagItem*tags){(void)unused;if(objectFails)return NULL;assert(!strcmp(id,"sysi"));const struct ScreenRenderContext*c=NULL;for(struct TagItem*t=tags;t->ti_Tag;t++)if(t->ti_Tag==SYSIA_RenderContext)c=(void*)t->ti_Data;assert(c);struct Image*i=allocate(sizeof(*i));if(!i)return NULL;i->Width=(14*c->src_Scale+50)/100;i->Height=i->Width;for(struct TagItem*t=tags;t->ti_Tag;t++){if(t->ti_Tag==IA_Width)i->Width=t->ti_Data;if(t->ti_Tag==IA_Height)i->Height=t->ti_Data;}images++;return (Object*)i;}
static void DisposeObject(void*i){assert(!trap);if(i){free(i);frees++;disposeImages++;}}
'''
main=r'''
static void setup(struct IntWindow*w,struct IntScreen*screen,const struct ScreenRenderContext*c,struct IntuitionBase*base,struct Gadget*g){memset(w,0,sizeof(*w));w->ScaleAware=1;w->pub.WScreen=&screen->pub;w->pub.Flags=WFLG_DRAGBAR|WFLG_CLOSEGADGET|WFLG_DEPTHGADGET;w->pub.Title="test";w->FrameDefaultMenuImages=7;w->pub.BorderLeft=2;w->pub.BorderTop=15;w->pub.BorderRight=2;w->pub.BorderBottom=2;w->pub.Width=300;w->pub.Height=200;assert(InitializeWindowFrame(&w->pub,&screen->pub,c,base));w->DecorUserBuffer=(IPTR)AllocMem(8,0);w->DecorUserBufferSize=8;assert(w->DecorUserBuffer);AdoptWindowFrameBuffer(&w->pub,TRUE);for(unsigned i=0;i<NUM_SYSGADS;i++){g[i]=(struct Gadget){.Width=14,.Height=14,.Flags=0x100};if(i!=DRAGBAR){g[i].GadgetRender=AllocMem(sizeof(struct Image),0);assert(g[i].GadgetRender);images++;}w->sysgads[i]=(void*)&g[i];}CaptureWindowFrameSystemImages(&w->pub);}
int main(void){struct GfxBase gfx={0};struct IntuitionBase base={&gfx};struct IntScreen screen={.WinDecorObj=(void*)0x123},otherScreen={.WinDecorObj=(void*)0x123};struct TextFont logical={13,1},fonts[13];struct Image stock[13][3];struct IntuitionCustomize customize[13];struct IntDrawInfo logicalD={.dri_Flags=1,.dri_Font=&logical,.dri_Screen=&screen.pub,.dri_Customize=&customize[0]},physicalD[13];struct ScreenRenderContext contexts[13];for(unsigned i=0;i<13;i++){fonts[i]=(struct TextFont){(13*(100+i*25)+50)/100,1};customize[i].submenu=&stock[i][2];physicalD[i]=(struct IntDrawInfo){.dri_Flags=1,.dri_Font=&fonts[i],.dri_Screen=&screen.pub,.dri_CheckMark=&stock[i][0],.dri_AmigaKey=&stock[i][1],.dri_Customize=&customize[i]};contexts[i]=(struct ScreenRenderContext){100+i*25,&logicalD,&physicalD[i]};}
/* The M1 Workbench at boot: IPrefs applies Arial 13 to the open 100% screen
   through a same-scale transaction. The startup Shell's console window is
   scale-aware and has a depth gadget (DEPTHIMAGE is 0); its frame must lay out
   in the new face, or the whole transaction refuses and the screen keeps topaz 8. */
{struct IntWindow con;struct Gadget cg[NUM_SYSGADS];struct TextFont topaz={8,1},arial={13,1};struct IntDrawInfo topazDI=physicalD[0],arialDI=physicalD[0];topazDI.dri_Font=&topaz;arialDI.dri_Font=&arial;struct ScreenRenderContext boot=contexts[0],prefs=contexts[0];boot.src_PhysicalDrawInfo=&topazDI;prefs.src_PhysicalDrawInfo=&arialDI;
 setup(&con,&screen,&boot,&base,cg);strcpy(con.titlebuffer,"AROS Shell");struct ScreenScaleResult cr;ScaleResultReset(&cr,100,100);ScaleResultSetStage(&cr,SSR_STAGE_FRAME,&con.pub);
 struct PreparedWindowFrame*p=PrepareWindowFrame(&con.pub,&prefs,&cr,&base);assert(p);
 if(!PrepareWindowFrameLayout(p,640,200,&cr,&base)){printf("workbench console refused the same-scale font change (stays topaz 8): detail %u slot %ld\n",(unsigned)cr.ssr_Detail,(long)cr.ssr_DetailValue[0]);abort();}
 trap=TRUE;InstallWindowFrame(p,FALSE);trap=FALSE;assert(WindowFrameDrawInfo(&con.pub)->dri_Font==&arial);ReleaseWindowFrame(p,TRUE,&base);assert(WindowFrameOwnsImage(&con.pub,cg[DEPTHGAD].GadgetRender)&&WindowFrameOwnsImage(&con.pub,cg[CLOSEGAD].GadgetRender));FreeWindowFrames(&con.pub,&base);assert(arial.refs==1&&topaz.refs==1);
 puts("PASS workbench console same-scale font change: depth gadget (type 0) laid out, frame font topaz 8 -> Arial 13");}
for(unsigned failure=0;failure<=13;failure++){struct IntWindow w;struct Gadget g[NUM_SYSGADS],old[NUM_SYSGADS];setup(&w,&screen,&contexts[0],&base,g);struct IntWindow original=w;memcpy(old,g,sizeof(old));unsigned beforeAlloc=allocations-frees;ordinal=0;failAt=failure;struct PreparedWindowFrame*p=PrepareWindowFrame(&w.pub,&contexts[4],NULL,&base);BOOL ready=p&&PrepareWindowFrameLayout(p,600,400,NULL,&base);failAt=0;assert(ready==(failure==0));assert(!memcmp(&w,&original,sizeof(w)));assert(!memcmp(g,old,sizeof(g)));if(ready){LONG l,t,r,b;assert(WindowFrameBorders(p,&l,&t,&r,&b)&&l==4&&t==30&&r==4&&b==4);unsigned a=allocations,f=frees,calls=providerCalls;trap=TRUE;InstallWindowFrame(p,FALSE);assert(WindowFrameDrawInfo(&w.pub)->dri_Font==&fonts[4]);assert(w.pub.BorderTop==30);InstallWindowFrame(p,TRUE);trap=FALSE;assert(a==allocations&&f==frees&&calls==providerCalls);assert(!memcmp(&w,&original,sizeof(w)));assert(!memcmp(g,old,sizeof(g)));cutOrdinal=0;InstallWindowFrame(p,FALSE);unsigned cuts=cutOrdinal;InstallWindowFrame(p,TRUE);for(unsigned cut=1;cut<=cuts;cut++){cutOrdinal=0;cutAt=cut;trap=TRUE;if(!setjmp(cutJump)){InstallWindowFrame(p,FALSE);assert(0);}cutAt=0;InstallWindowFrame(p,TRUE);trap=FALSE;assert(!memcmp(&w,&original,sizeof(w)));assert(!memcmp(g,old,sizeof(g)));}}ReleaseWindowFrame(p,FALSE,&base);assert(beforeAlloc==allocations-frees);FreeWindowFrames(&w.pub,&base);assert(allocations==frees);assert(fonts[0].refs==1);}
struct IntWindow w;struct Gadget g[NUM_SYSGADS];setup(&w,&screen,&contexts[0],&base,g);for(unsigned cycle=0;cycle<3;cycle++)for(unsigned i=0;i<13;i++){struct PreparedWindowFrame*p=PrepareWindowFrame(&w.pub,&contexts[i],NULL,&base);assert(p);unsigned beforeImages=images,beforeCalls=providerCalls;assert(PrepareWindowFrameLayout(p,300+i*25,200+i*15,NULL,&base));if(cycle){assert(images==beforeImages&&providerCalls==beforeCalls);}trap=TRUE;InstallWindowFrame(p,FALSE);trap=FALSE;assert(WindowFrameDrawInfo(&w.pub)->dri_Font==&fonts[i]);assert(w.Checkmark==&stock[i][0]&&w.AmigaKey==&stock[i][1]&&w.SubMenuImage==&stock[i][2]);ReleaseWindowFrame(p,TRUE,&base);assert(WindowFrameOwnsImage(&w.pub,g[CLOSEGAD].GadgetRender));}
for(unsigned birth=0;birth<=4;birth+=4){struct IntWindow client;struct Gadget cg[NUM_SYSGADS];setup(&client,&screen,&contexts[birth],&base,cg);unsigned factor=birth?2:1;client.pub.BorderLeft=2*factor;client.pub.BorderTop=15*factor;client.pub.BorderRight=2*factor;client.pub.BorderBottom=2*factor;RememberWindowFrameBorders(&client.pub);client.pub.BorderTop=18*factor;client.pub.BorderRight=20*factor;CaptureWindowFrameClientBorders(&client.pub);client.pub.Flags|=WFLG_SIZEBRIGHT;for(unsigned desired=0;desired<=4;desired+=4){struct PreparedWindowFrame*p=PrepareWindowFrame(&client.pub,&contexts[desired],NULL,&base);assert(p);LONG left,top,right,bottom;assert(WindowFrameBorders(p,&left,&top,&right,&bottom));assert(top==(desired?36:18)&&right==(desired?40:20));assert(PrepareWindowFrameLayout(p,300,200,NULL,&base));trap=TRUE;InstallWindowFrame(p,FALSE);trap=FALSE;ReleaseWindowFrame(p,TRUE,&base);}FreeWindowFrames(&client.pub,&base);}
{struct IntWindow client;struct Gadget cg[NUM_SYSGADS];setup(&client,&screen,&contexts[0],&base,cg);unsigned exits=exitCalls;struct PreparedWindowFrame*p=PrepareWindowFrame(&client.pub,&contexts[4],NULL,&base);assert(p);typedPrepareFails=TRUE;assert(!PrepareWindowFrameLayout(p,600,400,NULL,&base));typedPrepareFails=FALSE;ReleaseWindowFrame(p,FALSE,&base);assert(exitCalls==exits);FreeWindowFrames(&client.pub,&base);}
{struct IntWindow aspect;struct Gadget ag[NUM_SYSGADS];setup(&aspect,&screen,&contexts[0],&base,ag);nonSquare=TRUE;for(unsigned i=0;i<13;i++){unsigned scale=contexts[i].src_Scale;struct Image*im=CreateWindowFrameImage(&aspect.pub,&contexts[i],&physicalD[i],CLOSEIMAGE,(20*scale+50)/100,NULL,&base);assert(im&&im->Width==(15*scale+50)/100&&im->Height==(20*scale+50)/100);DisposeObject(im);}nonSquare=FALSE;FreeWindowFrames(&aspect.pub,&base);}
{struct IntWindow legacy;struct Gadget lg[NUM_SYSGADS];setup(&legacy,&screen,&contexts[0],&base,lg);legacy.ScaleAware=FALSE;legacy.Checkmark=&stock[0][0];legacy.AmigaKey=&stock[0][1];legacy.SubMenuImage=&stock[0][2];struct IntWindow before=legacy;unsigned provider=providerCalls;struct PreparedWindowFrame*p=PrepareWindowFrame(&legacy.pub,&contexts[4],NULL,&base);assert(p);LONG l,t,r,b;assert(WindowFrameBorders(p,&l,&t,&r,&b)&&l==2&&t==15&&r==2&&b==2);assert(PrepareWindowFrameLayout(p,123,77,NULL,&base));assert(PreparedWindowFrameDrawInfo(p)==WindowFrameDrawInfo(&legacy.pub)&&PreparedWindowFrameUserBuffer(p)==legacy.DecorUserBuffer);trap=TRUE;InstallWindowFrame(p,FALSE);InstallWindowFrame(p,TRUE);trap=FALSE;assert(!memcmp(&legacy,&before,sizeof legacy)&&provider==providerCalls);ReleaseWindowFrame(p,TRUE,&base);FreeWindowFrames(&legacy.pub,&base);}
{struct IntWindow menu;struct Gadget mg[NUM_SYSGADS];struct Image custom[3],fresh[3];struct TextFont newface={34,1};struct IntuitionCustomize newcustom={&fresh[2]};struct IntDrawInfo newDI=physicalD[4];newDI.dri_Font=&newface;newDI.dri_CheckMark=&fresh[0];newDI.dri_AmigaKey=&fresh[1];newDI.dri_Customize=&newcustom;struct ScreenRenderContext next=contexts[4];next.src_PhysicalDrawInfo=&newDI;setup(&menu,&screen,&contexts[4],&base,mg);struct PreparedWindowFrame*p=PrepareWindowFrame(&menu.pub,&next,NULL,&base);assert(p&&PrepareWindowFrameLayout(p,300,200,NULL,&base));trap=TRUE;InstallWindowFrame(p,FALSE);trap=FALSE;assert(WindowFrameDrawInfo(&menu.pub)->dri_Font==&newface&&menu.Checkmark==&fresh[0]&&menu.AmigaKey==&fresh[1]&&menu.SubMenuImage==&fresh[2]);ReleaseWindowFrame(p,TRUE,&base);menu.FrameDefaultMenuImages=0;menu.Checkmark=&custom[0];menu.AmigaKey=&custom[1];menu.SubMenuImage=&custom[2];p=PrepareWindowFrame(&menu.pub,&contexts[0],NULL,&base);assert(p&&PrepareWindowFrameLayout(p,300,200,NULL,&base));trap=TRUE;InstallWindowFrame(p,FALSE);trap=FALSE;assert(menu.Checkmark==&custom[0]&&menu.AmigaKey==&custom[1]&&menu.SubMenuImage==&custom[2]);ReleaseWindowFrame(p,TRUE,&base);FreeWindowFrames(&menu.pub,&base);assert(newface.refs==1);}
{struct ScreenScaleResult r;struct IntWindow d;struct Gadget dg[NUM_SYSGADS];struct PreparedWindowFrame*p;LONG l,t,rr,b;
#define RESET() (ScaleResultReset(&r,200,100),ScaleResultSetStage(&r,SSR_STAGE_FRAME,&d.pub))
#define DETAIL(code,a0,a1,a2,nm) (r.ssr_Status==SSR_FAILED&&r.ssr_Stage==SSR_STAGE_FRAME&&r.ssr_Window==(IPTR)&d.pub&&r.ssr_Detail==(code)&&r.ssr_DetailValue[0]==(LONG)(a0)&&r.ssr_DetailValue[1]==(LONG)(a1)&&r.ssr_DetailValue[2]==(LONG)(a2)&&!strcmp((char*)r.ssr_DetailName,(nm)))
#define FRESH() (p=PrepareWindowFrame(&d.pub,&contexts[4],&r,&base),assert(p))
setup(&d,&screen,&contexts[0],&base,dg);strcpy(d.titlebuffer,"Energy - arrows/wheel scroll");unsigned live=allocations-frees;
{struct IntWindow none;memset(&none,0,sizeof none);none.pub.WScreen=&screen.pub;RESET();ScaleResultSetStage(&r,SSR_STAGE_FRAME,&d.pub);assert(!PrepareWindowFrame(&none.pub,&contexts[4],&r,&base)&&DETAIL(SSR_DETAIL_FRAME_NO_GENERATION,0,0,0,""));}
{struct IntDrawInfo wrong=physicalD[4];wrong.dri_Screen=&otherScreen.pub;struct ScreenRenderContext bad=contexts[4];bad.src_PhysicalDrawInfo=&wrong;RESET();assert(!PrepareWindowFrame(&d.pub,&bad,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_CONTEXT,0,0,0,""));}
for(unsigned which=1;which<=2;which++){bordersFail=which;RESET();assert(!PrepareWindowFrame(&d.pub,&contexts[4],&r,&base)&&DETAIL(SSR_DETAIL_FRAME_CONTEXT_BORDERS,which==2,0,0,""));bordersFail=0;}
{struct IntDrawInfo nofont=physicalD[4];nofont.dri_Font=NULL;struct ScreenRenderContext c=contexts[4];c.src_PhysicalDrawInfo=&nofont;RESET();assert(!PrepareWindowFrame(&d.pub,&c,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_NO_FONT,0,0,0,""));}
{struct TextFont big={124,1};struct IntDrawInfo bd=physicalD[4];bd.dri_Font=&big;struct ScreenRenderContext c=contexts[4];c.src_PhysicalDrawInfo=&bd;RESET();assert(!PrepareWindowFrame(&d.pub,&c,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_BORDER_RANGE,1,128,0,""));}
d.pub.Flags|=WFLG_SIZEBBOTTOM;queryWidth=36;queryHeight=128;RESET();assert(!PrepareWindowFrame(&d.pub,&contexts[4],&r,&base)&&DETAIL(SSR_DETAIL_FRAME_SIZE_IMAGE,1,36,128,""));queryWidth=queryHeight=0;
queryFails=TRUE;RESET();assert(!PrepareWindowFrame(&d.pub,&contexts[4],&r,&base)&&DETAIL(SSR_DETAIL_FRAME_SIZE_IMAGE,0,0,0,""));queryFails=FALSE;
queryRefuses=TRUE;RESET();assert(!PrepareWindowFrame(&d.pub,&contexts[4],&r,&base)&&r.ssr_Detail==SSR_DETAIL_DECORATOR&&r.ssr_DetailMethod==WDM_GETSCALEDEFSIZE_SYSIMAGE&&r.ssr_DecorRefusal==WDP_REFUSE_IMAGE_EXTENT&&r.ssr_DetailValue[0]==28&&r.ssr_DetailValue[1]==38&&strlen((char*)r.ssr_DetailName)==sizeof(r.ssr_DetailName)-1);queryRefuses=FALSE;
/* A size gadget at 200%: the queried image height becomes the bottom border and lays out. */
RESET();FRESH();assert(WindowFrameBorders(p,&l,&t,&rr,&b)&&b==28&&t==30);assert(PrepareWindowFrameLayout(p,640,420,&r,&base));ReleaseWindowFrame(p,FALSE,&base);d.pub.Flags&=~WFLG_SIZEBBOTTOM;
RESET();FRESH();assert(!PrepareWindowFrameLayout(p,0,400,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_LAYOUT_SIZE,0,400,0,""));ReleaseWindowFrame(p,FALSE,&base);
bufferSize=0x80000000u;RESET();FRESH();assert(!PrepareWindowFrameLayout(p,600,400,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_USER_BUFFER,0x80000000u,0,0,""));ReleaseWindowFrame(p,FALSE,&base);bufferSize=8;
typedPrepareFails=TRUE;RESET();FRESH();assert(!PrepareWindowFrameLayout(p,600,400,&r,&base)&&DETAIL(SSR_DETAIL_DECORATOR,70000,200,0,"BarJoinTB")&&r.ssr_DetailMethod==WDM_PREPARESCALEWINDOW&&r.ssr_DecorRefusal==WDP_REFUSE_CONFIG);ReleaseWindowFrame(p,FALSE,&base);typedPrepareFails=FALSE;
{APTR keep=dg[DRAGBAR].GadgetRender;dg[DRAGBAR].GadgetRender=&dg[0];RESET();FRESH();assert(!PrepareWindowFrameLayout(p,600,400,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_UNTYPED_GADGET,DRAGBAR,0,0,""));ReleaseWindowFrame(p,FALSE,&base);dg[DRAGBAR].GadgetRender=keep;}
RESET();FRESH();queryFails=TRUE;assert(!PrepareWindowFrameLayout(p,600,400,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_IMAGE_QUERY,0,0,0,"depth"));queryFails=FALSE;ReleaseWindowFrame(p,FALSE,&base);
RESET();FRESH();queryRefuses=TRUE;assert(!PrepareWindowFrameLayout(p,600,400,&r,&base)&&r.ssr_Detail==SSR_DETAIL_DECORATOR&&r.ssr_DetailMethod==WDM_GETSCALEDEFSIZE_SYSIMAGE);queryRefuses=FALSE;ReleaseWindowFrame(p,FALSE,&base);
RESET();FRESH();queryWidth=30000;queryHeight=14;assert(!PrepareWindowFrameLayout(p,600,400,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_IMAGE_EXTENT,64286,30,30,"depth"));queryWidth=queryHeight=0;ReleaseWindowFrame(p,FALSE,&base);
RESET();FRESH();objectFails=TRUE;assert(!PrepareWindowFrameLayout(p,600,400,&r,&base)&&DETAIL(SSR_DETAIL_FRAME_IMAGE_OBJECT,30,30,0,"depth"));objectFails=FALSE;ReleaseWindowFrame(p,FALSE,&base);
for(unsigned at=1;at<=3;at++){RESET();ordinal=0;failAt=at;p=PrepareWindowFrame(&d.pub,&contexts[4],&r,&base);BOOL ready=p&&PrepareWindowFrameLayout(p,600,400,&r,&base);failAt=0;assert(!ready&&r.ssr_Reason==SSR_REASON_NO_MEMORY&&r.ssr_Detail==SSR_DETAIL_NONE);if(p)ReleaseWindowFrame(p,FALSE,&base);}
/* The Energy refusal end to end: real frame body, real receipt, real formatter. */
layoutFails=TRUE;RESET();FRESH();assert(!PrepareWindowFrameLayout(p,600,400,&r,&base)&&DETAIL(SSR_DETAIL_DECORATOR,WDM_LAYOUTSCALEBORDERGADGETS,0,0,"")&&r.ssr_DetailMethod==WDM_LAYOUTSCALEBORDERGADGETS&&r.ssr_DecorRefusal==WDP_REFUSE_SUPERCLASS);ReleaseWindowFrame(p,FALSE,&base);layoutFails=FALSE;
{struct ScaleAttempt attempt={.result=r,.requested=200,.actual=100,.screen_available=TRUE,.api_available=TRUE,.actual_known=TRUE};char text[SCALE_DIAGNOSTIC_SIZE],want[SCALE_DIAGNOSTIC_SIZE];ScaleAttemptFormat(&attempt,text,sizeof text);
 snprintf(want,sizeof want,"Cannot apply 200%%. Window frame preparation for Window \"Energy - arrows/wheel scroll\" [%p]: frame gadget layout was refused by the window decorator (WDM_LAYOUTSCALEBORDERGADGETS): its superclass refused WDM_LAYOUTSCALEBORDERGADGETS. Current scale: 100%%.",(void*)&d.pub);if(strcmp(text,want)){printf("got  %s\nwant %s\n",text,want);abort();}puts(text);
 log_scale_refusal(&r);snprintf(want,sizeof want,"[SCALE] 200%% refused at stage %u, window %p 'Energy - arrows/wheel scroll': frame gadget layout was refused by the window decorator (WDM_LAYOUTSCALEBORDERGADGETS): its superclass refused WDM_LAYOUTSCALEBORDERGADGETS\n",SSR_STAGE_FRAME,(void*)&d.pub);if(loglines!=1||strcmp(logline,want)){printf("log  %s\nwant %s\n",logline,want);abort();}fputs(logline,stdout);
 r.ssr_Detail=SSR_DETAIL_NONE;log_scale_refusal(&r);ScaleResultFinish(&r,SSR_SUCCESS,200);r.ssr_Detail=SSR_DETAIL_DECORATOR;log_scale_refusal(&r);assert(loglines==1);}
assert(live==allocations-frees);FreeWindowFrames(&d.pub,&base);puts("PASS actual frame refusal receipts: no generation, foreign context, physical/logical borders, no font, border range with side/value, size image status/extent and decorator refusal, 200% size gadget, layout size, buffer size, untyped slot, image query/extent/object with image name, prepare/layout decorator refusals with method and owned bounded name, allocation reasons, exact formatted Energy line");}
assert(fonts[0].refs==2);struct IntDrawInfo wrong=physicalD[0];wrong.dri_Screen=&otherScreen.pub;struct ScreenRenderContext bad=contexts[0];bad.src_PhysicalDrawInfo=&wrong;assert(!PrepareWindowFrame(&w.pub,&bad,NULL,&base));FreeWindowFrames(&w.pub,&base);assert(allocations==frees&&images==disposeImages&&fonts[0].refs==1);puts("PASS actual frame C:13scales+cache revisits,13actual allocation failures, immutable fonts/images, no-allocation install/every-statement-cut rollback, canonical non-square icon aspect, exact oldstate, clientborder birth100/200 exactreset, failedtypedprepare noExit, wrong-screen refusal, balanced close");return 0;}
'''
variants=[('actual',s),('skip-border-rollback',s.replace('rollback ? frame->oldborder : frame->border','frame->border')),('wrong-frame-font',s.replace('IW(window)->ScaleAware ?\n        frame->context->src_PhysicalDrawInfo : frame->context->src_LogicalDrawInfo','frame->context->src_LogicalDrawInfo')),('lose-owned-images',s.replace('DisposeObject(generation->gadgets[i].image);','(void)generation->gadgets[i].image;')),('lose-client-minimum',s.replace('generation->clientMinimum[i] = borders[i];','generation->clientMinimum[i] = 0;')),('exit-unsupported',s.replace('if (generation->initialized)\n    {','if (TRUE)\n    {')),('stale-stock-defaults',s.replace('if (IW(window)->FrameDefaultMenuImages & 1)', 'if (FALSE)')),('disable-cache',s.replace('generation->context == frame->context && generation->width == width','FALSE && generation->context == frame->context && generation->width == width')),
 ('layout-refusal-unreported',s.replace('frame_decorator_refusal(result, WDM_LAYOUTSCALEBORDERGADGETS, &refusal);','(void)0;')),
 ('prepare-refusal-wrong-method',s.replace('frame_decorator_refusal(result, WDM_PREPARESCALEWINDOW, &refusal);','frame_decorator_refusal(result, WDM_LAYOUTSCALEBORDERGADGETS, &refusal);')),
 ('border-side-lost',s.replace('ScaleResultDetail(result, SSR_DETAIL_FRAME_BORDER_RANGE, side,','ScaleResultDetail(result, SSR_DETAIL_FRAME_BORDER_RANGE, 0,')),
 ('size-image-generic',s.replace('ScaleResultDetail(result, SSR_DETAIL_FRAME_SIZE_IMAGE, status,','ScaleResultDetail(result, SSR_DETAIL_NONE, status,')),
 ('image-name-wrong',s.replace('case DEPTHIMAGE: return "depth";','case DEPTHIMAGE: return "close";')),
 ('object-refusal-unreported',s.replace('    if (!image)\n        ScaleResultDetail(','    if (FALSE)\n        ScaleResultDetail(')),
 ('depth-image-untyped',s.replace('default: return NO_SYSTEM_IMAGE;','default: return 0;').replace('GadgetRender && type == NO_SYSTEM_IMAGE)','GadgetRender && !type)').replace('if (type != NO_SYSTEM_IMAGE)','if (type)')),
 ('oom-as-refusal',s.replace('if (!frame) { ScaleResultReason(result, SSR_REASON_NO_MEMORY); return NULL; }','if (!frame) return NULL;'))]
with tempfile.TemporaryDirectory(prefix='window-frame-') as t:
    t=pathlib.Path(t)
    for name,code in variants:
        c=t/(name+'.c');exe=t/name;c.write_text(pre.replace('static unsigned ordinal,',refusal+receipt+'static unsigned ordinal,',1)+code+formatter+main);subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if name=='actual':
            if q.returncode:raise SystemExit(q.stderr)
            print(q.stdout,end='')
        elif q.returncode==0:raise SystemExit('escaped mutation '+name)
        else:print('REJECTED '+name)

# Independently execute the real constructor publication wrapper. The body is
# a scene-mutation sentinel; resources/scene cannot be touched before guards.
constructor=(a.source/'rom/intuition/openwindow.c').read_text()
start=constructor.rindex('static VOID int_openwindow(');left=constructor.index('{',start);depth=1;right=left+1
while depth:
    depth+=(constructor[right]=='{')-(constructor[right]=='}');right+=1
wrapper=constructor[left+1:right-1]
racepre=r'''
#include <assert.h>
#include <stdio.h>
typedef int BOOL;typedef unsigned long ULONG;
#define FALSE 0
#define TRUE 1
#define RESOURCE_SCREEN 1
struct Screen {int unused;};struct ScreenRenderContext {int unused;};struct Window {struct Screen*WScreen;BOOL ScaleAware;const struct ScreenRenderContext*frame,*expected;};struct IntuitionBase {struct LayersBase *LayersBase;};struct OpenWindowActionMsg{struct Window*window;BOOL success;};
#define IW(x) (x)
#define GetPrivIBase(x) (x)
static BOOL ibase,root,live,changeOnRoot;static unsigned bodyCalls;static const struct ScreenRenderContext*current,*next;
static ULONG LockIBase(ULONG unused){(void)unused;assert(!ibase&&!root);ibase=1;return 9;}
static void UnlockIBase(ULONG key){assert(key==9&&ibase&&!root);ibase=0;}
static BOOL ResourceExisting(struct Screen*s,int type,struct IntuitionBase*b){(void)s;(void)b;assert(ibase&&!root&&type==1);return live;}
static void lockRoot(struct Screen*s){(void)s;assert(ibase&&!root&&live);root=1;if(changeOnRoot)current=next;}
static void unlockRoot(struct Screen*s){(void)s;assert(ibase&&root);root=0;}
#define LOCK_REFRESH(s) lockRoot(s)
#define UNLOCK_REFRESH(s) unlockRoot(s)
static const struct ScreenRenderContext*WindowFrameContext(struct Window*w){return w->frame;}
static const struct ScreenRenderContext*WindowBoundRenderContext(struct Window*w){return w->expected;}
static BOOL BoundScreenRenderContext(const struct ScreenRenderContext*c,struct Screen*s){(void)s;assert(ibase&&root);return c==current;}
static const struct ScreenRenderContext*CurrentScreenRenderContext(struct Screen*s){(void)s;assert(ibase&&root);return current;}
static void int_openwindow_body(struct OpenWindowActionMsg*m,struct IntuitionBase*b){(void)b;struct Window*w=m->window;if(w->ScaleAware||w->expected){assert(ibase&&root&&live);if(w->ScaleAware)assert(w->frame==current);if(w->expected)assert(w->expected==current);}else assert(!ibase&&!root);bodyCalls++;m->success=1;}
'''
racemain=r'''
int main(void){struct Screen screen;struct ScreenRenderContext A,B;struct IntuitionBase base;struct Window window={&screen,1,&A,NULL};struct OpenWindowActionMsg msg={&window,0};for(unsigned mode=0;mode<11;mode++){window.ScaleAware=mode!=4&&mode!=5&&mode!=6&&mode!=10;window.frame=mode==7?&B:&A;window.expected=mode==8?&B:(mode==5||mode==6||mode==7||mode==9||mode==10?&A:NULL);current=mode==1||mode==6||mode==7?&B:&A;next=&B;live=mode!=2;changeOnRoot=mode==3||mode==10;bodyCalls=0;msg.success=0;BOOL success=mode==0||mode==4||mode==5||mode==9;action(&msg,&base);assert(!ibase&&!root);assert(bodyCalls==(unsigned)success);assert(msg.success==success);}puts("PASS actual OpenWindow combined publication guard: 11 matching/stale/disposed/lock-boundary/legacy/bound cases; immutable client snapshot and aware frame checked independently under IBase-before-root through publication");return 0;}

'''
with tempfile.TemporaryDirectory(prefix='frame-publication-') as temp:
    temp=pathlib.Path(temp)
    for name,code in [('actual',wrapper),('accept-stale',wrapper.replace('WindowFrameContext(window) == CurrentScreenRenderContext(screen)','TRUE')),('skip-bound-snapshot',wrapper.replace('BoundScreenRenderContext(expected, screen)','TRUE')),('skip-screen-lifetime',wrapper.replace('ResourceExisting(screen, RESOURCE_SCREEN, IntuitionBase)','TRUE'))]:
        text=racepre+'static void action(struct OpenWindowActionMsg*msg,struct IntuitionBase*IntuitionBase){'+code+'}'+racemain
        c=temp/(name+'.c');exe=temp/name;c.write_text(text)
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if name=='actual':
            if result.returncode:raise SystemExit(result.stderr)
            print(result.stdout,end='')
        elif result.returncode==0:raise SystemExit('escaped publication mutation '+name)
        else:print('REJECTED '+name)

# Frame retirement sends WDM_EXITWINDOW through window->WScreen. The actual
# int_closewindow must retire generations before it poisons that pointer, or
# the input.device task faults inside CloseWindow and input stops for good.
close=(a.source/'rom/intuition/closewindow.c').read_text()
start=close.index('VOID int_closewindow(struct CloseWindowActionMsg *msg,\n                     struct IntuitionBase *IntuitionBase)\n{')
body=close[start:close.index('} /* int_closewindow */',start)]
free=body.find('FreeWindowFrames(msg->window, IntuitionBase);');poison=body.find('msg->window->WScreen = (struct Screen *)0xC0DEBAD0;')
if free<0 or poison<0 or body.count('FreeWindowFrames(')!=1 or free>poison:raise SystemExit('CloseWindow frees frame generations after poisoning WScreen')
print('PASS actual int_closewindow retires frame generations once, before WScreen is poisoned')
