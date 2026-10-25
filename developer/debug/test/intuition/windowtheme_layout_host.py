#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL) Version 1.1.
"""Actual theme prepared resources and actual builtin/theme SYSI gadget layout.
Client BOOPSI getter/setter paths trap; only admitted server gadgets are tested.
"""
import pathlib,re,subprocess,tempfile
helper=pathlib.Path(__file__).with_name('windowtheme_prepare_host.py');exec(compile(helper.read_text().split("main=r'''",1)[0],str(helper),'exec'))
builtin=(a.source/'rom/intuition/windecorclass.c').read_text();h=(a.source/'compiler/include/intuition/windecorclass.h').read_text();ext=(a.source/'compiler/include/intuition/extensions.h').read_text(encoding='latin1')
pre=pre.replace('typedef int BOOL;','typedef int BOOL;typedef int8_t BYTE;typedef void*Msg;\n#define STACKED\n').replace('REFUSAL_RECORD',refusal)
pre+='''
struct Window{LONG Width,Height,BorderTop;ULONG Flags;};struct Gadget{struct Gadget*NextGadget;WORD LeftEdge,TopEdge,Width,Height;ULONG Flags,GadgetType,GadgetID,Activation;};struct ExtGadget{struct Gadget pub;ULONG MoreFlags;};
#define WDF_LBG_SYSTEMGADGET 1
#define WDF_LBG_MULTIPLE 2
#define WDM_LAYOUTSCALEBORDERGADGETS 17
#define GTYP_SYSTYPEMASK 255
#define GTYP_CUSTOMGADGET 0
#define GTYP_SIZING 1
#define GTYP_CLOSE 2
#define GTYP_WDEPTH 3
#define GTYP_WZOOM 4
#define GTYP_WDRAGGING 5
#define WFLG_CLOSEGADGET 1
#define WFLG_DEPTHGADGET 2
#define WFLG_HASZOOM 4
#define WFLG_SIZEGADGET 8
#define GFLG_RELRIGHT 1
#define GFLG_RELBOTTOM 2
#define GFLG_RELWIDTH 4
#define GFLG_RELHEIGHT 8
#define GFLG_EXTENDED 16
#define GACT_TOPBORDER 1
#define GMORE_BOOPSIGADGET 1
#define TAG_DONE 0
'''
pre+=''.join(x+'\n' for x in ext.splitlines() if x.startswith('#define ET'))
for name in sorted(set(re.findall(r'\b(?:GA|PGA)_[A-Za-z]+',function(s,'windecor_layout_bordergadgets')))):pre+='#define '+name+' 1\n'
pre+=''.join(structure(h,n) for n in ['wdpLayoutBorderGadgets','wdpLayoutScaleBorderGadgets'])
pre+='''static BOOL GetAttr(ULONG key,Object*obj,IPTR*out){(void)key;(void)obj;(void)out;assert(!"client getter in server-only layout");return FALSE;}static IPTR SetAttrs(Object*obj,...){(void)obj;assert(!"client setter in server-only layout");return 0;}\n'''
body='static IPTR window_layout_border_gadgets(struct wdpLayoutBorderGadgets*msg)'+function(builtin,'window_layout_border_gadgets')+'\n'
# DoSuperMethodA reaches the actual built-in method as the superclass would:
# cl is the built-in class while OCLASS(obj) stays the Decoration subclass.
body+='static IPTR WinDecorClass__WDM_LAYOUTSCALEBORDERGADGETS(Class *cl, Object *obj, struct wdpLayoutScaleBorderGadgets *msg)'+function(builtin,'WinDecorClass__WDM_LAYOUTSCALEBORDERGADGETS')+'\n'
body+='static Class superclass;static BOOL superRefuses;static IPTR DoSuperMethodA(Class*cl,Object*obj,Msg msg){assert(cl==actualClass);struct wdpLayoutScaleBorderGadgets*m=msg;assert(m->base.MethodID==WDM_LAYOUTSCALEBORDERGADGETS&&m->context);if(superRefuses)return FALSE;return WinDecorClass__WDM_LAYOUTSCALEBORDERGADGETS(&superclass,obj,m);}\n'
body+='static IPTR windecor_layout_bordergadgets(Class*cl,Object*obj,struct wdpLayoutBorderGadgets*msg,struct wdpScaleRefusal*refusal)'+function(s,'windecor_layout_bordergadgets')+'\n'
body+='static IPTR windecor_layoutscalebordergadgets(Class*cl,Object*obj,struct wdpLayoutScaleBorderGadgets*msg)'+function(s,'windecor_layoutscalebordergadgets')+'\n'
main=r'''
static unsigned pct(unsigned n,unsigned s){return(n*s+50)/100;}
int main(void){Class cl;actualClass=&cl;struct Screen screen={0};struct TextFont basefont={13};struct DrawInfo baseDI={&basefont};struct DecorConfig config={.BarHeight=19,.RightBorderGadgets=8,.BottomBorderGadgets=8,.BarPreGadget_s=2,.BarPostGadget_s=3,.GadgetsThreeState=FALSE};struct DecorImages sourceImages={0};
#define BUILD_SOURCE(id) sourceImages.img_##id=DNewDecorImageContainer(56,19);assert(sourceImages.img_##id);sourceImages.img_##id->ok=TRUE;sourceImages.img_##id->subimagescols=4;
WINDOW_SCALE_IMAGE_LIST(BUILD_SOURCE)
#undef BUILD_SOURCE
DDisposeImageContainer(sourceImages.img_winbar_normal);sourceImages.img_winbar_normal=DNewDecorImageContainer(128,38);sourceImages.img_winbar_normal->ok=TRUE;sourceImages.img_winbar_normal->subimagesrows=2;struct DecorThemeScreen theme={.dts_Screen=&screen,.dts_Images=&sourceImages};struct ScreenData sd={.dts=&theme,.di=&sourceImages};struct windecor_data data={.dc=&config};instance=&data;unsigned cases=0;
for(unsigned closeRight=0;closeRight<2;closeRight++)for(unsigned three=0;three<2;three++)for(unsigned top=19;top<=20;top++){config.CloseGadgetOnRight=closeRight;config.GadgetsThreeState=three;
#define SET_STATES(id) if(sourceImages.img_##id!=sourceImages.img_winbar_normal){sourceImages.img_##id->subimagescols=three?3:4;sourceImages.img_##id->w=three?42:56;}
WINDOW_SCALE_IMAGE_LIST(SET_STATES)
#undef SET_STATES
struct Gadget baseline[9];for(unsigned scale=100;scale<=400;scale+=25){struct ScreenRenderContext ctx={scale,&baseDI,&baseDI};struct TextFont font={pct(13,scale)};struct WindowData wd={0};struct wdpPrepareScaleWindow prep={.base={.wdp_UserBuffer=(IPTR)&wd,.wdp_ScreenUserBuffer=(IPTR)&sd,.wdp_Screen=&screen},.context=&ctx,.font=&font,.width=pct(300,scale),.height=pct(200,scale),.borderTop=pct(top,scale)};assert(windecor_preparescalewindow(&cl,NULL,&prep));struct Window w={.Width=pct(300,scale),.Height=pct(200,scale),.BorderTop=pct(top,scale),.Flags=WFLG_CLOSEGADGET|WFLG_DEPTHGADGET|WFLG_HASZOOM};for(unsigned index=0;index<9;index++){struct Gadget g={.Width=pct(14,scale),.Height=pct(top,scale),.GadgetType=index<3?(index==0?GTYP_CLOSE:index==1?GTYP_WDEPTH:GTYP_WZOOM):GTYP_CUSTOMGADGET,.GadgetID=index==3?ETI_MUI:index==4?ETI_PopUp:index==5?ETI_Snapshot:index==6?ETI_Iconify:index==7?ETI_Lock:ETI_Jump};struct wdpLayoutBorderGadgets layout={.wdp_Window=&w,.wdp_Gadgets=&g,.wdp_Flags=WDF_LBG_SYSTEMGADGET,.wdp_ExtraButtons=ETG_MUI|ETG_POPUP|ETG_SNAPSHOT|ETG_ICONIFY|ETG_LOCK,.wdp_UserBuffer=(IPTR)&wd};assert(windecor_layout_bordergadgets(&cl,NULL,&layout,NULL));if(scale==100)baseline[index]=g;if(index<8){unsigned canonicalWidth=(14*top+19/2)/19;assert(g.Width==(LONG)pct(canonicalWidth,scale)&&g.Height==(LONG)pct(top,scale));if(scale%100==0){unsigned f=scale/100;LONG left=g.LeftEdge+((g.Flags&GFLG_RELRIGHT)?w.Width-1:0),oldleft=baseline[index].LeftEdge+((baseline[index].Flags&GFLG_RELRIGHT)?299:0);assert(left==oldleft*(LONG)f&&g.TopEdge==baseline[index].TopEdge*(LONG)f);}}cases++;}struct wdpExitWindow exit={.wdp_UserBuffer=(IPTR)&wd};windecor_exitwindow(&cl,NULL,&exit);}}
/* Intuition's typed call: success leaves the record clear; each refusal names its cause. */
{struct ScreenRenderContext ctx={200,&baseDI,&baseDI};struct TextFont font={26};struct WindowData wd={0};struct wdpPrepareScaleWindow prep={.base={.wdp_UserBuffer=(IPTR)&wd,.wdp_ScreenUserBuffer=(IPTR)&sd,.wdp_Screen=&screen},.context=&ctx,.font=&font,.width=600,.height=400,.borderTop=38};assert(windecor_preparescalewindow(&cl,NULL,&prep));struct Window w={.Width=600,.Height=400,.BorderTop=38,.Flags=WFLG_CLOSEGADGET|WFLG_DEPTHGADGET};struct Gadget g={.Width=28,.Height=38,.GadgetType=GTYP_CLOSE};struct wdpScaleRefusal r={0};struct wdpLayoutScaleBorderGadgets m={.base={.MethodID=WDM_LAYOUTSCALEBORDERGADGETS,.wdp_Window=&w,.wdp_Gadgets=&g,.wdp_Flags=WDF_LBG_SYSTEMGADGET,.wdp_UserBuffer=(IPTR)&wd},.context=&ctx,.refusal=&r};
 assert(windecor_layoutscalebordergadgets(&cl,NULL,&m)&&!r.wsr_Code);
 superRefuses=TRUE;assert(!windecor_layoutscalebordergadgets(&cl,NULL,&m)&&r.wsr_Code==WDP_REFUSE_SUPERCLASS&&r.wsr_Value[0]==WDM_LAYOUTSCALEBORDERGADGETS);superRefuses=FALSE;
 memset(&r,0,sizeof r);struct ScreenRenderContext other=ctx;m.context=&other;assert(!windecor_layoutscalebordergadgets(&cl,NULL,&m)&&r.wsr_Code==WDP_REFUSE_STATE);m.context=&ctx;
 memset(&r,0,sizeof r);m.base.wdp_Gadgets=NULL;assert(!windecor_layoutscalebordergadgets(&cl,NULL,&m)&&r.wsr_Code==WDP_REFUSE_ARGUMENTS&&r.wsr_Value[0]==200);m.base.wdp_Gadgets=&g;
 Class other_class;memset(&r,0,sizeof r);actualClass=&other_class;assert(!windecor_layoutscalebordergadgets(&cl,NULL,&m)&&r.wsr_Code==WDP_REFUSE_UNSUPPORTED);actualClass=&cl;
 struct wdpExitWindow exit={.wdp_UserBuffer=(IPTR)&wd};windecor_exitwindow(&cl,NULL,&exit);
 puts("PASS actual Decoration typed layout through the actual built-in superclass method: delegated layout admitted, superclass/state/argument/class refusals named");}
#define FREE_SOURCE(id) DDisposeImageContainer(sourceImages.img_##id);
WINDOW_SCALE_IMAGE_LIST(FREE_SOURCE)
#undef FREE_SOURCE
assert(allocations==frees);printf("PASS actual builtin+theme layout/prepared resources: %u cases all13scales/3+4states/left+rightclose/oddaspect, image+gadget extent coherent, resolved integer positions\n",cases);return 0;}
'''
variants=[('actual',body),('drop-relative-inset',body.replace('gadget->LeftEdge -= (wd->scaleData->scale + 50) / 100 - 1;','gadget->LeftEdge -= 0;')),
 ('base-exact-class',body.replace('if (!msg->context || !msg->base.wdp_Window || !msg->base.wdp_Gadgets)','if (OCLASS(obj) != cl || !msg->context || !msg->base.wdp_Window || !msg->base.wdp_Gadgets)',1)),
 ('superclass-refusal-unnamed',body.replace('window_scale_refuse(refusal, WDP_REFUSE_SUPERCLASS,','window_scale_refuse(refusal, WDP_REFUSE_NONE,'))]
with tempfile.TemporaryDirectory(prefix='window-theme-layout-') as tmp:
 tmp=pathlib.Path(tmp)
 for name,code in variants:
  c=tmp/(name+'.c');exe=tmp/name;c.write_text(pre+macros+'\n'+structs+functions+code+main);subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-but-set-variable','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if name=='actual':
   if q.returncode:raise SystemExit(q.stderr)
   print(q.stdout,end='')
  elif not q.returncode:raise SystemExit('escaped mutation '+name)
  else:print('REJECTED '+name)
