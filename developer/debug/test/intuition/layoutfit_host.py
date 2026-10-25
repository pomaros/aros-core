#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual WindowFit + coordinator + cached GI; external scene/frame/OS adapters.
Measured ASL FR baseline316x223/150474x345 is cross-bound to its real metrics
fixture; this test does not duplicate the application layout/font formulas.
"""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[4]
fixture=root/'developer/debug/test/intuition/preparedscale_host.py'
ns={'__file__':str(fixture),'__name__':'layout_dependencies'}
exec(fixture.read_text().split('source=prefix+headers+boundaries+body+main')[0],ns)
strip=ns['strip']
def actual(text,name):
 marker=re.search(r'(?m)^.*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',text);assert marker,name
 a=marker.start();b=text.index('{',a);i=b+1;n=1
 while n:n+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[a:i]
prefix=ns['prefix'].replace('typedef int BOOL;','typedef int64_t QUAD;typedef int BOOL;')
aware=strip((root/'rom/intuition/awaregeometry.h').read_text())
public=(root/'compiler/include/intuition/intuition.h').read_text(encoding='latin1')
descriptor=re.search(r'struct ScaleWindowGeometry\s*\{[^}]*\};',public).group(0)
prefix=prefix.replace('struct WindowScaleShapes;',aware+'\n'+descriptor+'\nstruct WindowScaleShapes;',1)
prefix=prefix.replace('struct RastPort{void *BitMap;','struct RastPort{struct Layer*Layer;void *BitMap;')
# The coordinator model's Layer already names its Layer_Info.
assert 'struct Layer_Info*LayerInfo;};' in prefix
prefix=prefix.replace('struct Layer_Info{int dummy;};','struct Layer_Info{ULONG Flags;};')
prefix=prefix.replace('struct DrawInfo{struct TextFont*','struct DrawInfo{struct Screen*dri_Screen;struct TextFont*').replace('struct IntDrawInfo{struct TextFont*','struct IntDrawInfo{struct Screen*dri_Screen;struct TextFont*')
prefix=prefix.replace('BOOL gzz,care,ScaleAware,CustomShape,ScaleGeometryEnabled;', '''BOOL gzz,care,ScaleAware,CustomShape,ScaleGeometryEnabled,FitCanonicalValid;
struct Window*parent;LONG RelLeftEdge,RelTopEdge;UWORD MinWidth,MinHeight,MaxWidth,MaxHeight,FitAutomatic;
struct AwareGeometry ScaleGeometry;const struct ScreenRenderContext*LayoutMinContext;
LONG LayoutMinWidth,LayoutMinHeight,FitLeft,FitTop,FitWidth,FitHeight;struct RastPort*RPort,*BorderRPort;''')
prefix=prefix.replace('struct Screen{struct Screen*','struct Screen{LONG Width,Height;struct Screen*')
prefix=prefix.replace('struct IntScreen{struct Screen Screen;','struct IntScreen{struct Screen Screen;UWORD FitBatch;')
prefix=prefix.replace('struct IntuitionBase{struct Screen*','struct IntuitionBase{struct {ULONG ic_Flags;}IControlPrefs;struct Interrupt*InputHandler;struct Screen*')
prefix+='''
static BOOL expectlayout=TRUE;static unsigned auxrefresh;
#define IS_HIDD_BM(b) ((b)!=NULL)
#define ICF_OFFSCREENLAYERS 1
#define LIFLG_SUPPORTS_OFFSCREEN_LAYERS 1
struct NewWindow{LONG LeftEdge,TopEdge,Width,Height,MinWidth,MinHeight,MaxWidth,MaxHeight;};
static struct Layer*WindowCompositionLayer(struct Window*w){return w->bl?w->bl:w->wl;}
static BOOL DoMoveSizeWindow(struct Window*w,LONG x,LONG y,LONG a,LONG b,BOOL n,struct IntuitionBase*i){assert(0);return FALSE;}
static void NotifyWindowGeometry(struct Window*w,BOOL n,struct IntuitionBase*i){assert(0);}
'''
headers=ns['headers']
boundaries=ns['boundaries']
a=boundaries.index('struct WindowFitTransaction{');b=boundaries.index('static struct LogicalScreenInfo*',a)
boundaries=boundaries[:a]+actual(boundaries,'gate')+'\n'+boundaries[b:]
# Layout-only source must never invoke any of the screen/font/cursor generation
# preparations. Normal scale mode remains exercised by preparedscale_host.
# Requester rendering is independently qualified by requester fixture. The
# target here is a normal active string window, not an OS-owned requester.
a=boundaries.index('static BOOL PrepareSystemRequesterScale(');b=boundaries.index('\nstatic BOOL SystemRequesterScaleBounds',a)
boundaries=boundaries[:a]+'''static BOOL PrepareSystemRequesterScale(struct Window*w,const struct ScreenRenderContext*c,const struct WindowFitGeometry*g,LONG l,LONG t,LONG r,LONG b,struct PreparedSystemRequesterScale**out,struct IntuitionBase*base){*out=NULL;return TRUE;}'''+boundaries[b:]
boundaries=boundaries.replace('assert(p->wdp_Font->tf_YSize==(p->wdp_Window->ScaleAware?26:13));','') # actually in prefix below
prefix=prefix.replace('assert(p->wdp_Font->tf_YSize==(p->wdp_Window->ScaleAware?26:13));','').replace('assert(p->wdp_BorderTop==(p->wdp_Window->ScaleAware?26:13));','').replace('assert(p->wdp_UserBuffer==(p->wdp_Window->ScaleAware?200:100));','')
boundaries=boundaries.replace('static void DrawAuxiliaryLayerRefresh(struct Screen*s,struct IntuitionBase*b){(void)s;(void)b;assert(!hardtrap);}', 'static void DrawAuxiliaryLayerRefresh(struct Screen*s,struct IntuitionBase*b){(void)s;(void)b;assert(!hardtrap&&!events);auxrefresh++;}')
boundaries=boundaries.replace('assert(!events);assert(IS(b->FirstScreen)', 'assert(!events&&auxrefresh);assert(IS(b->FirstScreen)')
boundaries=boundaries.replace('assert(IS(b->FirstScreen)->Scale==200&&IS(b->FirstScreen)->RenderContext->scale==200);','assert(IS(b->FirstScreen)->Scale==IS(b->FirstScreen)->RenderContext->scale);')
boundaries=boundaries.replace('for(ULONG i=0;i<p->count;i++)assert(p->specs[i].flags&PNF_SCALE);','for(ULONG i=0;i<p->count;i++)assert(expectlayout ? !(p->specs[i].flags&PNF_SCALE) : (p->specs[i].flags&PNF_SCALE));')
boundaries=boundaries.replace('assert(p->w->framevalue==(rollback?100:200));','assert(p->w->framevalue==(rollback?100:200));')
a=boundaries.index('/* Active input cache internals')
boundaries=boundaries[:a]
# Use the COMPLETE actual frame installer at the coordinator boundary. The
# external frame constructor allocates detached descriptors only; it cannot
# invent NULL acceptance or hide fields read by the real installer.
frame_source=(root/'rom/intuition/windowframe.c').read_text()
intern_source=(root/'rom/intuition/intuition_intern.h').read_text()
def structure(text,name):return re.search(r'struct '+name+r'\s*\{.*?\n\};',text,re.S).group(0)
system_enum=re.search(r'enum\s*\{[^}]*NUM_SYSGADS\s*\};',intern_source,re.S).group(0)
# These fields are real native concepts; host model ABI remains semantic.
prefix=prefix.replace('struct Screen;\nstruct Window{','struct Screen;\nstruct Window{')
prefix=prefix.replace('void*Composition;};','void*Composition;struct WindowFrameGeneration*FrameGeneration;ULONG DecorUserBufferSize,FrameDefaultMenuImages;struct Image*Checkmark,*AmigaKey,*SubMenuImage;LONG sizeimage_width,sizeimage_height;};')
prefix=prefix.replace('struct DrawInfo{','struct Image;struct IntuitionCustomize{struct Image*submenu;};\nstruct DrawInfo{',1)
prefix=prefix.replace('struct IntDrawInfo{struct Screen*dri_Screen;struct TextFont*dri_Font;};','struct IntDrawInfo{struct Screen*dri_Screen;struct TextFont*dri_Font;struct Image*dri_CheckMark,*dri_AmigaKey;struct IntuitionCustomize*dri_Customize;};')
prefix+='''
typedef void Object;
struct Gadget{UWORD GadgetType,Activation,Flags;LONG LeftEdge,TopEdge,Width,Height;void*GadgetRender;};
#define GFLG_RELRIGHT (1<<4)
#define GFLG_RELBOTTOM (1<<3)
#define GFLG_RELWIDTH (1<<5)
#define GFLG_RELHEIGHT (1<<6)
'''
frame_structs=system_enum+'\n'+structure(frame_source,'FrameGadget')+'\n'+structure(frame_source,'WindowFrameGeneration')+'\n'+structure(frame_source,'PreparedWindowFrame').replace('    BOOL owned;','    BOOL owned;struct PreparedWindowFrame*retainedNext;unsigned modelOldValue;')
real_install=actual(frame_source,'InstallWindowFrame')
# A trailing test-only diagnostic stamp preserves existing external checks;
# no native installer statement, guard, or branch is replaced.
real_install=real_install[:-1]+'frame->window->framevalue = rollback ? frame->modelOldValue : 200;\n}'
frame_adapters=r'''
static struct PreparedWindowFrame*retainedframes;
static struct PreparedWindowFrame*PrepareWindowFrame(struct Window*w,const struct ScreenRenderContext*c,struct ScreenScaleResult*r,struct IntuitionBase*b){(void)b;(void)r;struct PreparedWindowFrame*p=allocate(sizeof*p);if(!p)return NULL;p->window=w;p->context=c;p->previous=w->FrameGeneration;p->modelOldValue=w->framevalue;
p->border[0]=p->oldborder[0]=w->BorderLeft;p->border[1]=p->oldborder[1]=w->BorderTop;p->border[2]=p->oldborder[2]=w->BorderRight;p->border[3]=p->oldborder[3]=w->BorderBottom;p->oldbuffer=w->DecorUserBuffer;p->oldbufferSize=w->DecorUserBufferSize;p->oldMenuImages[0]=w->Checkmark;p->oldMenuImages[1]=w->AmigaKey;p->oldMenuImages[2]=w->SubMenuImage;p->oldSizeWidth=w->sizeimage_width;p->oldSizeHeight=w->sizeimage_height;return p;}
static BOOL WindowFrameBorders(const struct PreparedWindowFrame*p,LONG*l,LONG*t,LONG*r,LONG*b){if(!gate())return FALSE;*l=p->border[0];*t=p->border[1];*r=p->border[2];*b=p->border[3];return TRUE;}
static BOOL PrepareWindowFrameLayout(struct PreparedWindowFrame*p,LONG w,LONG h,struct ScreenScaleResult*r,struct IntuitionBase*b){(void)b;(void)r;assert(p&&w>0&&h>0&&!pixellease);if(!gate())return FALSE;p->generation=allocate(sizeof*p->generation);if(!p->generation)return FALSE;p->generation->dri.dri_Screen=p->window->WScreen;p->generation->dri.dri_Font=p->context->physical;p->generation->context=p->context;p->generation->userbuffer=p->context->scale;return TRUE;}
static void ReleaseWindowFrame(struct PreparedWindowFrame*p,BOOL committed,struct IntuitionBase*b){(void)b;assert(!pixellease);if(committed){p->retainedNext=retainedframes;retainedframes=p;}else{release(p->generation);release(p);}}
'''
a=boundaries.index('struct PreparedWindowFrame{');b=boundaries.index('struct LayerGeometryBatch{',a)
boundaries=boundaries[:a]+frame_structs+'\n'+frame_adapters+actual(frame_source,'PreparedWindowFrameDrawInfo')+'\n'+actual(frame_source,'PreparedWindowFrameUserBuffer')+'\n'+real_install+'\n'+boundaries[b:]

# Actual macros/domain and generic cached input tuple, semantic host structures.
intern=(root/'rom/intuition/intuition_intern.h').read_text()
def macro(name):
 lines=intern[intern.index('#define '+name+'('):].splitlines();result=[lines[0]]
 while result[-1].endswith('\\'):result.append(lines[len(result)])
 return '\n'.join(result)
gi='''
#define GTYP_SCRGADGET 32
#define GTYP_GZZGADGET 16
#define GTYP_REQGADGET 64
#define GTYP_GADGETTYPE 255
#define GTYP_SYSGADGET 128
#define GACT_RIGHTBORDER 1
#define GACT_LEFTBORDER 2
#define GACT_TOPBORDER 4
#define GACT_BOTTOMBORDER 8
struct IBox{LONG Left,Top,Width,Height;};

struct GadgetInfo{struct Screen*gi_Screen;struct Window*gi_Window;struct Requester*gi_Requester;
struct RastPort*gi_RastPort;struct Layer*gi_Layer;struct DrawInfo*gi_DrInfo;struct IBox gi_Domain;};
struct IIHData{struct Gadget*ActiveGadget,*ActiveSysGadget;struct GadgetInfo GadgetInfo;struct RastPort GadgetInfoRastPort;};
struct Interrupt{struct IIHData*is_Data;};
'''
gi+='\n'.join(macro(x) for x in ['IS_GZZ_GADGET','IS_BORDER_GADGET','IS_REQ_GADGET','IS_SCREEN_GADGET','SET_GI_RPORT'])+'\n'
gi+=actual((root/'rom/intuition/gadgets.c').read_text(encoding='latin1'),'GetGadgetDomain')+'\n'
gi+=strip((root/'rom/intuition/activegadgetscale.h').read_text())+strip((root/'rom/intuition/activegadgetscale.c').read_text())
# The actual fit planner runs inside the actual coordinator, not a parallel
# handwritten implementation of the new minimum/ownership semantics.
fit=strip((root/'rom/intuition/windowfit.c').read_text())
fit=fit[fit.index('struct FitGeometry'):]
fitprefix=strip((root/'rom/graphics/scalecompose.h').read_text())
# Include every scalar publication in real WindowFitInstall in cut coverage.
a=fit.index('void WindowFitInstall(');b=fit.index('\nstatic BOOL legacy_window',a)
fi=fit[a:b]
fi=re.sub(r'([^\n;]*;)',lambda m:m.group(0)+(' FIT_CUT();' if '=' in m.group(0) and not any(x in m.group(0) for x in ['for (','struct ','ULONG ']) else ''),fi)
fit=fit[:a]+fi+fit[b:]
fitprefix+='''
struct ScaleInstall;static struct ScaleInstall*cutprepared;
static void fit_cut(BOOL rollback,struct IntuitionBase*IntuitionBase);
#define FIT_CUT() do{fit_cut(rollback, NULL);if(aborted&&!rollback)return;}while(0)
'''
body=ns['body']
body=body.replace('++cut_count; if (cutting', 'if(aborted)return; ++cut_count; if (cutting')
start=body.index('static void install_scale(struct ScaleInstall *prepared, BOOL rollback,')
opening=body.index('{',start)
body=body[:opening+1]+'\n    if(!rollback)cutprepared=prepared;'+body[opening+1:]
body+='''
static struct IntuitionBase *cutbase;
static void fit_cut(BOOL rollback,struct IntuitionBase*unused){(void)unused;if(rollback)return;
++cut_count;if(cutting&&cut_count==cut_at){install_scale(cutprepared,TRUE,cutbase);aborted=1;}}
'''
publicsrc=(root/'rom/intuition/fitwindowlayout.c').read_text()
a=publicsrc.index('struct FitWindowLayoutAction');b=publicsrc.index('/****************************************************************************',a)
publicbody=publicsrc[a:b]
a=publicsrc.index('{',publicsrc.index('AROS_LH4(BOOL, FitWindowLayout'))
publicbody+='static BOOL FitWindowLayout(struct Window*window,const struct ScreenRenderContext*context,LONG physicalMinWidth,LONG physicalMinHeight,struct IntuitionBase*IntuitionBase)'+publicsrc[a:]
publicprefix=r''' 
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define APTR void*
#define NT_PROCESS 13
#define RESOURCE_WINDOW 1
struct IntuiActionMsg{unsigned unused;};
static struct {struct {unsigned ln_Type;}tc_Node;} task={{NT_PROCESS}};
#define FindTask(x) (&task)
static unsigned dispatches;
static BOOL ResourceExisting(struct Window*w,unsigned type,struct IntuitionBase*b){assert(type==RESOURCE_WINDOW&&ibaselocks);for(struct Window*p=b->FirstScreen->FirstWindow;p;p=p->NextWindow)if(p==w)return TRUE;return FALSE;}
static void DoSyncAction(void*code,struct IntuiActionMsg*message,struct IntuitionBase*b){assert(!ibaselocks&&!refreshlocks);dispatches++;((void(*)(void*,struct IntuitionBase*))code)(message,b);}
'''
main=r''' 
static struct IntScreen screen,otherScreen;static struct Window otherWindow;static struct IntuitionBase base;static struct Window windows[2];static struct Layer layers[2];
static struct TextFont font,basefont;static struct LogicalScreenInfo logical;static struct ScreenRenderContext context,stale;
static struct RastPort rp;static struct Gadget gadget;static struct IIHData input;static struct Interrupt handler;
struct Snapshot{struct IntScreen otherScreen;struct Window otherWindow;struct IntScreen screen;struct Window windows[2];struct Layer layers[2];struct RastPort rp,cache;struct GadgetInfo gi;struct TextFont font,basefont;unsigned allocs;};
static void take(struct Snapshot*s){s->otherScreen=otherScreen;s->otherWindow=otherWindow;s->screen=screen;memcpy(s->windows,windows,sizeof windows);memcpy(s->layers,layers,sizeof layers);s->rp=rp;s->cache=input.GadgetInfoRastPort;s->gi=input.GadgetInfo;s->font=font;s->basefont=basefont;s->allocs=live;}
static void unchanged(struct Snapshot*s){assert(!memcmp(&s->otherScreen,&otherScreen,sizeof otherScreen)&&!memcmp(&s->otherWindow,&otherWindow,sizeof otherWindow));assert(!memcmp(&s->screen,&screen,sizeof screen)&&!memcmp(s->windows,windows,sizeof windows)&&!memcmp(s->layers,layers,sizeof layers)&&!memcmp(&s->rp,&rp,sizeof rp)&&!memcmp(&s->cache,&input.GadgetInfoRastPort,sizeof s->cache)&&!memcmp(&s->gi,&input.GadgetInfo,sizeof s->gi)&&!memcmp(&s->font,&font,sizeof font)&&!memcmp(&s->basefont,&basefont,sizeof basefont)&&live==s->allocs&&!events&&!draws&&!pixellease&&!ibaselocks&&!refreshlocks&&pixels==100);}
static void setup(ULONG percent){memset(&screen,0,sizeof screen);memset(windows,0,sizeof windows);memset(layers,0,sizeof layers);memset(&base,0,sizeof base);memset(&input,0,sizeof input);memset(&rp,0,sizeof rp);
 font=(struct TextFont){(13*percent+50)/100,(8*percent+50)/100,(10*percent+50)/100,7};basefont=(struct TextFont){13,8,10,5};
 context=(struct ScreenRenderContext){NULL,&screen.Screen,&basefont,&font,percent};stale=context;screen.RenderContext=&context;screen.Scale=percent;screen.ScaleBaseFont=&basefont;screen.DInfo.dri_Font=&font;logical.base=&basefont;screen.LogicalInfo=&logical;screen.Screen.Width=2560;screen.Screen.Height=1600;SetFont(&screen.Screen.RastPort,&font);
 for(unsigned i=0;i<2;i++){struct Window*w=&windows[i];w->WScreen=&screen.Screen;w->NextWindow=i?NULL:&windows[1];w->LeftEdge=10+i*800;w->TopEdge=20;w->Width=i?701:(316*percent+50)/100;w->Height=i?503:(223*percent+50)/100;w->RelLeftEdge=w->LeftEdge;w->RelTopEdge=w->TopEdge;w->MinWidth=i?90:w->Width;w->MinHeight=i?90:w->Height;w->MaxWidth=2560;w->MaxHeight=1600;w->BorderLeft=w->BorderRight=w->BorderBottom=2;w->BorderTop=font.tf_YSize;w->GZZWidth=w->Width-4;w->GZZHeight=w->Height-font.tf_YSize-2;w->ScaleAware=TRUE;w->ScaleGeometryEnabled=TRUE;w->DecorDrawInfo.dri_Screen=&screen.Screen;w->DecorDrawInfo.dri_Font=&font;w->wl=&layers[i];w->framevalue=100;layers[i]=(struct Layer){.LayerInfo=&screen.Screen.LayerInfo,.bounds={w->LeftEdge,w->TopEdge,w->LeftEdge+w->Width-1,w->TopEdge+w->Height-1},.Width=w->Width,.Height=w->Height};
  LONG values[AG_COUNT]={10+(LONG)i*800,20,i?701:316,i?503:223,i?90:316,i?90:223,-1,-1};for(unsigned j=0;j<AG_COUNT;j++)w->ScaleGeometry.axis[j]=(struct AwareGeometryValue){values[j],100};}
 otherScreen=(struct IntScreen){.Scale=175};otherWindow=(struct Window){.Width=501,.Height=403,.WScreen=&otherScreen.Screen};otherScreen.Screen.FirstWindow=&otherWindow;screen.Screen.NextScreen=&otherScreen.Screen;screen.Screen.FirstWindow=windows;base.FirstScreen=&screen.Screen;handler.is_Data=&input;base.InputHandler=&handler;rp.Layer=&layers[0];rp.BitMap=(void*)1;SetFont(&rp,&font);windows[0].RPort=windows[0].BorderRPort=&rp;input.ActiveGadget=&gadget;input.GadgetInfo=(struct GadgetInfo){&screen.Screen,&windows[0],NULL,&input.GadgetInfoRastPort,&layers[0],&windows[0].DecorDrawInfo,{0,0,windows[0].Width,windows[0].Height}};input.GadgetInfoRastPort=rp;
 expectlayout=TRUE;auxrefresh=0;events=draws=refreshlocks=ibaselocks=ordinal=denied=hardtrap=cutting=aborted=cut_count=0;pointervalue=pixels=100;cutbase=&base;assert(!live&&!pixellease);}
static void cleanup(void){while(screen.RenderGenerations){struct ScreenRenderContext*c=screen.RenderGenerations;screen.RenderGenerations=c->next;CloseFont(c->base);CloseFont(c->physical);release(c);}while(screen.LogicalInfo!=&logical){struct LogicalScreenInfo*p=screen.LogicalInfo;screen.LogicalInfo=p->previous;DisposeLogicalScreenInfo(p,&base);}FreeWindowScaleShapes(&windows[0],&base);FreeWindowScaleShapes(&windows[1],&base);while(retainedframes){struct PreparedWindowFrame*p=retainedframes;retainedframes=p->retainedNext;release(p->generation);release(p);}assert(!live&&!pixellease&&!ibaselocks&&!refreshlocks);}
static BOOL apply(ULONG percent,LONG w,LONG h){struct WindowLayoutFit fit={windows,&context,w,h};assert(percent==screen.Scale);return FitWindowLayout(windows,&context,w,h,&base);}
int main(void){unsigned faults=0,cuts=0;for(ULONG percent=100;percent<=400;percent+=25){
 unsigned total=0,stores=0;for(unsigned f=0;;f++){setup(percent);struct Snapshot old;take(&old);struct AwareGeometry canonical=windows[0].ScaleGeometry;struct Window other=windows[1];
 /* Real FRGadInit fixture measured 316x223 at100 and474x345 at150.
    Additional fractional phases deliberately exceed projected minima too. */
 LONG minw=percent==100?316:percent==150?474:windows[0].Width+7;LONG minh=percent==100?223:percent==150?345:windows[0].Height+5;failat=f;
 BOOL ok=apply(percent,minw,minh);unsigned used=ordinal;failat=0;
 if(!f){assert(ok);total=used;stores=cut_count;assert(windows[0].Width==minw&&windows[0].Height==minh&&windows[0].MinWidth==minw&&windows[0].MinHeight==minh);assert(windows[0].LayoutMinContext==&context&&windows[0].LayoutMinWidth==minw&&windows[0].LayoutMinHeight==minh);assert(!memcmp(&canonical,&windows[0].ScaleGeometry,sizeof canonical)&&!memcmp(&other,&windows[1],sizeof other));assert(screen.RenderContext==&context&&screen.DInfo.dri_Font==&font&&screen.ScaleBaseFont==&basefont&&font.refs==7&&basefont.refs==5&&pointervalue==100);assert(!memcmp(&old.otherScreen,&otherScreen,sizeof otherScreen)&&!memcmp(&old.otherWindow,&otherWindow,sizeof otherWindow));assert(windows[0].framevalue==200&&input.GadgetInfo.gi_DrInfo->dri_Font==&font);assert(input.GadgetInfo.gi_Domain.Width==minw&&input.GadgetInfo.gi_Domain.Height==minh&&input.GadgetInfoRastPort.Font==&font);cleanup();}
 else{assert(!ok&&denied==1);unchanged(&old);faults++;cleanup();}if(f==total)break;}
 for(unsigned c=1;c<=stores;c++){setup(percent);struct Snapshot old;take(&old);cutting=1;cut_at=c;LONG w=percent==150?474:windows[0].Width+7,h=percent==150?345:windows[0].Height+5;
 assert(!apply(percent,w,h)&&aborted&&cut_count==c);hardtrap=0;unchanged(&old);cleanup();cuts++;}}
 /* Stale context, impossible max/viewport, unsupported and crossscreen reject. */
 setup(150);struct Snapshot old;take(&old);struct WindowLayoutFit fit={windows,&stale,474,345};assert(!FitWindowLayout(windows,&stale,474,345,&base));assert(!FitWindowLayout((struct Window*)1,&context,474,345,&base));assert(!ApplyPreparedScreenScale(&screen,&font,&basefont,150,&fit,NULL,&base));unchanged(&old);
 fit.context=&context;fit.minwidth=3000;assert(!ApplyPreparedScreenScale(&screen,&font,&basefont,150,&fit,NULL,&base));unchanged(&old);
 fit.minwidth=800;windows[0].ScaleGeometry.axis[AG_MAXWIDTH]=(struct AwareGeometryValue){500,100};take(&old);assert(!ApplyPreparedScreenScale(&screen,&font,&basefont,150,&fit,NULL,&base));unchanged(&old);windows[0].ScaleGeometry.axis[AG_MAXWIDTH]=(struct AwareGeometryValue){-1,100};
 fit.minwidth=474;windows[0].ScaleAware=FALSE;take(&old);assert(!ApplyPreparedScreenScale(&screen,&font,&basefont,150,&fit,NULL,&base));unchanged(&old);windows[0].ScaleAware=TRUE;
 struct Screen another={0};struct Window foreign=windows[0];foreign.WScreen=&another;fit.window=&foreign;take(&old);assert(!ApplyPreparedScreenScale(&screen,&font,&basefont,150,&fit,NULL,&base));unchanged(&old);cleanup();
 /* The real public boundary refuses invalid/nonProcess calls before dispatch. */
 setup(150);unsigned beforedispatch=dispatches;task.tc_Node.ln_Type=0;assert(!FitWindowLayout(windows,&context,474,345,&base)&&dispatches==beforedispatch);task.tc_Node.ln_Type=NT_PROCESS;
 assert(!FitWindowLayout(windows,&context,0,345,&base)&&dispatches==beforedispatch);cleanup();
 /* Same-percentage base-font transition is a REAL normal coordinator request.
    It invalidates measured minima without replacing canonical user intent. */
 setup(150);assert(apply(150,474,345));struct AwareGeometry samecanonical=windows[0].ScaleGeometry;
 struct TextFont newfont={26,16,20,1},newbase={17,8,13,1};events=draws=0;expectlayout=FALSE;
 assert(ApplyPreparedScreenScale(&screen,&newfont,&newbase,150,NULL,NULL,&base));
 assert(screen.Scale==150&&screen.DInfo.dri_Font==&newfont&&screen.ScaleBaseFont==&newbase&&screen.RenderContext!=&context);
 assert(!windows[0].LayoutMinContext&&!windows[0].LayoutMinWidth&&!windows[0].LayoutMinHeight&&!memcmp(&samecanonical,&windows[0].ScaleGeometry,sizeof samecanonical));
 assert(input.GadgetInfo.gi_DrInfo->dri_Font==&newfont&&input.GadgetInfoRastPort.Font==&newfont);cleanup();assert(newfont.refs==1&&newbase.refs==1&&font.refs==6&&basefont.refs==4);
 /* A clamped target parent translates descendants in their own native units.
    Their sizes, relative origins, canonical intent and measured minima stay. */
 for(unsigned legacy=0;legacy<2;legacy++){
 setup(200);windows[0].LeftEdge=2400;layers[0].bounds.MinX=2400;layers[0].bounds.MaxX=3031;windows[0].ScaleGeometry.axis[AG_LEFT]=(struct AwareGeometryValue){1200,100};
 struct Window*child=&windows[1];child->parent=windows;child->ScaleAware=!legacy;child->ScaleGeometryEnabled=!legacy;child->RelLeftEdge=10;child->RelTopEdge=10;child->LeftEdge=legacy?1210:2410;child->TopEdge=legacy?20:30;layers[1].bounds.MinX=2420;layers[1].bounds.MinY=40;layers[1].Width=legacy?1402:701;layers[1].Height=legacy?1006:503;screen.Screen.RastPort.BitMap=(void*)1;
 struct AwareGeometry childintent=child->ScaleGeometry;struct WindowFitFrameMetrics fs[2]={{windows,2,26,2,2},{child,2,26,2,2}};struct WindowLayoutFit request={windows,&context,700,500};struct WindowFitTransaction*t=NULL;
 assert(WindowFitPreflight(&screen.Screen,200,fs,2,&request,&t,NULL,&base));struct WindowFitGeometry pg,cg;struct Window*p;
 assert(WindowFitDesired(t,0,&p,&pg)&&p==windows&&pg.left==1860&&pg.width==700);
 assert(WindowFitDesired(t,1,&p,&cg)&&p==child&&cg.physical_left==1880&&cg.width==701&&cg.left==(legacy?940:1870)&&cg.relleft==10);
 WindowFitInstall(t,FALSE);assert(!memcmp(&childintent,&child->ScaleGeometry,sizeof childintent));WindowFitFinish(t,FALSE,&base);cleanup();}
 /* Exact canonical reset and explicit user resize anchors: actual planner. */
 setup(150);struct AwareGeometry canonical=windows[0].ScaleGeometry;assert(apply(150,474,345));assert(!memcmp(&canonical,&windows[0].ScaleGeometry,sizeof canonical));
 struct WindowFitFrameMetrics frames[2]={{windows,2,13,2,2},{windows+1,2,13,2,2}};struct WindowFitTransaction*t=NULL;
 assert(WindowFitPreflight(&screen.Screen,100,frames,2,NULL,&t,NULL,&base));WindowFitInstall(t,FALSE);WindowFitFinish(t,FALSE,&base);assert(windows[0].Width==316&&windows[0].Height==223&&!windows[0].LayoutMinContext&&!windows[0].LayoutMinWidth&&!windows[0].LayoutMinHeight);
 screen.Scale=150;windows[0].Width=503;windows[0].Height=401;WindowFitRecordChange(windows,WINDOWFIT_WIDTH|WINDOWFIT_HEIGHT);canonical=windows[0].ScaleGeometry;
 assert(WindowFitPreflight(&screen.Screen,100,frames,2,NULL,&t,NULL,&base));WindowFitInstall(t,FALSE);WindowFitFinish(t,FALSE,&base);assert(windows[0].Width==335&&windows[0].Height==267&&!memcmp(&canonical,&windows[0].ScaleGeometry,sizeof canonical));cleanup();
 printf("PASS actual coordinator+WindowFit+cachedGI:13scales fractionalFR316x223→474x345, reset/user intent, exactcontext/no fontownership/noCHANGESCALE; %u preparation faults %u install cuts rollback\n",faults,cuts);return 0;}
'''
source=prefix+headers+boundaries+gi+fitprefix+fit+body+publicprefix+publicbody+main
mutants={
 'unguarded-optional-frame':('if (prepared->windows[i].frame)\n            InstallWindowFrame(prepared->windows[i].frame, rollback);','InstallWindowFrame(prepared->windows[i].frame, rollback);'),
 'canonical-drift':('window->MinWidth = geometry->minwidth;', 'IW(window)->ScaleGeometry.axis[AG_WIDTH]=(struct AwareGeometryValue){geometry->width,100}; window->MinWidth = geometry->minwidth;'),
 'layout-font-close':('if (committed && !layout) { CloseFont(oldfont); CloseFont(oldbase); }','if (committed) { CloseFont(oldfont); CloseFont(oldbase); }'),
 'auxiliary-exposure-not-refreshed':('DrawAuxiliaryLayerRefresh(screen, IntuitionBase);','if (!layout) DrawAuxiliaryLayerRefresh(screen, IntuitionBase);'),
 'layout-scale-event':('if (!layout) specs[i].flags |= PNF_SCALE;','specs[i].flags |= PNF_SCALE;'),
 'stale-context-admitted':('!BoundScreenRenderContext(layout->context, screen)','FALSE'),
 'minimum-width-ignored':('geometry.minwidth = transaction->layout->minwidth;','geometry.minwidth = geometry.minwidth;'),
 'minimum-height-ignored':('geometry.minheight = transaction->layout->minheight;','geometry.minheight = geometry.minheight;'),
 'measured-context-not-cleared':('if (layout)\n        {\n            plan->newMinContext', 'plan->newMinContext=plan->oldMinContext; plan->newMinWidth=plan->oldMinWidth; plan->newMinHeight=plan->oldMinHeight; if (layout)\n        {\n            plan->newMinContext'),
 'minimum-rollback-lost':('rollback ? plan->oldMinHeight : plan->newMinHeight','plan->newMinHeight'),
 'cache-geometry-stale':('InstallActiveGadgetScale(&prepared->windows[i].active, rollback);','(void)0;'),
 'unrelated-reprojected':('transaction->layout && transaction->layout->window != window','FALSE'),
 'legacy-descendant-physical-domain':('plan->old.left + (plan->legacy ?', 'plan->old.left + (FALSE ?'),
 'public-nonprocess-admitted':('FindTask(NULL)->tc_Node.ln_Type != NT_PROCESS','FALSE'),
 'public-synchronous-result-lost':('return action.fitted;','return TRUE;'),
 'request-declared-max-ignored':('geometry.minwidth, geometry.maxwidth, &geometry.left','geometry.minwidth, 32767, &geometry.left'),
}
with tempfile.TemporaryDirectory(prefix='layoutfit-') as directory:
 out=Path(directory);unit=out/'test.c';binary=out/'test'
 for name,text in [('production',source)]+[(name,source.replace(old,new)) for name,(old,new) in mutants.items()]:
  if name!='production':assert text!=source,name
  unit.write_text(text)
  p=subprocess.run(['clang','-std=c11','-g','-O1','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],capture_output=True,text=True)
  if p.returncode:print(name,p.stderr);raise SystemExit(p.returncode)
  p=subprocess.run([str(binary)],capture_output=True,text=True)
  assert (p.returncode==0)==(name=='production'),(name,p.stdout,p.stderr)
  print(p.stdout if name=='production' else 'REJECTED '+name,end='' if name=='production' else '\n')
