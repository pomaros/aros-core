#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Run actual menu clone/layout and native hit-test bodies under ASan/UBSan.

Exec allocation and graphics font measurement are controlled external boundaries;
no clone/layout/reflow/input conversion algorithm is replaced by a model.
"""
from pathlib import Path
import argparse, json, os, re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[4]

def function(text, name):
    match = list(re.finditer(r'^(?:static )?[^;\n]+\b' + re.escape(name) + r'\([^;]*?\)\n\{', text, re.M))[-1]
    begin = text.index('{', match.start()); depth = 1; end = begin + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[match.start():end] + '\n'

PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
typedef int BOOL; typedef uint8_t UBYTE; typedef int8_t BYTE;
typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint32_t ULONG; typedef int32_t LONG; typedef int64_t QUAD;
typedef uint64_t UQUAD; typedef intptr_t SIPTR; typedef uintptr_t IPTR; typedef void *APTR; typedef void Object;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define ITEMTEXT 2
#define ITEMENABLED 16
#define COMMSEQ 4
#define CHECKIT 1
#define CHECKED 256
#define HIGHITEM 8192
#define HIGHFLAGS 192
#define HIGHIMAGE 0
#define MENUENABLED 1
#define CUSTOMIMAGEDEPTH (-1)
#define FSF_TAGGED 128
#define FPF_REVPATH 4
#define FPF_TALLDOT 8
#define FPF_WIDEDOT 16
#define JAM1 1
#define SHADOWPEN 4
#define SHINEPEN 3
#define SA_RenderContext 164
#define TAG_DONE 0
#define WA_Left 1
#define WA_Top 2
#define WA_Width 3
#define WA_Height 4
#define MDM_GETSCALESPACES 507
#define MDM_QUERYCAPABILITIES 512
#define MDM_CAP_SCALE 1
#define MENUBARLABELCLASS "menubarlabel"
#define OCLASS(o) (((struct Image *)(o))->class)
#define IW(w) (w)
#define IS(s) (s)
#define GetPrivIBase(i) (i)
#define bug(...) ((void)0)
struct Library { int unused; }; struct GfxBase { int unused; };
struct LayersBase { int unused; };
struct IntuitionBase { int MenuLock; struct Library *UtilityBase; struct GfxBase *GfxBase; struct LayersBase *LayersBase; void *IBaseLock; };
struct TagItem { ULONG ti_Tag; IPTR ti_Data; };
struct TextAttr { char *ta_Name; UWORD ta_YSize; UBYTE ta_Style, ta_Flags; };
struct TTextAttr { char *ta_Name; UWORD ta_YSize; UBYTE ta_Style, ta_Flags; struct TagItem *tta_Tags; };
struct TextFontExtension { struct TagItem *tfe_Tags; };
struct TextFont { struct { struct { char *ln_Name; } mn_Node; } tf_Message;
 UWORD tf_YSize, tf_XSize; UBYTE tf_Style, tf_Flags; struct TextFontExtension *tf_Extension; };
struct IClass { int unused; };
struct Image { struct IClass *class; WORD Depth, Width, Height; struct Image *NextImage; };
struct IntuitionCustomize { struct Image *submenu; };
struct DrawInfo { struct TextFont *dri_Font; struct Image *dri_CheckMark,*dri_AmigaKey; UWORD *dri_Pens; struct Screen *dri_Screen; };
struct IntDrawInfo { struct TextFont *dri_Font; struct Image *dri_CheckMark,*dri_AmigaKey; UWORD *dri_Pens; struct Screen *dri_Screen;
 struct IntuitionCustomize *dri_Customize; };
struct RastPort { struct TextFont *Font; int initialized; };
struct Rectangle { WORD MinX,MinY,MaxX,MaxY; };
struct IntuiText { UBYTE FrontPen, BackPen, DrawMode; WORD LeftEdge,TopEdge; struct TextAttr *ITextFont;
 char *IText; struct IntuiText *NextText; };
struct MenuItem { struct MenuItem *NextItem; WORD LeftEdge,TopEdge,Width,Height; UWORD Flags;
 LONG MutualExclude; APTR ItemFill,SelectFill; UBYTE Command; struct MenuItem *SubItem; UWORD NextSelect; };
struct Menu { struct Menu *NextMenu; WORD LeftEdge,TopEdge,Width,Height; UWORD Flags;
 char *MenuName; struct MenuItem *FirstItem; WORD JazzX,JazzY,BeatX,BeatY; };
struct ScreenRenderContext { UWORD src_Scale,src_LegacyScaleFactor; ULONG src_PhysicalBarHeight;
 UWORD src_PhysicalBarHBorder,src_PhysicalBarVBorder,src_PhysicalMenuHBorder,src_PhysicalMenuVBorder;
 UWORD src_LogicalBarHBorder,src_LogicalBarVBorder,src_LogicalMenuHBorder,src_LogicalMenuVBorder;
 struct TTextAttr src_BaseFont; struct DrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo; };
struct Screen { LONG MouseX,MouseY; int LayerInfo; LONG Width,Height; Object *MenuDecorObj; const struct ScreenRenderContext *context; };
struct Window { struct Screen *WScreen; struct Menu *MenuStrip; const struct ScreenRenderContext *MenuRenderContext,*expected;
 BOOL ScaleAware,MenuRenderLogical; struct Image *Checkmark,*AmigaKey,*SubMenuImage; WORD LeftEdge,TopEdge; };
struct mdpGetMenuSpaces { ULONG MethodID; BOOL color; LONG fields[10]; };
struct mdpQueryCapabilities { ULONG MethodID; };
struct mdpGetScaleSpaces { struct mdpGetMenuSpaces base; const struct ScreenRenderContext *context; BOOL logical; };
typedef void *Msg;
static struct IClass separator;
static int calls, fail_at, live, rastports, typed = 1, draws;
static int iblock, lilock;
struct Layer { struct Window *Window; }; static struct Layer hit;
static ULONG LockIBase(ULONG flags) { assert(!flags&&!iblock&&!lilock);iblock=1;return 1; }
static void UnlockIBase(ULONG token) { assert(token==1&&iblock&&!lilock);iblock=0; }
static void ObtainSemaphore(void *s) { (void)s;assert(!iblock&&!lilock);iblock=1; }
static void ReleaseSemaphore(void *s) { (void)s;assert(iblock&&!lilock);iblock=0; }
static void LockLayerInfo(int *li) { (void)li;assert(iblock&&!lilock);lilock=1; }
static void UnlockLayerInfo(int *li) { (void)li;assert(iblock&&lilock);lilock=0; }
static const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *s) { assert(iblock&&lilock);return s->context; }
static struct Layer *WhichLayer(int *li,LONG x,LONG y) { (void)li;assert(iblock&&lilock);return x==221&&y==141?&hit:NULL; }

static APTR AllocVec(ULONG bytes, ULONG flags) { assert(!iblock&&!lilock); (void)flags; if (++calls == fail_at) return NULL;
 void *p = calloc(1,bytes); if(p) live++; return p; }
static void FreeVec(APTR p) { if (p) { live--; free(p); } }
static struct IClass *FindClass(const char *name) { assert(!strcmp(name,MENUBARLABELCLASS)); return &separator; }
static IPTR DoMethodA(Object *o, Msg m) { assert(!iblock&&!lilock); (void)o; assert(((struct mdpQueryCapabilities *)m)->MethodID == MDM_QUERYCAPABILITIES); return typed; }
static BOOL __attribute__((unused)) GetAttr(ULONG attr,Object *o,IPTR *value) { assert(attr==SA_RenderContext); *value=(IPTR)((struct Screen*)o)->context; return TRUE; }
static const struct ScreenRenderContext *WindowBoundRenderContext(struct Window *w) { return w->expected; }
static struct TagItem *NextTagItem(struct TagItem **at) { if (!*at || !(*at)->ti_Tag) return NULL; return (*at)++; }
static void ScreenFontTextAttr(struct TextFont *f,struct TTextAttr *a) { a->ta_Name=f->tf_Message.mn_Node.ln_Name;
 a->ta_YSize=f->tf_YSize; a->ta_Style=f->tf_Style; a->ta_Flags=f->tf_Flags; a->tta_Tags=f->tf_Extension?f->tf_Extension->tfe_Tags:NULL; }
static void InitRastPort(struct RastPort *rp) { rp->initialized=1; rastports++; }
static void DeinitRastPort(struct RastPort *rp) { assert(rp->initialized); rp->initialized=0; rastports--; }
static void SetFont(struct RastPort *rp,struct TextFont *font) { rp->Font=font; }
static LONG TextLength(struct RastPort *rp,const char *s,ULONG length) { assert(s||!length); return (LONG)length*rp->Font->tf_XSize; }
static void SetAPen(struct RastPort *r, ULONG p) { (void)r;(void)p; }
static void RectFill(struct RastPort *r,LONG x,LONG y,LONG right,LONG bottom) { (void)r;assert(right>=x && bottom>=y);draws++; }
'''

TEST = r'''
static void assert_clean(void) { assert(live==0); assert(rastports==0); }
static void exercise(void)
{
 struct TagItem tags[]={{10,0x00480048},{0,0}}, clonedtags[]={{10,0x00480048},{0,0}};
 struct TextFontExtension ext={tags};
 struct TextFont base={.tf_Message.mn_Node.ln_Name="base.font",.tf_YSize=13,.tf_XSize=7,.tf_Style=FSF_TAGGED,.tf_Extension=&ext};
 struct TextFont physical=base;
 struct Image check={.Width=10,.Height=13}, key={.Width=14,.Height=14}, arrow={.Width=7,.Height=13};
 struct IntuitionCustomize custom={&arrow};UWORD pens[8]={0};
 struct IntDrawInfo logical={.dri_Font=&base,.dri_CheckMark=&check,.dri_AmigaKey=&key,.dri_Pens=pens,.dri_Customize=&custom}, scaled=logical;
 struct ScreenRenderContext c={.src_Scale=100,.src_LegacyScaleFactor=1,.src_PhysicalBarHeight=17,
 .src_LogicalBarVBorder=1,.src_LogicalMenuHBorder=2,.src_LogicalMenuVBorder=1,.src_PhysicalMenuHBorder=2,.src_PhysicalMenuVBorder=1,
 .src_LogicalDrawInfo=(struct DrawInfo*)&logical,.src_PhysicalDrawInfo=(struct DrawInfo*)&scaled};
 struct ScreenRenderContext old=c;struct Screen screen={.Width=2560,.Height=1600,.context=&c}, other={.Width=1920,.Height=1080,.context=&old};
 logical.dri_Screen=scaled.dri_Screen=&screen;
 struct TTextAttr attr={"base.font",13,FSF_TAGGED,0,clonedtags};
 struct IntuiText subtext={.IText="child"},t1={.LeftEdge=2,.TopEdge=1,.ITextFont=(struct TextAttr*)&attr,.IText="long first row"},t2={.LeftEdge=2,.TopEdge=1,.IText="second"};
 struct MenuItem sub={.Width=80,.Height=16,.Flags=ITEMTEXT|ITEMENABLED,.ItemFill=&subtext,.NextSelect=65535};
 struct MenuItem second={.TopEdge=16,.Width=80,.Height=16,.Flags=ITEMTEXT|ITEMENABLED|CHECKIT|CHECKED,.ItemFill=&t2,.NextSelect=65535};
 struct MenuItem first={.NextItem=&second,.Width=80,.Height=16,.Flags=ITEMTEXT|ITEMENABLED|CHECKIT|COMMSEQ,.ItemFill=&t1,.Command='A',.SubItem=&sub,.NextSelect=65535};
 struct Menu empty={.LeftEdge=40,.Width=30,.Height=13,.MenuName="Empty",.Flags=MENUENABLED};
 struct Menu menu={.NextMenu=&empty,.Width=35,.Height=13,.MenuName="File",.FirstItem=&first,.Flags=MENUENABLED};
 struct Window owner={.MenuRenderLogical=TRUE,.WScreen=&screen,.MenuStrip=&menu,.Checkmark=&check,.AmigaKey=&key,.SubMenuImage=&arrow};
 struct IntuitionBase ib={0};struct Menu originalmenu=menu;struct MenuItem originalfirst=first, originalsecond=second;
 struct MenuHandlerData h={0}; struct Window bar={0}, pop={.LeftEdge=110,.TopEdge=70}, subpop={.LeftEdge=400,.TopEdge=100};
 int scales[]={100,125,150,200,300,400,100};
 for (unsigned k=0;k<sizeof(scales)/sizeof(scales[0]);k++)
 {
  c.src_Scale=scales[k]; c.src_LegacyScaleFactor=(scales[k]+50)/100;
  c.src_PhysicalBarHeight=(17*scales[k]+50)/100;
  c.src_PhysicalMenuHBorder=(2*scales[k]+50)/100;c.src_PhysicalMenuVBorder=(scales[k]+50)/100;
  physical.tf_YSize=(13*scales[k]+50)/100;physical.tf_XSize=(7*scales[k]+50)/100;scaled.dri_Font=&physical;
  owner.expected=&old;owner.MenuRenderContext=CaptureMenuScaleContext(&owner,&ib);assert(owner.MenuRenderContext==&old);owner.expected=NULL;
  struct MenuScaleGeneration *g=PrepareMenuScale(&owner,&ib);assert(g);
  const struct MenuScaleView *v=MenuScaleView(g);assert(v->sharp&&v->factor==1&&v->scale==scales[k]);
  assert(v->menu!=&menu&&v->menu->FirstItem!=&first);
  struct MenuItem *a=v->menu->FirstItem,*b=a->NextItem;
  assert(a->Height>=physical.tf_YSize); assert(a->Width >= physical.tf_XSize*14);
  assert(b->TopEdge>=a->TopEdge+a->Height);
  assert(v->menu->NextMenu->LeftEdge>=v->menu->LeftEdge+v->menu->Width);
  struct IntuiText *it=a->ItemFill;assert(it->ITextFont==(struct TextAttr*)&g->font);
  assert(((struct TTextAttr*)it->ITextFont)->tta_Tags==tags);
  assert(!memcmp(&menu,&originalmenu,sizeof(menu))&&!memcmp(&first,&originalfirst,sizeof(first)));
  h.view=v;h.scr=&screen;screen.MouseX=221;screen.MouseY=141;hit.Window=&pop;
  assert(MenuScaleWindowAtPointer(&h,&ib)==&pop&&!h.presentationFailed);
  screen.context=&old;assert(MenuScaleWindowAtPointer(&h,&ib)==NULL&&h.presentationFailed);
  screen.context=&c;h.presentationFailed=FALSE;
  struct TagItem popup[]={{WA_Left,3000},{WA_Top,2000},{WA_Width,400},{WA_Height,180},{TAG_DONE,0}};
  assert(FitMenuScaleWindow(&h,popup)&&popup[0].ti_Data==2160&&popup[1].ti_Data==1420);
  popup[0].ti_Data=(IPTR)-5;popup[1].ti_Data=(IPTR)-9;
  assert(FitMenuScaleWindow(&h,popup)&&popup[0].ti_Data==0&&popup[1].ti_Data==0);
  popup[2].ti_Data=UINT64_C(0x100000001);assert(!FitMenuScaleWindow(&h,popup)&&h.presentationFailed);h.presentationFailed=FALSE;
  h.menu=v->menu;h.activemenu=v->menu;h.activeitem=a;h.menubarwin=&bar;h.menuwin=&pop;h.submenuwin=&subpop;
  h.menuinnerleft=3;h.menuinnertop=2;
  h.scrmousex=pop.LeftEdge+a->LeftEdge-v->menu->JazzX+h.menuinnerleft;
  h.scrmousey=pop.TopEdge+a->TopEdge-v->menu->JazzY+h.menuinnertop;
  WORD index=-1;assert(FindItem(&index,&h)==a&&index==0);
  h.submenubox.MinX=-2;h.submenubox.MinY=-1;
  h.scrmousex=subpop.LeftEdge+a->SubItem->LeftEdge-h.submenubox.MinX+h.menuinnerleft;
  h.scrmousey=subpop.TopEdge+a->SubItem->TopEdge-h.submenubox.MinY+h.menuinnertop;
  assert(FindSubItem(&index,&h)==a->SubItem&&index==0);
  h.scrmousex=v->menu->LeftEdge;h.scrmousey=0;assert(FindMenu(&index,&h,&ib)==v->menu&&index==0);
  a->Flags|=CHECKED;b->Flags&=~CHECKED;a->NextSelect=123;
  CommitMenuScaleState(g);assert(first.Flags&CHECKED);assert(!(second.Flags&CHECKED));assert(first.NextSelect==123);
  first=originalfirst;second=originalsecond;
  DisposeMenuScale(g);assert_clean();assert(other.context==&old&&other.Width==1920);
  // Unknown per-client font makes the whole popup legacy, not mixed-size text.
  struct TextAttr foreign={"foreign.font",11,0,0};t1.ITextFont=&foreign;owner.MenuRenderContext=&old;
  g=PrepareMenuScale(&owner,&ib);assert(g);v=MenuScaleView(g);assert(!v->sharp&&v->logical&&v->factor==c.src_LegacyScaleFactor);
  h.view=v; LONG title_y=actual_title_y(&h,v->dri->dri_Font);
  assert(title_y>=0&&title_y+v->dri->dri_Font->tf_YSize<=v->barheight+1);
  assert(v->menu->FirstItem->Width==first.Width&&v->menu->FirstItem->Height==first.Height);
  h.scr=&screen;h.view=v;assert(MenuScaleWindowAtPointer(&h,&ib)==&pop);
  struct TagItem edge[]={{WA_Left,3000},{WA_Top,2000},{WA_Width,200},{WA_Height,100},{TAG_DONE,0}};
  assert(FitMenuScaleWindow(&h,edge)&&edge[0].ti_Data==(IPTR)(v->width-200)&&edge[1].ti_Data==(IPTR)(v->height-100));
  assert(v->width==screen.Width/v->factor);assert(MenuScaleMouse(v,221)==221/v->factor);
  assert(MenuScaleMouse(v,-1)==-1);assert(MenuScaleMouse(v,-v->factor-1)==-2);
  h.view=v;h.activemenu=v->menu;h.menuwin=&pop;h.submenuwin=NULL;h.menuinnerleft=h.menuinnertop=0;
  h.scrmousex=MenuScaleMouse(v,(pop.LeftEdge+v->menu->FirstItem->LeftEdge-v->menu->JazzX)*v->factor);
  h.scrmousey=MenuScaleMouse(v,(pop.TopEdge+v->menu->FirstItem->TopEdge-v->menu->JazzY)*v->factor);
  assert(FindItem(&index,&h)==v->menu->FirstItem);
  DisposeMenuScale(g);assert_clean();owner.ScaleAware=TRUE;owner.MenuRenderLogical=FALSE;assert(!PrepareMenuScale(&owner,&ib));owner.ScaleAware=FALSE;owner.MenuRenderLogical=TRUE;
  t1.ITextFont=(struct TextAttr*)&attr;
 }
 // Explicit logical menu source stays logical even on an aware owner.
 struct TextAttr borrowedfont={"foreign.font",11,0,0};
 t1.ITextFont=&borrowedfont;owner.ScaleAware=TRUE;owner.MenuRenderLogical=TRUE;
 c.src_Scale=200;c.src_LegacyScaleFactor=2;owner.MenuRenderContext=&old;
 struct MenuScaleGeneration *borrowed=PrepareMenuScale(&owner,&ib);
 assert(borrowed&&borrowed->view.logical&&borrowed->view.factor==2);
 DisposeMenuScale(borrowed);assert_clean();
 t1.ITextFont=(struct TextAttr*)&attr;owner.ScaleAware=FALSE;
 // Already-aware client geometry uses its installed source generation exactly once.
 c.src_Scale=200;c.src_LegacyScaleFactor=2;physical.tf_YSize=26;physical.tf_XSize=14;
 owner.ScaleAware=TRUE;owner.MenuRenderLogical=FALSE;owner.MenuRenderContext=&c;attr.ta_YSize=26;
 first.Width=second.Width=320;first.Height=second.Height=32;second.TopEdge=32;
 struct MenuScaleGeneration *aware=PrepareMenuScale(&owner,&ib);assert(aware&&aware->view.sharp);
 assert(aware->view.menu->FirstItem->Width==320);DisposeMenuScale(aware);assert_clean();
 owner.ScaleAware=FALSE;owner.MenuRenderLogical=TRUE;owner.MenuRenderContext=&old;attr.ta_YSize=13;first=originalfirst;second=originalsecond;
 // Same-scale typeface generation changes must remeasure, not only rescale rectangles.
 c.src_Scale=100;c.src_LegacyScaleFactor=1;physical.tf_YSize=13;physical.tf_XSize=20;
 struct MenuScaleGeneration *g=PrepareMenuScale(&owner,&ib);assert(g);assert(g->view.menu->FirstItem->Width>=280);
 assert(g->view.menu->NextMenu->LeftEdge>=g->view.menu->LeftEdge+g->view.menu->Width);
 DisposeMenuScale(g);assert_clean();
 // Real explicit installation A is retained while popup destination is B.
 struct TextFont sourcefont=physical,destinationfont=physical;
 sourcefont.tf_YSize=26;sourcefont.tf_XSize=14;attr.ta_YSize=26;
 destinationfont.tf_YSize=39;destinationfont.tf_XSize=21;
 struct IntDrawInfo sourcephysical=scaled,destinationphysical=scaled;
 sourcephysical.dri_Font=&sourcefont;destinationphysical.dri_Font=&destinationfont;
 struct ScreenRenderContext sourcectx=c,destinationctx=c;
 sourcectx.src_Scale=200;sourcectx.src_LegacyScaleFactor=2;sourcectx.src_PhysicalBarHeight=34;
 sourcectx.src_PhysicalDrawInfo=(struct DrawInfo*)&sourcephysical;
 destinationctx.src_PhysicalDrawInfo=(struct DrawInfo*)&destinationphysical;
 destinationctx.src_Scale=300;destinationctx.src_LegacyScaleFactor=3;destinationctx.src_PhysicalBarHeight=51;
 struct MenuItem candidateitem=first;candidateitem.NextItem=candidateitem.SubItem=NULL;
 candidateitem.LeftEdge=20;candidateitem.Width=320;candidateitem.Height=32;
 struct Menu candidate=menu;candidate.NextMenu=NULL;candidate.FirstItem=&candidateitem;
 struct Window installed=owner;installed.MenuStrip=NULL;installed.MenuRenderContext=&old;installed.expected=&old;
 screen.context=&destinationctx;
 assert(SetMenuStripContext_fixture(&installed,&candidate,&sourcectx,FALSE,&ib));
 assert(installed.MenuRenderContext==&sourcectx&&!installed.MenuRenderLogical&&installed.expected==&old);
 struct MenuScaleGeneration *raced=PrepareMenuScale(&installed,&ib);
 assert(raced&&raced->sourceScale==200&&raced->view.context==&destinationctx&&raced->view.sharp);
 assert(raced->view.menu->FirstItem->LeftEdge==30&&raced->view.dri->dri_Font->tf_YSize==39);
 DisposeMenuScale(raced);assert_clean();screen.context=&c;attr.ta_YSize=13;
 // Every allocation failure keeps client nodes intact and releases all prior nodes.
 calls=0;fail_at=0;g=PrepareMenuScale(&owner,&ib);assert(g);int count=calls;DisposeMenuScale(g);assert_clean();
 for(int fail=1;fail<=count;fail++){calls=0;fail_at=fail;assert(!PrepareMenuScale(&owner,&ib));assert_clean();assert(!memcmp(&menu,&originalmenu,sizeof menu));assert(!memcmp(&first,&originalfirst,sizeof first));}
 fail_at=0;
 // Explicit default tagged font copies match by tag contents, not pointer equality.
 clonedtags[0].ti_Data++;g=PrepareMenuScale(&owner,&ib);assert(g&&!g->view.sharp);DisposeMenuScale(g);clonedtags[0].ti_Data--;assert_clean();
 // Exact audited separator images qualify; arbitrary bitmap images do not.
 struct Image sep={.class=&separator,.Depth=CUSTOMIMAGEDEPTH,.Width=80,.Height=2};
 first.Flags=ITEMENABLED;first.ItemFill=&sep;first.SelectFill=NULL;first.SubItem=NULL;
 g=PrepareMenuScale(&owner,&ib);assert(g&&g->view.sharp);struct RastPort rp={0};
 assert(MenuScaleDrawSeparator(g,g->view.menu->FirstItem,&rp,0,0,&ib));assert(draws==2);DisposeMenuScale(g);assert_clean();
 sep.Depth=1;g=PrepareMenuScale(&owner,&ib);assert(g&&!g->view.sharp);DisposeMenuScale(g);assert_clean();
 first=originalfirst;typed=0;g=PrepareMenuScale(&owner,&ib);assert(g&&!g->view.sharp);DisposeMenuScale(g);typed=1;assert_clean();
 first.NextItem=&first;assert(!PrepareMenuScale(&owner,&ib));first=originalfirst;assert_clean();
 printf("PASS actual menu cloning/layout/hit/selection; %d allocation failures; 7 scale phases; second screen; tagged/default/custom fonts; same-scale face; separator; cycle\n",count);
}
int main(void){exercise();return 0;}
'''

def build_source():
    source=(ROOT/'rom/intuition/menuscale.c').read_text()
    source=re.sub(r'^#include[^\n]*\n','',source,flags=re.M)
    header=(ROOT/'rom/intuition/menuscale.h').read_text()
    header=re.sub(r'^#(?:include|ifn?def|endif|define)[^\n]*\n','',header,flags=re.M)
    menu=(ROOT/'rom/intuition/menutask.c').read_text()
    model=r'''
struct MenuHandlerData { struct Screen *scr; BOOL presentationFailed; const struct MenuScaleView *view; struct Menu *menu,*activemenu;
 struct MenuItem *activeitem; struct Window *menubarwin,*menuwin,*submenuwin;
 WORD scrmousex,scrmousey,innerleft,innertop,menubaritemwidth,menubaritemheight,nummenubaritems;
 WORD menuinnerleft,menuinnertop; struct Rectangle submenubox; };
#define MENUS_UNDERMOUSE(i) 0
'''
    explicit=(ROOT/'rom/intuition/setmenustripcontext.c').read_text()
    classic=(ROOT/'rom/intuition/setmenustrip.c').read_text()
    install='\n#define HASSUBITEM 0x8000\n#define SANITY_CHECKR(w,r) if(!(w))return(r);\nstatic BOOL EnsureDefaultMenuHandler(struct IntuitionBase *base){(void)base;assert(!iblock&&!lilock);return TRUE;}\n'+function(explicit,'MenuStripContextDimensions')+function(classic,'Characterize')
    install+='static BOOL SetMenuStripContext_fixture(struct Window *window,struct Menu *menu,const struct ScreenRenderContext *context,BOOL logical,struct IntuitionBase *IntuitionBase){'+explicit.split('    AROS_LIBFUNC_INIT',1)[1].split('    AROS_LIBFUNC_EXIT',1)[0]+'}\n'
    funcs=''.join(function(menu,n) for n in ('MenuScaleWindowAtPointer','FitMenuScaleWindow','FindMenu','FindItem','FindSubItem'))
    # Use the actual normal-bar glyph placement statement, not a mirrored
    # layout equation, to check its final font box against popup rows.
    placement=next(line.strip() for line in function(menu,'RenderMenuTitle').splitlines()
                   if 'y = mhd->view->barvborder' in line)
    title='static LONG actual_title_y(struct MenuHandlerData *mhd,struct TextFont *font){struct RastPort port={.Font=font};struct RastPort *rp=&port;LONG y;'+placement+'return y;}\n'
    return PREAMBLE+header+source+model+install+funcs+title+TEST

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path);args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='menuscale-') as temporary:
        directory=args.output or Path(temporary);directory.mkdir(parents=True,exist_ok=True)
        c=directory/'menuscale.c';exe=directory/'menuscale';c.write_text(build_source())
        command=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',str(c),'-o',str(exe)]
        subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
        mutations = {
            'legacy-title-padding': ('if (generation->view.barheight + 1 < minimum)', 'if (0)'),
            'legacy-input-domain': ('layer = WhichLayer(&mhd->scr->LayerInfo, mhd->scr->MouseX, mhd->scr->MouseY);', 'layer = WhichLayer(&mhd->scr->LayerInfo, mhd->scrmousex, mhd->scrmousey);'),
            'right-edge-fit': ('left->ti_Data = mhd->view->width - width->ti_Data;', 'left->ti_Data = left->ti_Data;'),
            'bottom-edge-fit': ('top->ti_Data = mhd->view->height - height->ti_Data;', 'top->ti_Data = top->ti_Data;'),
            'stale-context': ('CurrentScreenRenderContext(mhd->scr) == mhd->view->context', 'CurrentScreenRenderContext(mhd->scr) != NULL'),
            'double-aware-scale': ('generation->sourceScale = snapshot.logical ? 100 : source->src_Scale;', 'generation->sourceScale = 100;'),
            'fixed-font-width': ('pixels = TextLength(&generation->measure, text->IText, length);', 'pixels = (LONG)length * 7;'),
            'title-overlap': ('left = (LONG)previous->LeftEdge + previous->Width;', 'left = menu->LeftEdge;'),
            'tag-pointer-identity': ('return menu_scale_tags(((const struct TTextAttr *)attr)->tta_Tags,', 'return ((const struct TTextAttr *)attr)->tta_Tags == ((struct TextFontExtension *)font->tf_Extension)->tfe_Tags && menu_scale_tags(((const struct TTextAttr *)attr)->tta_Tags,'),
            'check-state-loss': ('(item->copy.Flags & CHECKED)', '(item->original->Flags & CHECKED)'),
            'legacy-integer-factor': ('context->src_LegacyScaleFactor : 1;', '1 : 1;'),
        }
        original=build_source()
        results=[]
        for label,(old,new) in mutations.items():
            assert old in original,label
            c.write_text(original.replace(old,new,1))
            subprocess.run(command,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            results.append(dict(label=label,returncode=result.returncode,stderr=result.stderr))
            assert result.returncode != 0, 'mutation survived: '+label
        (directory/'mutants.json').write_text(json.dumps(results,indent=2))
        c.write_text(original)
        subprocess.run(command,check=True)
        print('PASS rejected '+str(len(mutations))+' actual-source mutants')
if __name__=='__main__':main()
