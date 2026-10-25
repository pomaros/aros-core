#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL) Version 1.1.
"""Actual theme SYSI preparation/dispatch/ownership, natural-render handoff.
Shares the exact source/type extraction with the WindowData preparation fixture.
Rendering services are sentinels; state-sheet pixels have a separate real oracle.
"""
import pathlib,subprocess,tempfile
helper=pathlib.Path(__file__).with_name('windowtheme_prepare_host.py')
exec(compile(helper.read_text().split("main=r'''",1)[0],str(helper),'exec'))
pre=pre.replace('typedef int BOOL;','typedef int BOOL;typedef int8_t BYTE;\n#define STACKED\n').replace('REFUSAL_RECORD',refusal)
pre=pre.replace('static unsigned ordinal,failAt,allocations,frees;','static unsigned ordinal,failAt,allocations,frees;static BOOL trap;')
pre=pre.replace('(void)flags;if(++ordinal','(void)flags;assert(!trap);if(++ordinal')
pre=pre.replace('static void FreeVec(void*p){if(p)','static void FreeVec(void*p){assert(!trap);if(p)')
h=(a.source/'compiler/include/intuition/windecorclass.h').read_text()
pre+='\nstruct RastPort{int unused;};\n'+''.join(structure(h,n) for n in ['wdpGetDefSizeSysImage','wdpGetScaleDefSizeSysImage','wdpDrawSysImage','wdpDrawScaleSysImage','wdpReleaseScaleSysImage'])
pre+='''\n#define WDP_SCALE_FAILED 0
#define WDP_SCALE_READY 1
#define WDP_SCALE_UNAVAILABLE 2
enum{DEPTHIMAGE=0x00,ZOOMIMAGE=0x01,SIZEIMAGE=0x02,CLOSEIMAGE=0x03,LEFTIMAGE=0x0A,UPIMAGE=0x0B,RIGHTIMAGE=0x0C,DOWNIMAGE=0x0D,ICONIFYIMAGE=0x12,LOCKIMAGE=0x13,MUIIMAGE=0x14,POPUPIMAGE=0x15,SNAPSHOTIMAGE=0x16,JUMPIMAGE=0x17};
static unsigned draws,blits;static LONG expectedWidth,expectedHeight;
/* The cached title bar is a bitmap of barWidth x barRows pixels: every
 * reblit must stay inside it (M1 generation 51: the size gadget's corner of
 * a 1100x620 Shell read row 599 of a 20-row bar and faulted input.device). */
static LONG barWidth,barRows,blitX,blitY,blitW,blitH;
static void BltBitMapRastPort(void*bm,LONG sx,LONG sy,struct RastPort*rp,LONG dx,LONG dy,LONG w,LONG h,ULONG mode){assert(bm&&rp&&sx==dx&&sy==dy&&mode==0xc0);assert(sx>=0&&sy>=0&&w>0&&h>0&&sx+w<=barWidth&&sy+h<=barRows);blitX=sx;blitY=sy;blitW=w;blitH=h;blits++;}
#define WFLG_WINDOWACTIVE 0x2000
#define WFLG_TOOLBOX 0x80000000
struct Window{WORD Width,Height;BYTE BorderTop;ULONG Flags;STRPTR Title;};
'''
# DRenderElement needs the real image/element types; append after extracted structs.
render='''static void DRenderElement(struct DecoratorElement*e,struct RastPort*rp,ULONG state,LONG x,LONG y,LONG w,LONG h,ULONG flags){(void)state;(void)x;(void)y;(void)flags;assert(trap&&rp&&w==-1&&h==-1);assert(e->de_Image->w/e->de_SubImageCols==expectedWidth&&e->de_Image->h/e->de_SubImageRows==expectedHeight);draws++;}\n'''
macros+='\n'+'\n'.join(x for x in decor.splitlines() if x.startswith('#define DEF_'))
body=structure(s,'WindowScaleSysImage')+'\n'+'\n'.join(x for x in s.splitlines() if x.startswith('#define CHANGE_'))+'\n'
body+='static ULONG HasTitleBarChanged(struct CachedTitleBar *cached, struct Window *window)'+function(s,'HasTitleBarChanged')+'\n'
body+='static void CacheTitleBar(struct CachedTitleBar *cached, struct Window *window, struct BitMap *bitmap)'+function(s,'CacheTitleBar')+'\n'
body+='static BOOL titlebar_image(ULONG which)'+function(s,'titlebar_image')+'\n'
body+='static void reblit_titlebar(struct WindowData *wd, struct RastPort *rp, LONG left, LONG top, LONG width, LONG height)'+function(s,'reblit_titlebar')+'\n'
for name,decl in [('windecor_getscaledefsizes','static IPTR windecor_getscaledefsizes(Class*cl,Object*obj,struct wdpGetScaleDefSizeSysImage*msg)'),('windecor_drawscalesysimage','static IPTR windecor_drawscalesysimage(Class*cl,Object*obj,struct wdpDrawScaleSysImage*msg)'),('windecor_releasescalesysimage','static IPTR windecor_releasescalesysimage(Class*cl,Object*obj,struct wdpReleaseScaleSysImage*msg)')]:body+=decl+function(s,name)+'\n'
main=r'''
int main(void){Class cl,unknown;actualClass=&cl;struct DecorConfig config={.BarHeight=19,.RightBorderGadgets=8,.BottomBorderGadgets=9,.UpDownAddY=1,.LeftRightAddX=2};struct windecor_data data={.dc=&config};instance=&data;struct DecorImage*sheet=DNewDecorImageContainer(42,38);assert(sheet);sheet->ok=TRUE;sheet->subimagescols=3;sheet->subimagesrows=2;data.img_size=data.img_close=data.img_depth=data.img_zoom=data.img_mui=data.img_popup=data.img_snapshot=data.img_iconify=data.img_lock=data.img_up=data.img_down=data.img_left=data.img_right=sheet;struct TextFont font={13};struct DrawInfo di={&font};struct RastPort rp;unsigned cases=0,faults=0;
for(unsigned scale=100;scale<=400;scale+=25)for(unsigned logical=0;logical<2;logical++)for(unsigned ti_=0,type;ti_<sizeof((unsigned[]){DEPTHIMAGE,ZOOMIMAGE,SIZEIMAGE,CLOSEIMAGE,LEFTIMAGE,UPIMAGE,RIGHTIMAGE,DOWNIMAGE,ICONIFYIMAGE,LOCKIMAGE,MUIIMAGE,POPUPIMAGE,SNAPSHOTIMAGE})/sizeof(unsigned)&&((type=((unsigned[]){DEPTHIMAGE,ZOOMIMAGE,SIZEIMAGE,CLOSEIMAGE,LEFTIMAGE,UPIMAGE,RIGHTIMAGE,DOWNIMAGE,ICONIFYIMAGE,LOCKIMAGE,MUIIMAGE,POPUPIMAGE,SNAPSHOTIMAGE})[ti_]),1);ti_++){
struct ScreenRenderContext ctx={scale,&di,&di};ULONG width=0,height=0;APTR handle=NULL;struct wdpGetScaleDefSizeSysImage q={.base={.wdp_Which=type,.wdp_Width=&width,.wdp_Height=&height,.wdp_ReferenceFont=&font},.context=&ctx,.logical=logical,.prepared=&handle};unsigned pct=logical?100:scale,bw=14,bh=19;if(type==SIZEIMAGE){bw=8;bh=9;}if(type==UPIMAGE||type==DOWNIMAGE){bw=8;bh=20;}if(type==LEFTIMAGE||type==RIGHTIMAGE){bw=16;bh=9;}unsigned ew=(bw*pct+50)/100,eh=(bh*pct+50)/100;
unsigned owners=allocations-frees;for(unsigned failure=0;failure<=3;failure++){ordinal=0;failAt=failure;IPTR status=windecor_getscaledefsizes(&cl,NULL,&q);failAt=0;assert(status==(failure?0:1));if(failure){assert(!handle&&!width&&!height);faults++;}else{assert(handle&&width==ew&&height==eh);expectedWidth=width;expectedHeight=height;struct WindowData wd={0};wd.tbar.bm=(void*)1;wd.tbar.width=1100;wd.tbar.bordertop=height;barWidth=1100;barRows=height;BOOL bar=type!=SIZEIMAGE&&type!=LEFTIMAGE&&type!=RIGHTIMAGE&&type!=UPIMAGE&&type!=DOWNIMAGE;
/* Title gadgets sit in the bar, the others at the 1100x620 window's right/bottom edge. */
LONG x=1100-(LONG)width-4,y=bar?0:620-(LONG)height;struct wdpDrawScaleSysImage draw={.base={.wdp_RPort=&rp,.wdp_X=x,.wdp_Y=y,.wdp_Width=width,.wdp_Height=height,.wdp_Which=type,.wdp_UserBuffer=(IPTR)&wd},.context=&ctx,.logical=logical,.prepared=handle};unsigned a0=allocations,f0=frees;trap=TRUE;blits=0;assert(windecor_drawscalesysimage(&cl,NULL,&draw));assert(blits==(unsigned)bar);if(bar)assert(blitX==x&&blitY==0&&blitW==(LONG)width&&blitH==(LONG)height);
/* A bar lower than the gadget (a stale cache) restores only its own rows. */
if(bar&&height>2){wd.tbar.bordertop=barRows=height-2;blits=0;assert(windecor_drawscalesysimage(&cl,NULL,&draw));assert(blits==1&&blitH==(LONG)height-2);wd.tbar.bordertop=barRows=height;}draw.context=NULL;assert(!windecor_drawscalesysimage(&cl,NULL,&draw));trap=FALSE;assert(a0==allocations&&f0==frees);struct wdpReleaseScaleSysImage release={.prepared=handle};assert(windecor_releasescalesysimage(&cl,NULL,&release));handle=NULL;}assert(allocations-frees==owners);}
q.prepared=NULL;ordinal=0;assert(windecor_getscaledefsizes(&cl,NULL,&q)==1&&width==ew&&height==eh&&!ordinal);
q.requestedWidth=17;q.requestedHeight=23;q.prepared=&handle;assert(windecor_getscaledefsizes(&cl,NULL,&q)==1&&width==17&&height==23);struct wdpReleaseScaleSysImage release={.prepared=handle};windecor_releasescalesysimage(&cl,NULL,&release);handle=NULL;
q.base.wdp_Which=JUMPIMAGE;assert(windecor_getscaledefsizes(&cl,NULL,&q)==2&&!handle);q.base.wdp_Which=999;assert(windecor_getscaledefsizes(&cl,NULL,&q)==0&&!handle);q.base.wdp_Which=type;actualClass=&unknown;ordinal=0;assert(windecor_getscaledefsizes(&cl,NULL,&q)==0&&!handle&&!ordinal);actualClass=&cl;cases++;}
/* Typed size refusals name their cause, key and THEME: image. */
{struct ScreenRenderContext ctx={200,&di,&di};ULONG width=0,height=0;APTR handle=NULL;struct wdpScaleRefusal r={0};struct wdpGetScaleDefSizeSysImage q={.base={.wdp_Which=CLOSEIMAGE,.wdp_Width=&width,.wdp_Height=&height,.wdp_ReferenceFont=&font},.context=&ctx,.prepared=&handle,.refusal=&r};
#define QREFUSED(code,a0,a1,a2,nm) (memset(&r,0,sizeof r),windecor_getscaledefsizes(&cl,NULL,&q)==WDP_SCALE_FAILED&&!handle&&r.wsr_Code==(code)&&r.wsr_Value[0]==(LONG)(a0)&&r.wsr_Value[1]==(LONG)(a1)&&r.wsr_Value[2]==(LONG)(a2)&&!strcmp((char*)r.wsr_Name,nm))
 assert(windecor_getscaledefsizes(&cl,NULL,&q)==WDP_SCALE_READY&&!r.wsr_Code&&handle);struct wdpReleaseScaleSysImage release={.prepared=handle};windecor_releasescalesysimage(&cl,NULL,&release);handle=NULL;
 actualClass=&unknown;assert(QREFUSED(WDP_REFUSE_UNSUPPORTED,0,0,0,""));actualClass=&cl;
 q.base.wdp_Which=999;assert(QREFUSED(WDP_REFUSE_ARGUMENTS,200,0,0,""));q.base.wdp_Which=CLOSEIMAGE;
 ctx.src_Scale=210;assert(QREFUSED(WDP_REFUSE_ARGUMENTS,210,0,0,""));ctx.src_Scale=200;
 sheet->w=43;assert(QREFUSED(WDP_REFUSE_IMAGE_GRID,43,38,302,"System/Close/Default"));sheet->w=42;
 q.base.wdp_Which=SIZEIMAGE;config.RightBorderGadgets=0;assert(QREFUSED(WDP_REFUSE_CONFIG,0,200,0,"RightBorderGadgets"));config.RightBorderGadgets=8;
 config.BottomBorderGadgets=-2;assert(QREFUSED(WDP_REFUSE_CONFIG,-2,200,0,"BottomBorderGadgets"));config.BottomBorderGadgets=9;
 q.requestedHeight=40000;assert(QREFUSED(WDP_REFUSE_IMAGE_EXTENT,16,40000,0,"System/Size/Default"));q.requestedHeight=0;
 ordinal=0;failAt=1;assert(QREFUSED(WDP_REFUSE_NO_MEMORY,sizeof(struct WindowScaleSysImage),0,0,""));
 ordinal=0;failAt=2;assert(QREFUSED(WDP_REFUSE_IMAGE_EXTENT,16,18,0,"System/Size/Default"));failAt=0;
 puts("PASS actual Decoration typed size refusals: class, arguments, grid with THEME: name, named config keys, extent, allocation");}
/* A new screen font keeps the outer size but changes BorderTop: the cached bar
 * (exactly BorderTop rows) must be regenerated, not reused or blitted. */
{struct Window w={1100,620,20,WFLG_WINDOWACTIVE,"AROS Shell"};struct CachedTitleBar c={0};CacheTitleBar(&c,&w,(void*)1);assert(c.bordertop==20&&HasTitleBarChanged(&c,&w)==CHANGE_NO_CHANGE);w.BorderTop=24;assert(HasTitleBarChanged(&c,&w)==CHANGE_SIZE_CHANGE);w.BorderTop=20;w.Flags=0;assert(HasTitleBarChanged(&c,&w)==CHANGE_NO_SIZE_CHANGE);FreeVec(c.title);
 puts("PASS actual Decoration title bar cache: a BorderTop change regenerates the bar bitmap");}
DDisposeImageContainer(sheet);assert(allocations==frees);printf("PASS actual theme SYSI: %u cases/%u allocation faults, logical+physical metrics, eager requested extents, natural drawing handoff hardtraps, ownership and unsupported/missing/OOM distinction\n",cases,faults);return 0;}
'''
variants=[('actual',body),('double-scale',body.replace('scale = msg->logical ? 100 : msg->context->src_Scale;','scale = msg->context->src_Scale;')),('leak-handle',body.replace('FreeVec(prepared);','(void)prepared;')),('lazy-draw',body.replace('base->wdp_X, base->wdp_Y, -1, -1, 0','base->wdp_X, base->wdp_Y, base->wdp_Width, base->wdp_Height, 0')),('oom-as-missing',body.replace('window_scale_refuse(refusal, WDP_REFUSE_NO_MEMORY, sizeof(*prepared), 0, 0, NULL);\n        return WDP_SCALE_FAILED;','window_scale_refuse(refusal, WDP_REFUSE_NO_MEMORY, sizeof(*prepared), 0, 0, NULL);\n        return WDP_SCALE_UNAVAILABLE;')),
 ('grid-refusal-unnamed',body.replace('source->subimagescols * 100 + source->subimagesrows % 100, window_scale_image_name(id));','source->subimagescols * 100 + source->subimagesrows % 100, NULL);')),
 ('size-gadget-reblit',body.replace('    if (titlebar_image(base->wdp_Which))\n        reblit_titlebar(','    if (1)\n        reblit_titlebar(').replace('if (bottom > (LONG)wd->tbar.bordertop) bottom = wd->tbar.bordertop;','')),
 ('unclamped-reblit',body.replace('if (bottom > (LONG)wd->tbar.bordertop) bottom = wd->tbar.bordertop;','')),
 ('bar-height-unkeyed',body.replace('if (cached->bordertop != window->BorderTop)\n        return CHANGE_SIZE_CHANGE;','')),
 ('size-config-wrong-key',body.replace('right ? "RightBorderGadgets" : "BottomBorderGadgets");','right ? "BottomBorderGadgets" : "RightBorderGadgets");'))]
with tempfile.TemporaryDirectory(prefix='window-theme-sysi-') as tmp:
 tmp=pathlib.Path(tmp)
 for name,code in variants:
  assert name=='actual' or code!=body,name
  c=tmp/(name+'.c');exe=tmp/name;c.write_text(pre+macros+'\n'+structs+render+functions+code+main);subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if name=='actual':
   if q.returncode:raise SystemExit(q.stderr)
   print(q.stdout,end='')
  elif q.returncode==0:raise SystemExit('escaped mutant '+name)
  else:print('REJECTED '+name)
