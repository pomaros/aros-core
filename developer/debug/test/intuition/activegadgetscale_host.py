#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual cached GI prepare/install, activation/domain and STRGCLASS editing.
OS/font/raster dispatch adapters remain explicit; this is no native pixel run.
"""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[4]
backend=(root/'rom/intuition/activegadgetscale.c').read_text()
assert not re.search(r'\b(?:Alloc\w*|Free\w*|OpenDiskFont|OpenFont|CloseFont|DoMethod\w*|CallHook\w*|GetScreenDrawInfo)\s*\(',backend)
fixture=root/'developer/debug/test/intuition/requesterscale_host.py'
ns={'__file__':str(fixture),'__name__':'active_dependencies'};exec(fixture.read_text().split('variants=[')[0],ns)
def extract(text,name):
 m=re.search(r'(?m)^.*\b'+re.escape(name)+r'\s*\([^;]*?\)\s*\{',text);assert m,name
 a=m.start();b=text.index('{',a);i=b+1;n=1
 while n:n+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[a:i]
gadgets=(root/'rom/intuition/gadgets.c').read_text(encoding='latin1');strings=(root/'rom/intuition/strgadgets.c').read_text();strclass=(root/'rom/intuition/strgclass.c').read_text();activation=(root/'rom/intuition/inputhandler_support.c').read_text()
prefix=ns['prefix'];prefix=prefix.replace('tf_YSize,tf_Baseline;','tf_YSize,tf_Baseline,tf_XSize;').replace('struct TextExtent{int unused;};','struct TextExtent{LONG te_Width;struct Rectangle te_Extent;};')
prefix=prefix.replace('LONG x,y,w,h;','LONG x,y,w,h;UWORD TxWidth,TxHeight,TxBaseline;').replace('struct GfxBase{int unused;};','struct GfxBase{struct TextFont*DefaultFont;};')
prefix=prefix.replace('void*GadgetRender,*SelectRender;','void*GadgetRender,*SelectRender,*SpecialInfo;').replace('struct Layer*BarLayer;};','struct Layer*BarLayer;UBYTE DetailPen,BlockPen;};').replace('struct Requester*FirstRequest;};','struct Requester*FirstRequest;UBYTE DetailPen,BlockPen;LONG MouseX;struct DrawInfo*frame;};')
prefix=prefix.replace('LONG LeftEdge,TopEdge,Width,Height;};','LONG LeftEdge,TopEdge,Width,Height;struct Window*RWindow;};',1)
prefix=prefix.replace('void*ReqFont,*KeymapBase,*UtilityBase;','void*ReqFont,*KeymapBase,*UtilityBase;struct Hook*GlobalEditHook;struct TextFont*TopazFont;').replace('char ic_ReqTrue,ic_ReqFalse;','char ic_ReqTrue,ic_ReqFalse;ULONG ic_Flags;')
# Use the actual allocation-free graphics SetFont body, not a Font-only mock.
font_source=subprocess.check_output(['git','show','HEAD:rom/graphics/setfont.c'],cwd=root,text=True);font_body=font_source.split('    AROS_LIBFUNC_INIT',1)[1].split('    AROS_LIBFUNC_EXIT',1)[0]
prefix=prefix.replace(extract(prefix,'SetFont'),'static void SetFont(struct RastPort*rp,struct TextFont*textFont){struct GfxBase*GfxBase=&gfx;'+font_body+'}')
prefix=prefix.replace('static struct GfxBase gfx;','static struct GfxBase gfx;') # declaration precedes SetFont
prefix=prefix.replace('assert(direction==1&&height>0);','assert((direction==1||direction==-1)&&height>0);')
prefix=prefix.replace('void*ReqFont,*KeymapBase,*UtilityBase;', 'void*ReqFont,*KeymapBase,*UtilityBase,*LayersBase;')
inputprefix=ns['inputprefix'].replace('struct Layer*gi_Layer;};','struct Layer*gi_Layer;struct{UBYTE DetailPen,BlockPen;}gi_Pens;};')
# Rendering dispatch is wired below after the real class entry points.
inputprefix=inputprefix.replace(extract(inputprefix,'DoMethodA'),'static IPTR DoMethodA(Object,Msg);')
headers=(root/'compiler/include/intuition/intuition.h').read_text(encoding='latin1');sg=(root/'compiler/include/intuition/sghooks.h').read_text(encoding='latin1');public=sg[sg.index('struct StringExtend'):sg.rindex('#endif')]
a=headers.index('struct StringInfo\n');b=headers.index('\n};',a)+3;public=headers[a:b]+'\n'+public
constants='\n'.join(line for line in headers.splitlines() if re.match(r'#define\s+(GFLG_STRINGEXTEND|GFLG_DISABLED|GACT_STRINGLEFT|GACT_STRINGCENTER|GACT_STRINGRIGHT|GACT_LONGINT|GTYP_CUSTOMGADGET|GTYP_GTYPEMASK|CURSORLEFT|CURSORRIGHT)\b',line))
constants+='\n'+strings[strings.index('#define CURSORPEN'):strings.index('VOID UpdateStringInfo')]
more=r'''
#define STATIC static
#define VOID void
#define MIN(a,b) ((a)<(b)?(a):(b))
#define FILLPEN 4
#define CONST const
#define EnterFunc(x)
#define D(x)
#define ReturnInt(n,t,v) return (v)
#define ReturnVoid(n) return
#define DEBUG_SETUPGINFO(x)
#define SetMem(p,v,n) memset(p,v,n)
#define SELECTDOWN 0
#define MENUDOWN 2
#define IECODE_UP_PREFIX 128
#define IEQUALIFIER_LSHIFT 2
#define IEQUALIFIER_RSHIFT 4
#define IEQUALIFIER_CONTROL 8
#define IEQUALIFIER_RCOMMAND 16
#define ICF_STRGAD_FILTER 4
#define GMR_PREVACTIVE 16
#define GMR_NEXTACTIVE 32
#define FPF_PROPORTIONAL 1
#define GFLG_RELRIGHT 16
#define GFLG_RELBOTTOM 32
#define GFLG_RELWIDTH 64
#define GFLG_RELHEIGHT 256
#define INVERSVID 4
struct BBox{LONG Left,Top,Width,Height;};
struct StrGData{struct StringInfo StrInfo;struct StringExtend StrExtend;UBYTE Flags;};
struct Hook{void*h_Data;};
#define INST_DATA(cl,o) ((void)(cl),(struct StrGData*)((char*)(o)+sizeof(struct Gadget)))
static struct DrawInfo*WindowDrawInfo(struct Screen*s,struct Window*w){assert(s==w->WScreen&&w->frame);return w->frame;}
static struct DrawInfo*GetScreenDrawInfo(struct Screen*s){assert(s->current);return s->current->src_LogicalDrawInfo;}
static void FreeScreenDrawInfo(struct Screen*s,struct DrawInfo*d){(void)s;(void)d;assert(0&&"borrowed frame DrawInfo must not be freed");}
static void GetGadgetIBox(struct Gadget*g,struct GadgetInfo*gi,struct IBox*b){(void)g;*b=gi->gi_Domain;}
static void SetDrMd(struct RastPort*rp,ULONG mode){(void)rp;(void)mode;}
static void RenderDisabledPattern(struct RastPort*r,struct DrawInfo*d,LONG l,LONG t,LONG x,LONG y,struct IntuitionBase*b){(void)r;(void)d;(void)l;(void)t;(void)x;(void)y;(void)b;}
static void TextExtent(struct RastPort*rp,CONST_STRPTR t,ULONG n,struct TextExtent*e){e->te_Width=TextLength(rp,t,n);e->te_Extent=(struct Rectangle){0,-(WORD)rp->Font->tf_Baseline,e->te_Width-1,rp->Font->tf_YSize-rp->Font->tf_Baseline-1};}
static void RefreshStrGadget(struct Gadget*g,struct Window*w,struct Requester*r,struct IntuitionBase*b){(void)g;(void)w;(void)r;(void)b;assert(0);}
static IPTR CallHookPkt(struct Hook*,void*,void*);
static void Custom_DoMethodA(struct IntuitionBase*b,struct Gadget*g,Msg m){(void)b;DoMethodA(g,m);}
'''
geometry='\n'.join(extract(gadgets,n) for n in ['GetWinGadgetIBox','GetDomGadgetIBox','CalcBBox','SetupGInfo'])
geometry=geometry.replace('ADDREL(', 'ADDREL(')
more+='\n#define ADDREL(gad,flag,w,field) (((gad)->Flags&(flag))?(w)->field:0)\n'
prepare=extract(activation,'PrepareGadgetInfo')+'\n'+extract(activation,'SetGadgetInfoGadget')
methods='\n'.join(extract(strings,n) for n in ['UpdateStringInfo','MaxDispPos','UpdateDisp','GetTextLeft','GetTextRight','GetPensAndFont','MoveCharsLeft','DoSGHKey','DoSGHClick','HandleStrInput','UpdateStrGadget'])
methods=methods.replace('CONST char cursorkey[]','CONST char cursorkey[]')
more+='\nstatic const char cursorkey[]="_";\n#define CURSORKEYLEN 1\n'
classes='\n'.join(extract(strclass,n) for n in ['StrGClass__GM_RENDER','StrGClass__GM_GOACTIVE','StrGClass__GM_HANDLEINPUT'])
more+='\nstatic void UpdateStrGadget(struct Gadget*,struct Window*,struct Requester*,struct IntuitionBase*);\n'
dispatch=r'''
static IPTR DoMethodA(Object o,Msg msg){struct gpRender*m=msg;assert(m->MethodID==GM_RENDER);rendered_gadget=o;rendered_font=m->gpr_RPort->Font;Class cl={(IPTR)&base};return StrGClass__GM_RENDER(&cl,o,m);}
static IPTR CallHookPkt(struct Hook*h,void*object,void*message){assert(h==base.GlobalEditHook);struct SGWork*work=object;ULONG command=*(ULONG*)message;if(command==SGH_KEY)return DoSGHKey(work,h->h_Data);assert(command==SGH_CLICK);return DoSGHClick(work,h->h_Data);}
'''
general=ns['general_production'];start=general.index('void InstallActiveGadgetScale(');general=general[:start]+re.sub(r'(?m)(    (?:prepared->info->|\*prepared->raster).*;)$',r'\1 if (!rollback) store_cut();',general[start:])
# Actual postcommit cache repair prefix, before unrelated resize drawing.
actions=(root/'rom/intuition/inputhandler_actions.c').read_text();post=extract(actions,'WindowSizeHasChanged');post=post[:post.index('    /* Relayout GFLG_REL??? gadgets */')]+"}\n"
post=post.replace('void WindowSizeHasChanged(', 'static void postcommit_domain(')
extra=r'''
struct LayersBase{int unused;};
#define BLAYER(w) (((w)->Flags&WFLG_GIMMEZEROZERO)?(w)->BorderRPort->Layer:NULL)
#define WLAYER(w) ((w)->RPort->Layer)
'''
baseunit=prefix+inputprefix+ns['handler_prefix']+ns['general_prefix']+public+'\n'+constants+'\n'+more+ns['general_header']+ns['domain']+geometry+prepare+general+methods+classes+dispatch+extra+post
# Tests are appended below. No GUI target is started by this fixture.
tests=r'''
int main(void){UWORD pens[]={0,1,2,3,4};struct Screen screen={.Width=2560,.Height=1600};struct TextFont logical={13,10,7},fonts[15];struct DrawInfo logicalDI={&screen,&logical,pens},dri[15];struct ScreenRenderContext ctx={.src_LogicalDrawInfo=&logicalDI};screen.current=&ctx;gfx.DefaultFont=&logical;gfx.DefaultFont=&logical;
struct RastPort rp={.Layer=(void*)1,.BitMap=(void*)2,.w=2000,.h=1500},border=rp,reqrp=rp;
struct Window window={.WScreen=&screen,.RPort=&rp,.BorderRPort=&border,.Width=700,.Height=400,.BorderLeft=2,.BorderTop=16,.BorderRight=2,.BorderBottom=2,.ScaleAware=TRUE,.frame=&logicalDI};
struct IIHData input={0};struct Interrupt ih={&input};base.InputHandler=&ih;struct Hook hook={&base};base.GlobalEditHook=&hook;
struct {struct Gadget gadget;struct StrGData data;} string={0};char buffer[128]="abc",undo[128]="",work[128]="";
string.data.StrInfo=(struct StringInfo){.Buffer=(UBYTE*)buffer,.UndoBuffer=(UBYTE*)undo,.BufferPos=2,.MaxChars=128,.Extension=&string.data.StrExtend};string.data.StrExtend.WorkBuffer=work;
struct Gadget*g=&string.gadget;g->SpecialInfo=&string.data.StrInfo;g->GadgetType=GTYP_CUSTOMGADGET;g->Flags=GFLG_STRINGEXTEND;g->Width=500;g->Height=100;input.ActiveGadget=g;
PrepareGadgetInfo(&input.GadgetInfo,&screen,&window,NULL);SetGadgetInfoGadget(&input.GadgetInfo,g,&base);
Class cl={(IPTR)&base};struct gpInput active={.gpi_GInfo=&input.GadgetInfo};assert(StrGClass__GM_GOACTIVE(&cl,g,&active)==GMR_MEACTIVE);assert(!strcmp(undo,"abc")&&(g->Flags&GFLG_SELECTED));
for(unsigned phase=0;phase<15;phase++){
 ULONG scale=phase==13?100:phase==14?200:100+phase*25;ULONG baseheight=phase==14?17:13;fonts[phase]=(struct TextFont){(baseheight*scale+50)/100,(10*scale+50)/100,(7*scale+50)/100};dri[phase]=(struct DrawInfo){&screen,&fonts[phase],pens};
 struct WindowFitGeometry desired={.width=1000,.height=700};struct PreparedActiveGadgetScale p;
 struct StringInfo saved=string.data.StrInfo;struct StringExtend savedExtend=string.data.StrExtend;char savedBuffer[128],savedUndo[128];memcpy(savedBuffer,buffer,128);memcpy(savedUndo,undo,128);
 assert(PrepareActiveGadgetScale(&window,&dri[phase],&desired,4,40,4,4,&p,&base));
 struct GadgetInfo old=input.GadgetInfo;struct RastPort oldRP=input.GadgetInfoRastPort;
 for(unsigned cut=1;cut<=4;cut++){store_count=0;store_fail=cut;if(!setjmp(store_return)){InstallActiveGadgetScale(&p,FALSE);assert(0);}store_fail=0;InstallActiveGadgetScale(&p,TRUE);assert(!memcmp(&old,&input.GadgetInfo,sizeof old)&&!memcmp(&oldRP,&input.GadgetInfoRastPort,sizeof oldRP));}
 window.Width=desired.width;window.Height=desired.height;window.BorderLeft=4;window.BorderTop=40;window.BorderRight=4;window.BorderBottom=4;window.frame=&dri[phase];InstallActiveGadgetScale(&p,FALSE);
 assert(input.GadgetInfo.gi_DrInfo==&dri[phase]&&input.GadgetInfo.gi_Domain.Width==1000&&input.GadgetInfo.gi_Domain.Height==700&&input.GadgetInfoRastPort.Font==&fonts[phase]&&input.GadgetInfoRastPort.TxHeight==fonts[phase].tf_YSize&&input.GadgetInfoRastPort.TxBaseline==fonts[phase].tf_Baseline);
 assert(!memcmp(&saved,&string.data.StrInfo,sizeof saved)&&!memcmp(&savedExtend,&string.data.StrExtend,sizeof savedExtend)&&!memcmp(savedBuffer,buffer,128)&&!memcmp(savedUndo,undo,128)&&input.ActiveGadget==g);
 struct InputEvent key={.ie_Class=IECLASS_RAWKEY,.ie_Code='x'};ULONG termination=99;struct gpInput message={&input.GadgetInfo,&key,&termination,{0,0}};
 WORD pos=string.data.StrInfo.BufferPos;assert(StrGClass__GM_HANDLEINPUT(&cl,g,&message)==GMR_MEACTIVE);assert(string.data.StrInfo.BufferPos==pos+1&&buffer[pos]=='x'&&!strcmp(undo,"abc")&&rendered_font==&fonts[phase]);
 UWORD customPens[3];GetPensAndFont(g,customPens,&window,&rp,&base);assert(rp.Font==&fonts[phase]);
 window.MouseX=2*(fonts[phase].tf_YSize*6/13);key=(struct InputEvent){.ie_Class=IECLASS_RAWMOUSE,.ie_Code=SELECTDOWN};message.gpi_Mouse.X=window.MouseX;
 assert(StrGClass__GM_HANDLEINPUT(&cl,g,&message)==GMR_MEACTIVE);assert(string.data.StrInfo.BufferPos==2&&!strcmp(undo,"abc"));
}
/* Legacy fixed font and explicit client override keep their contracts. */
struct TextFont fixed={8,6,5},custom={21,16,11};base.TopazFont=&fixed;UWORD pen[3];
g->Flags=GFLG_SELECTED;window.ScaleAware=FALSE;GetPensAndFont(g,pen,&window,&rp,&base);assert(rp.Font==&fixed);
window.ScaleAware=TRUE;GetPensAndFont(g,pen,&window,&rp,&base);assert(rp.Font==window.frame->dri_Font);
g->Flags|=GFLG_STRINGEXTEND;string.data.StrExtend.Font=&custom;GetPensAndFont(g,pen,&window,&rp,&base);assert(rp.Font==&custom);string.data.StrExtend.Font=NULL;
/* Actual activation routing for GZZ client, border/master and requester. */
struct WindowFitGeometry shape={.width=900,.height=600};struct PreparedActiveGadgetScale plan;
window.Flags=WFLG_GIMMEZEROZERO;window.Width=1000;window.Height=700;
PrepareGadgetInfo(&input.GadgetInfo,&screen,&window,NULL);SetGadgetInfoGadget(&input.GadgetInfo,g,&base);
assert(PrepareActiveGadgetScale(&window,window.frame,&shape,6,42,8,10,&plan,&base));assert(plan.newDomain.Left==6&&plan.newDomain.Top==42&&plan.newDomain.Width==886&&plan.newDomain.Height==548);
struct Gadget sys={.GadgetType=GTYP_GZZGADGET};border.Layer=(void*)3;border.BitMap=(void*)4;input.ActiveSysGadget=&sys;
assert(PrepareActiveGadgetScale(&window,window.frame,&shape,6,42,8,10,&plan,&base));assert(plan.newDomain.Left==0&&plan.newDomain.Width==900&&plan.newLayer==border.Layer&&plan.newRaster.BitMap==border.BitMap);InstallActiveGadgetScale(&plan,FALSE);assert(input.GadgetInfo.gi_Layer==border.Layer);InstallActiveGadgetScale(&plan,TRUE);input.ActiveSysGadget=NULL;
g->Activation=GACT_RIGHTBORDER;assert(PrepareActiveGadgetScale(&window,window.frame,&shape,6,42,8,10,&plan,&base)&&plan.newLayer==border.Layer);g->Activation=0;
struct Layer requestLayer={&reqrp};struct Requester request={.ReqLayer=&requestLayer,.LeftEdge=10,.TopEdge=12,.Width=220,.Height=110,.RWindow=&window};window.FirstRequest=&request;window.Flags=0;g->GadgetType=GTYP_CUSTOMGADGET|GTYP_REQGADGET;
PrepareGadgetInfo(&input.GadgetInfo,&screen,&window,&request);SetGadgetInfoGadget(&input.GadgetInfo,g,&base);
assert(PrepareActiveGadgetScale(&window,window.frame,&shape,6,42,8,10,&plan,&base));assert(plan.newDomain.Left==16&&plan.newDomain.Top==54&&plan.newDomain.Width==220&&plan.newDomain.Height==110&&plan.newLayer==reqrp.Layer);
window.Width=900;window.Height=600;window.BorderLeft=6;window.BorderTop=42;window.BorderRight=8;window.BorderBottom=10;InstallActiveGadgetScale(&plan,FALSE);
postcommit_domain(&window,0,0,FALSE,&base);assert(input.GadgetInfo.gi_Domain.Left==16&&input.GadgetInfo.gi_Domain.Top==54&&input.GadgetInfo.gi_Domain.Width==220);
/* No-op and inconsistent/unsupported capture paths do not touch live state. */
struct GadgetInfo before=input.GadgetInfo;struct RastPort beforeRP=input.GadgetInfoRastPort;
window.ScaleAware=FALSE;assert(PrepareActiveGadgetScale(&window,NULL,NULL,0,0,0,0,&plan,&base)&&!plan.info);window.ScaleAware=TRUE;
struct Window other=window;struct Screen anotherScreen=screen;other.WScreen=&anotherScreen;assert(PrepareActiveGadgetScale(&other,NULL,NULL,0,0,0,0,&plan,&base)&&!plan.info);
input.ActiveGadget=NULL;assert(PrepareActiveGadgetScale(&window,NULL,NULL,0,0,0,0,&plan,&base)&&!plan.info);input.ActiveGadget=g;
input.GadgetInfo.gi_RastPort=&rp;assert(!PrepareActiveGadgetScale(&window,window.frame,&shape,6,42,8,10,&plan,&base)&&!plan.info);input.GadgetInfo=before;
input.GadgetInfo.gi_Requester=(void*)1;assert(!PrepareActiveGadgetScale(&window,window.frame,&shape,6,42,8,10,&plan,&base)&&!plan.info);input.GadgetInfo=before;
assert(!PrepareActiveGadgetScale(&window,NULL,&shape,6,42,8,10,&plan,&base)&&!plan.info);
shape.width=32768;assert(!PrepareActiveGadgetScale(&window,window.frame,&shape,6,42,8,10,&plan,&base)&&!plan.info);
g->GadgetType=GTYP_SCRGADGET;assert(PrepareActiveGadgetScale(&window,NULL,NULL,0,0,0,0,&plan,&base)&&!plan.info);g->GadgetType=GTYP_CUSTOMGADGET|GTYP_REQGADGET;
assert(!memcmp(&before,&input.GadgetInfo,sizeof before)&&!memcmp(&beforeRP,&input.GadgetInfoRastPort,sizeof beforeRP));
assert(!live&&!locked);puts("PASS actual active STRGCLASS GOACTIVE/HANDLEINPUT+global edit/render:13scales/reset+same-scale basefont, text/undo/caret/capture stable, GZZ/border/requester routes, prepared font/domain/raster and4 cuts rollback");}
'''
source=baseunit+tests
mutants={
 'stale-drawinfo':('prepared->newDrawInfo = drawinfo;','prepared->newDrawInfo = prepared->oldDrawInfo;'),
 'stale-raster-font':('SetFont(&prepared->newRaster, drawinfo->dri_Font);','/* stale font */'),
 'stale-layer':('prepared->newLayer = source.gi_RastPort->Layer;','prepared->newLayer = input->GadgetInfo.gi_Layer;'),
 'whole-window-domain':('GetGadgetDomain(gadget, window->WScreen, &metrics, requester, &prepared->newDomain);','prepared->newDomain=(struct IBox){0,0,desired->width,desired->height};'),
 'domain-rollback':('rollback ? prepared->oldDomain : prepared->newDomain','prepared->newDomain'),
 'requester-null-postcommit':('iihdata->GadgetInfo.gi_Requester,','NULL,'),
 'logical-click-font':('WindowDrawInfo(window->WScreen, window)','GetScreenDrawInfo(window->WScreen)'),
 'logical-render-font':('WindowDrawInfo(win->WScreen, win)','GetScreenDrawInfo(win->WScreen)'),
 'explicit-client-font-lost':('SetFont(rp, strext->Font);','/* lost override */'),
 'legacy-fixed-font-lost':('if (!IW(win)->ScaleAware)','if (FALSE)'),
 'master-domain-instead-of-real-sysgadget':('input->ActiveSysGadget ? input->ActiveSysGadget : input->ActiveGadget','input->ActiveGadget'),
 'unsupported-raster-admitted':('input->GadgetInfo.gi_RastPort != &input->GadgetInfoRastPort','FALSE'),
}
with tempfile.TemporaryDirectory(prefix='activegadgetscale-') as directory:
 out=Path(directory)
 for name,text in [('production',source)]+[(name,source.replace(old,new)) for name,(old,new) in mutants.items()]:
  if name!='production':assert text!=source,name
  unit=out/(name+'.c');binary=out/name;unit.write_text(text)
  p=subprocess.run(['clang','-std=c11','-g','-O1','-Wno-incompatible-pointer-types','-Wno-pointer-sign','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],capture_output=True,text=True)
  if p.returncode:print(p.stderr);raise SystemExit(p.returncode)
  p=subprocess.run([str(binary)],capture_output=True,text=True)
  assert (p.returncode==0)==(name=='production'),(name,p.stdout,p.stderr)
  print(p.stdout if name=='production' else 'REJECTED '+name,end='' if name=='production' else '\n')
