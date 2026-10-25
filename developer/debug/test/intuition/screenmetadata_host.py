#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Actual established logical metadata and gadget destination selection.
Frame lookup is a controlled adapter; complete frame ownership/publication is
covered by windowframe_host.py, immutable164 lifetime by screenrendercontext_host.py.
"""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[4]
def block(text,marker):
 a=text.index(marker);opening=text.index('{',a);i=opening+1;depth=1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[a:i]
misc=(ROOT/'rom/intuition/misc.c').read_text()
functions='\n'.join(block(misc,n) for n in ['void ScreenFontTextAttr(', 'void DisposeLogicalScreenInfo(', 'struct LogicalScreenInfo *PrepareLogicalScreenInfo(', 'void CommitLogicalScreenInfo(', 'void FreeLogicalScreenInfo(', 'struct DrawInfo *LogicalScreenDrawInfo(', 'struct DrawInfo *WindowDrawInfo('])
gadget=block((ROOT/'rom/intuition/inputhandler_support.c').read_text(encoding='latin1'),'void PrepareGadgetInfo(')
window='w->ScaleAware=scaleaware;w->frameInfo=scaleaware?IS(screen)->DInfo:*LogicalScreenDrawInfo(screen);w->frameInfo.dri_Font=SafeReopenFont(IntuitionBase,&w->frameInfo.dri_Font);'
screenclass=(ROOT/'rom/intuition/screenclass.c').read_text()
# Physical transaction publication has separate fault/cutpoint coverage. This
# unit sets up a scaled physical environment to test logical generation lifetime.
scale_source=(ROOT/'rom/intuition/windowscale.c').read_text()
apply=block(scale_source,'BOOL ApplyPreparedScreenScale(')
assert apply.index('logical = PrepareLogicalScreenInfo(') < apply.index('install_scale(&install, FALSE')
assert 'CommitLogicalScreenInfo(is, prepared->logical);' in block(scale_source,'static void install_scale(')
physical='is->DInfo.dri_Font=font;is->Scale=scale;SetFont(&scr->RastPort,font);ScreenFontTextAttr(font,&is->textattr);scr->BarHeight=font->tf_YSize+2;'
assert 'gi->gi_DrInfo    = WindowDrawInfo(gi->gi_Screen, gi->gi_Window);' in (ROOT/'rom/intuition/gadgets.c').read_text(encoding='latin1')
assert 'return LogicalScreenDrawInfo(screen);' in (ROOT/'rom/intuition/getscreendrawinfo.c').read_text()
assert 'SA_RenderContext' in (ROOT/'compiler/include/intuition/screens.h').read_text(encoding='latin1')
openscreen=(ROOT/'rom/intuition/openscreen.c').read_text()
a=openscreen.index('    if (ok)\n    {\n        struct LogicalScreenInfo *logical = PrepareLogicalScreenInfo(')
b=openscreen.index('        ok = int_InitDecorator(&screen->Screen, screen->ScaleBaseFont, screen->Scale);',a)+len('        ok = int_InitDecorator(&screen->Screen, screen->ScaleBaseFont, screen->Scale);')
constructor=openscreen[a:b]+'\n    }\n    return ok;\n}\n'
constructor='static BOOL ConstructMetadata(struct IntScreen *screen,struct IntuitionBase *IntuitionBase) {BOOL ok=TRUE;\n'+constructor
PRE=r''' #include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef unsigned int ULONG;typedef uintptr_t IPTR;typedef int BOOL;
#define DEBUG_OPENSCREEN(x)
#define MEMF_PUBLIC 0
#define MEMF_CLEAR 0
#define FSF_TAGGED 128
#define TAG_DONE 0
#define SYSIA_Which 1
#define SYSIA_DrawInfo 2
#define MENUCHECK 1
#define AMIGAKEY 2
#define SUBMENUIMAGE 3
#define SYSICLASS "sysiclass"
#define SA_SCALE_MIN 100
#define TRUE 1
#define FALSE 0
struct TextFontExtension {void *tfe_Tags;};
struct TextFont {struct {struct {char *ln_Name;}mn_Node;}tf_Message;unsigned tf_YSize,tf_Style,tf_Flags;struct TextFontExtension *tf_Extension;int refs;};
struct TextAttr {char *ta_Name;unsigned ta_YSize,ta_Style,ta_Flags;};
struct TTextAttr {char *tta_Name;unsigned tta_YSize,tta_Style,tta_Flags;void *tta_Tags;};
struct Image {int height;};typedef struct Image Object;
struct IntuitionCustomize {struct Image *submenu;};
struct DrawInfo {struct TextFont *dri_Font;struct Image *dri_CheckMark,*dri_AmigaKey;struct IntuitionCustomize *dri_Customize;unsigned short *dri_Pens;};
#define IntDrawInfo DrawInfo
struct LogicalScreenInfo {struct LogicalScreenInfo *previous;struct IntDrawInfo drawinfo;struct IntuitionCustomize customize;struct TTextAttr fontattr;};
struct RastPort {struct TextFont *Font;};
struct IntScreen;struct Screen {struct TextAttr *Font;unsigned DetailPen,BlockPen;struct RastPort RastPort;int BarHeight,WBorTop,BarVBorder,BarHBorder,MenuVBorder,MenuHBorder;};
struct IntScreen {struct Screen Screen;struct IntDrawInfo DInfo;struct LogicalScreenInfo *LogicalInfo;struct TextFont *ScaleBaseFont;unsigned Scale;BOOL SysFont,ScaleSysFont;struct TTextAttr textattr;};
struct Window {struct Screen *WScreen;BOOL ScaleAware;struct IntDrawInfo frameInfo;};
#define IS(p) ((struct IntScreen *)(p))
#define IW(p) (p)
struct GfxBase {int unused;};struct IntuitionBase {struct GfxBase *GfxBase;};
#define GetPrivIBase(p) (p)
struct TagItem {IPTR ti_Tag,ti_Data;};
struct Requester {int unused;};
struct GadgetInfo {struct Screen *gi_Screen;struct Window *gi_Window;struct Requester *gi_Requester;void *gi_RastPort;struct {unsigned DetailPen,BlockPen;}gi_Pens;struct DrawInfo *gi_DrInfo;};
struct DrawInfo *WindowFrameDrawInfo(struct Window*w){return &w->frameInfo;}
static void SetFont(struct RastPort *rp,struct TextFont *font) {rp->Font=font;}
static void AskFont(struct RastPort *rp,struct TextAttr *attr) {attr->ta_YSize=rp->Font->tf_YSize;}
static int decoratorcalls;
static BOOL int_InitDecorator(struct Screen *screen,struct TextFont*font,ULONG scale) {(void)screen;(void)font;(void)scale;decoratorcalls++;return TRUE;}
static int allocations,images,failallocation,failimage,imagecalls;
static void *AllocVec(size_t size,int flags) {(void)flags;if(failallocation)return NULL;allocations++;return calloc(1,size);}
static void FreeVec(void *p) {if(p){allocations--;free(p);}}
static void CloseFont(struct TextFont *f) {assert(f->refs>0);f->refs--;}
static struct TextFont *SafeReopenFont(struct IntuitionBase *b,struct TextFont **font) {(void)b;if(*font)(*font)->refs++;return *font;}
static void DisposeObject(Object *o) {if(o){images--;free(o);}}
static Object *NewObjectA(void *base,const char *type,struct TagItem *tags) {
(void)base;(void)type;imagecalls++;if(imagecalls==failimage)return NULL;
struct DrawInfo *d=(void *)tags[1].ti_Data;Object *o=malloc(sizeof(*o));assert(o);o->height=d->dri_Font->tf_YSize;images++;return o;}
'''
TEST=r'''
static int InitWindow(struct Window *w,struct Screen *screen,int scaleaware,struct IntuitionBase *IntuitionBase) {
WINDOW_INIT
return 1;
}
int main(void) {
struct GfxBase gfx;struct IntuitionBase base={&gfx};
int tags[]={42,0};struct TextFontExtension ext={tags};
struct TextFont f13={.tf_YSize=13,.tf_Message.mn_Node.ln_Name="arial.font",.tf_Style=FSF_TAGGED,.tf_Extension=&ext,.refs=1};
struct TextFont f26={.tf_YSize=26,.tf_Message.mn_Node.ln_Name="arial.font",.refs=1};
struct TextFont f17={.tf_YSize=17,.tf_Message.mn_Node.ln_Name="other.font",.refs=1};
struct Image physicalimage={26};struct IntuitionCustomize physicalcustom={&physicalimage};
unsigned short pens[]={0,1};struct IntScreen screen={0};screen.ScaleBaseFont=&f13;
screen.DInfo.dri_Font=&f13;screen.DInfo.dri_CheckMark=screen.DInfo.dri_AmigaKey=&physicalimage;screen.DInfo.dri_Customize=&physicalcustom;screen.DInfo.dri_Pens=pens;
struct IntScreen failedscreen=screen;failallocation=1;
assert(!ConstructMetadata(&failedscreen,&base)&&!failedscreen.LogicalInfo&&!failedscreen.Screen.Font&&!decoratorcalls);
failallocation=0;
for(int failure=1;failure<=3;failure++){
 failedscreen=screen;failimage=imagecalls+failure;
 assert(!ConstructMetadata(&failedscreen,&base)&&!failedscreen.LogicalInfo&&!failedscreen.Screen.Font&&!decoratorcalls);
 failimage=0;assert(!allocations&&!images&&f13.refs==1);
}
struct LogicalScreenInfo *logical=PrepareLogicalScreenInfo(&screen,&f13,&base);assert(logical);CommitLogicalScreenInfo(&screen,logical);
struct TextAttr *borrowed=screen.Screen.Font;struct DrawInfo *borroweddi=LogicalScreenDrawInfo(&screen);
assert(borrowed->ta_YSize==13&&logical->fontattr.tta_Tags==tags&&f13.refs==2&&images==3);
/* Physical drawfont/bar/RastPort change independently; public metadata does not. */
screen.Screen.WBorTop=2;screen.Screen.BarVBorder=1;PublishPhysical(&screen,&f26,200);
assert(screen.Screen.RastPort.Font==&f26&&screen.Screen.BarHeight==28&&screen.textattr.tta_YSize==26);
assert(PrepareLogicalScreenInfo(&screen,&f13,&base)==logical&&allocations==1&&images==3);
assert(screen.Screen.Font->ta_YSize==13&&LogicalScreenDrawInfo(&screen.Screen)->dri_Font->tf_YSize==13);
assert(logical->drawinfo.dri_CheckMark->height==13&&logical->drawinfo.dri_AmigaKey->height==13&&logical->customize.submenu->height==13);
assert(logical->drawinfo.dri_CheckMark!=screen.DInfo.dri_CheckMark&&logical->drawinfo.dri_Pens==pens);
struct Window legacy={0},aware={0};legacy.WScreen=aware.WScreen=&screen.Screen;
assert(InitWindow(&legacy,&screen.Screen,0,&base)&&InitWindow(&aware,&screen.Screen,1,&base));
struct GadgetInfo gi;PrepareGadgetInfo(&gi,&screen.Screen,&legacy,NULL);assert(gi.gi_DrInfo->dri_Font->tf_YSize==13);
PrepareGadgetInfo(&gi,&screen.Screen,&aware,NULL);assert(gi.gi_DrInfo->dri_Font->tf_YSize==26);
PrepareGadgetInfo(&gi,&screen.Screen,NULL,NULL);assert(gi.gi_DrInfo->dri_Font->tf_YSize==26);
assert(legacy.frameInfo.dri_Font->tf_YSize*2==26&&aware.frameInfo.dri_Font->tf_YSize==26);
/* Pending basefont generations never publish on failure. */
for(int n=1;n<=3;n++){failimage=imagecalls+n;assert(!PrepareLogicalScreenInfo(&screen,&f17,&base));failimage=0;assert(screen.LogicalInfo==logical&&screen.Screen.Font==borrowed&&allocations==1&&images==3&&f17.refs==1);}
failallocation=1;assert(!PrepareLogicalScreenInfo(&screen,&f17,&base));failallocation=0;
struct LogicalScreenInfo *pending=PrepareLogicalScreenInfo(&screen,&f17,&base);assert(pending);DisposeLogicalScreenInfo(pending,&base);
assert(screen.LogicalInfo==logical&&allocations==1&&images==3&&f17.refs==1);
pending=PrepareLogicalScreenInfo(&screen,&f17,&base);assert(pending);CommitLogicalScreenInfo(&screen,pending);screen.ScaleBaseFont=&f17;
assert(borrowed->ta_YSize==13&&borroweddi->dri_Font==&f13&&borroweddi->dri_CheckMark->height==13);
assert(screen.Screen.Font->ta_YSize==17&&pending->drawinfo.dri_CheckMark->height==17);
int calls=imagecalls;unsigned scales[]={100,200,300,400,100};
struct TextFont changing={.tf_Message.mn_Node.ln_Name="other.font",.refs=1};
for(int repeat=0;repeat<100;repeat++)for(unsigned i=0;i<5;i++){
 changing.tf_YSize=17*scales[i]/100;PublishPhysical(&screen,&changing,scales[i]);
 assert(PrepareLogicalScreenInfo(&screen,&f17,&base)==pending);CommitLogicalScreenInfo(&screen,pending);
 assert(screen.Screen.Font->ta_YSize==17&&LogicalScreenDrawInfo(&screen.Screen)->dri_Font->tf_YSize==17);
 assert(screen.Screen.RastPort.Font->tf_YSize==17*scales[i]/100&&screen.Screen.BarHeight==17*scales[i]/100+2);
}
assert(imagecalls==calls&&allocations==2&&images==6);
CloseFont(legacy.frameInfo.dri_Font);CloseFont(aware.frameInfo.dri_Font);
FreeLogicalScreenInfo(&screen,&base);FreeLogicalScreenInfo(&screen,&base);
assert(!allocations&&!images&&f13.refs==1&&f26.refs==1&&f17.refs==1);
CloseFont(&f13);CloseFont(&f26);CloseFont(&f17);assert(!f13.refs&&!f26.refs&&!f17.refs);
puts("PASS actual logical metadata/font-tags/images, legacy/aware gadget defaults13/26, pending unwind, retained generations and500 scale/reset-only reuses");return 0;
}
'''
# The test calls helpers with both public Screen and private prefix; actual ABI
# Screen is the first IntScreen field, just as modeled here.
physical_wrapper='static void PublishPhysical(struct IntScreen *is,struct TextFont *font,unsigned scale) {struct Screen *scr=&is->Screen;'+physical+'}\n'
TEST=TEST.replace('LogicalScreenDrawInfo(&screen)',  'LogicalScreenDrawInfo(&screen.Screen)').replace('WINDOW_INIT',window)
with tempfile.TemporaryDirectory(prefix='aros-screenmetadata-') as t:
 d=Path(t)
 def run(label,helpers,flow=constructor):
  unit=d/(label+'.c');binary=d/label;unit.write_text(PRE+helpers+gadget+physical_wrapper+flow+TEST)
  subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
  return subprocess.run([str(binary)],capture_output=True,text=True)
 result=run('production',functions);print(result.stdout,end='');assert result.returncode==0,result.stderr
 mutations={
  'physical_public_font':functions.replace('return IS(screen)->LogicalInfo ? (struct DrawInfo *)&IS(screen)->LogicalInfo->drawinfo : NULL;', 'return (struct DrawInfo *)&IS(screen)->DInfo;'),
  'physical_legacy_gadgets':functions.replace('return (struct DrawInfo *)WindowFrameDrawInfo(window);', 'return (struct DrawInfo *)&IS(screen)->DInfo;'),
  'physical_images':functions.replace('tags[1].ti_Data = (IPTR)&info->drawinfo;', 'tags[1].ti_Data = (IPTR)&screen->DInfo;'),
  'scale_churn':functions.replace('if (screen->LogicalInfo &&', 'if (0 && screen->LogicalInfo &&'),
  'lost_old_generation':functions.replace('info->previous = screen->LogicalInfo;', 'info->previous = NULL;'),
 }
 for label,changed in mutations.items():
  assert changed!=functions,label
  result=run(label,changed);assert result.returncode!=0,label+' escaped detection'
  print('PASS negative control:',label)
 changed=constructor.replace('    if (ok)\n    {\n        SetFont', '    if (TRUE)\n    {\n        SetFont')
 assert changed!=constructor
 result=run('constructor_failure_overwrite',functions,changed);assert result.returncode!=0,'failed constructor overwritten by decorator success'
 print('PASS negative control: constructor failure cannot be overwritten by later decorator success')
