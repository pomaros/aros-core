#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# This file is part of AROS, distributed under the AROS Public License 1.1.
"""Execute actual Wanderer rendering helpers under ASan/UBSan.
Host adapters replace Exec/BOOPSI/icon remapping. The pixel sampler is the
actual icon.library scaler; this is not native MUI or M1 acceptance.
"""
from pathlib import Path
import subprocess, tempfile, json, sys
ROOT=Path(__file__).resolve().parents[4]
def read(p): return (ROOT/p).read_text(encoding='latin1')
def function(s,name):
    import re
    m=re.search(r'(?m)^.*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',s)
    assert m,name
    a=m.start(); b=s.index('{',m.start()); depth=1; i=b+1
    while depth:
        if s[i]=='{':depth+=1
        elif s[i]=='}':depth-=1
        i+=1
    return s[a:i]+'\n'
s=read('workbench/system/Wanderer/Classes/iconlist.c')
scale=read('workbench/system/Wanderer/wandererscale.h')
scale='\n'.join(l for l in scale.splitlines() if not l.startswith('#include'))
fields=['IconHorizontalSpacing','IconVerticalSpacing','IconImageSpacing','LabelTextHorizontalPadding','LabelTextVerticalPadding','LabelTextBorderWidth','LabelTextBorderHeight']
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef char*STRPTR;typedef int32_t LONG;typedef uint32_t ULONG;typedef int64_t QUAD;typedef uintptr_t IPTR;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0
#define MEMF_ANY 0
#define BMF_CLEAR 0
#define BMA_DEPTH 1
#define ILE_TYPE_APPICON 99
#define ICONGETA_RemapIcon 1
#define ICONGETA_FailIfUnavailable 2
#define ICONDUPA_DuplicateImageData 3
#define ICONCTRLA_SetScaleBox 4
#define MUIA_IconList_BufferRastport 5
#define PACK_ICON_SCALEBOX(w,h) (((ULONG)(w)<<16)|(h))
#define ICONENTRY_SIZE 16
#define HEADERLINE_EXTRAHEIGHT 4
#define LINE_EXTRAHEIGHT 2
struct Rectangle{LONG MinX,MinY,MaxX,MaxY;};
struct Layer_Info{int unused;};struct Layer{struct Layer_Info*LayerInfo;};
struct BitMap{int display;};struct RastPort{struct BitMap*BitMap;struct Layer*Layer;};
struct TagItem{IPTR ti_Tag,ti_Data;};
struct TextFontExtension{struct TagItem*tfe_Tags;};
struct TextFont{ULONG tf_YSize,tf_Style,tf_Flags;struct{struct{char*ln_Name;}mn_Node;}tf_Message;struct TextFontExtension*tf_Extension;};
struct TextAttr{char*ta_Name;unsigned short ta_YSize;unsigned char ta_Style,ta_Flags;};
struct TTextAttr{char*tta_Name;unsigned short tta_YSize;unsigned char tta_Style,tta_Flags;struct TagItem*tta_Tags;};
struct DiskFontHeader{struct TextFont dfh_TF;};struct Library{int unused;};
#define FSF_TAGGED 128

struct ListViewModeAttribs{ULONG lmva_HeaderHeight,lmva_RowHeight;};
struct DiskObject{int width,height,dense;ULONG *pixels;};
struct IconEntry{struct IconEntry*next;struct DiskObject*ie_DiskObj,*ie_RenderDiskObj;ULONG ie_RenderScale;struct{char*ln_Name;}ie_IconNode;struct{LONG type;char*label;}ie_IconListEntry;
char *ie_TxtBuf_DisplayedLabel,*ie_TxtBuf_SIZE,*ie_TxtBuf_TIME,*ie_TxtBuf_DATE;
ULONG ie_TxtBuf_DisplayedLabelWidth,ie_SplitParts,ie_TxtBuf_SIZEWidth,ie_TxtBuf_TIMEWidth,ie_TxtBuf_DATEWidth,ie_Flags;
ULONG ie_IconWidth,ie_IconHeight,ie_AreaWidth,ie_AreaHeight,ie_GridWidth,ie_GridHeight;};
struct List{struct IconEntry*head;};
struct IconList_DATA{struct List icld_IconList;struct ListViewModeAttribs*icld_LVMAttribs;ULONG icld_RenderScale,icld_ViewWidth,icld_ViewHeight;LONG icld_DrawOffsetX,icld_DrawOffsetY;
struct RastPort *icld_DisplayRastPort,*icld_BufferRastPort;
ULONG icld__Option_IconBorderOverlap,icld__Option_LabelTextMode,icld__Option_LabelTextMultiLine,icld__Option_IconListMode;
ULONG icld_SortFlags,icld_LabelLargestHeight,icld_IconLargestHeight,icld_IconAreaLargestHeight,icld_IconAreaLargestWidth;
struct TextFont *icld_ScaledLabelFont,*icld_ScaledInfoFont,*icld_LabelBaseFont,*icld_InfoBaseFont,*icld_IconLabelFont,*icld_IconInfoFont;
'''
prefix+='\n'.join('ULONG icld_Base%s,icld__Option_%s;'%(f,f) for f in fields)+'\n};\n'
prefix+=r'''
typedef struct Object{struct IconList_DATA*data;ULONG scale;LONG left,top,width,height;LONG delta;}Object;
static unsigned calls,failat,live;static int dense_available,persistent;
static void*alloc(size_t n){if(++calls==failat||(persistent&&failat&&calls>=failat))return NULL;void*p=calloc(1,n);assert(p);++live;return p;}
static void release(void*p){if(p){assert(live);--live;free(p);}}
#define AllocVec(n,f) alloc(n)
#define FreeVec(p) release(p)
#define CopyMem(s,d,n) memcpy(d,s,n)
static int screen;
#define _screen(o) (&screen)
static const int __iconList_DrawIconStateTags=0;
static void GetIconRectangleA(void*r,struct DiskObject*i,void*t,struct Rectangle*b,int tags){(void)r;(void)t;(void)tags;*b=(struct Rectangle){0,0,i->width-1,i->height-1};}
static struct DiskObject*newicon(int w,int h,int dense){struct DiskObject*i=alloc(sizeof*i);if(!i)return NULL;i->width=w;i->height=h;i->dense=dense;return i;}
static struct DiskObject*GetIconTags(const char*n,...){assert(strstr(n,"@2x"));return dense_available?newicon(64,48,1):NULL;}
static struct DiskObject*DupDiskObject(struct DiskObject*b,...){return newicon(b->width,b->height,0);}
static void FreeDiskObject(struct DiskObject*i){if(i){release(i->pixels);release(i);}}
static BOOL IconControl(struct DiskObject*i,int tag,ULONG box,...){assert(tag==4);i->width=box>>16;i->height=box&65535;return TRUE;}
'''
icons=read('workbench/libs/icon/layouticon.c')
prefix+=function(icons,'ScaleLine')+function(icons,'ScaleRect')
prefix+=r'''
static BOOL LayoutIcon(struct DiskObject*i,void*scr,...){(void)scr;ULONG source[64*48];int w=i->dense?64:32,h=i->dense?48:24;for(int y=0;y<h;y++)for(int x=0;x<w;x++)source[y*w+x]=((y+1)<<16)|(x+1);i->pixels=alloc(i->width*i->height*sizeof(ULONG));if(!i->pixels)return FALSE;ScaleRect(i->pixels,source,w,h,i->width,i->height);return TRUE;}
static ULONG GetBitMapAttr(struct BitMap*b,int tag){(void)b;(void)tag;return 32;}
static struct BitMap*AllocBitMap(ULONG w,ULONG h,ULONG d,int f,struct BitMap*b){assert(w&&h&&d==32);(void)f;(void)b;return alloc(sizeof(struct BitMap));}
static void FreeBitMap(struct BitMap*b){assert(!b->display);release(b);}
static struct RastPort*CreateRastPort(void){return alloc(sizeof(struct RastPort));}
static void FreeRastPort(struct RastPort*p){release(p);}
static struct Layer_Info*NewLayerInfo(void){return alloc(sizeof(struct Layer_Info));}
static void DisposeLayerInfo(struct Layer_Info*l){release(l);}
static struct Layer*CreateUpfrontLayer(struct Layer_Info*l,struct BitMap*b,int x,int y,int w,int h,int f,void*bm){(void)b;(void)x;(void)y;(void)w;(void)h;(void)f;(void)bm;struct Layer*p=alloc(sizeof*p);if(p)p->LayerInfo=l;return p;}
static void SetFont(struct RastPort*,struct TextFont*);
static void DeleteLayer(int unused,struct Layer*l){(void)unused;release(l);}
#define SET(o,a,b) ((void)(a),(o)->data->icld_BufferRastPort=(b))
'''
prefix+=r'''
#define MUIA_RenderScale 8
#define MUIA_RenderContext 9
struct DrawInfo{int generation;};
struct ScreenRenderContext{ULONG src_LegacyScaleFactor,src_PhysicalBarHeight;struct DrawInfo*src_LogicalDrawInfo;};
static struct DrawInfo contextDrawInfo={7};
static struct ScreenRenderContext context={1,28,&contextDrawInfo};
static struct ScreenRenderContext*selectedContext=&context;
#define _win(o) (o)
#define ICON_TEXTMODE_DROPSHADOW 1
#define ICON_TEXTMODE_PLAIN 0
#define ICON_LISTMODE_GRID 1
#define ST_USERDIR 2
#define MUIV_IconList_Sort_BySize 1
#define MUIV_IconList_Sort_ByDate 2
#define ICONENTRY_FLAG_TODAY 1
#define ICONTILE_SIZE 1
#define MUIM_IconList_Tile 55
struct IconTileRequest{ULONG operation,width,height;void*bitmap;LONG x,y;};
#define MUIM_IconList_Sort 56
#define ForeachNode(list,node) for((node)=(list)->head;(node);(node)=(node)->next)
struct IClass{int unused;};typedef void*Msg;struct MUIP_Setup{IPTR MethodID;};
struct IconListview_DATA{Object*vert,*horiz;};
#define MUIA_Prop_DeltaFactor 66
#define set(o,a,v) ((void)(a),(o)->delta=(v))
#define INST_DATA(cl,o) ((void)(cl),(void*)(o)->data)
static BOOL superclass_supported=TRUE;
static IPTR DoSuperMethodA(struct IClass*c,Object*o,Msg m){(void)c;(void)o;(void)m;return superclass_supported;}
static IPTR DoMethod(Object*o,IPTR method,...){(void)o;assert(method==55||method==56);return FALSE;}
static void IconList__LabelFunc_CreateLabel(Object*o,struct IconList_DATA*d,struct IconEntry*e);
static ULONG TextLength(struct RastPort*r,const char*t,ULONG n){(void)r;(void)t;return n*8;}
static struct TextFont inherited={.tf_YSize=13};
#define _font(o) (&inherited)
#define MUIA_Window_LeftEdge 10
#define MUIA_Window_TopEdge 11
#define MUIA_Window_Width 12
#define MUIA_Window_Height 13
#define SA_Scale 14
#define IWDFLAG_ISROOT 1
struct Screen{LONG Width,Height,BarHeight;ULONG scale;};
struct IconWindow_DATA{LONG iwd_PointLeft,iwd_PointTop,iwd_PointWidth,iwd_PointHeight,iwd_ObservedLeft,iwd_ObservedTop,iwd_ObservedWidth,iwd_ObservedHeight;ULONG iwd_GeometryScale,iwd_Flags;BOOL iwd_ObservedGeometry;struct Screen*iwd_Screen;};
static IPTR xget(Object*o,int a){switch(a){case 8:return o->scale;case 9:return (IPTR)selectedContext;case 10:return o->left;case 11:return o->top;case 12:return o->width;case 13:return o->height;}abort();}
#define XGET(o,a) xget(o,a)
static void GetAttr(int tag,Object*o,IPTR*out){*out=tag==14?((struct Screen*)o)->scale:xget(o,tag);}
static void setattrs(Object*o,IPTR*tags){for(;tags[0];tags+=2)switch(tags[0]){case 10:o->left=tags[1];break;case 11:o->top=tags[1];break;case 12:o->width=tags[1];break;case 13:o->height=tags[1];break;default:abort();}}
#define SetAttrs(o,...) setattrs(o,(IPTR[]){__VA_ARGS__})
static struct Library library;
static int wrongfont;
static struct Library*OpenLibrary(const char*n,int v){assert(!strcmp(n,"diskfont.library")&&!v);return &library;}
static void CloseLibrary(struct Library*l){assert(l==&library);}
static struct TextFont*OpenFont(struct TextAttr*a){
 struct TTextAttr*t=(void*)a;
 if(t->tta_Style&FSF_TAGGED){assert(t->tta_Tags&&t->tta_Tags[0].ti_Data==72&&t->tta_Tags[1].ti_Tag==0);}
 struct TextFont*f=alloc(sizeof*f);if(f){f->tf_YSize=t->tta_YSize;f->tf_Style=t->tta_Style;}return f;}
static struct TextFont*OpenDiskFont(struct TextAttr*a){struct TextFont*f=OpenFont(a);if(f&&wrongfont)f->tf_YSize++;return f;}
static struct DiskFontHeader*NewScaledDiskFont(struct TextFont*b,struct TextAttr*a){assert(b);return (void*)OpenFont(a);}
static void CloseFont(struct TextFont*f){release(f);}
static void SetFont(struct RastPort*p,struct TextFont*f){assert(p&&f);}
'''
bodies=''.join(function(s,n) for n in ['iconlist_release_render_icon','iconlist_render_icon','iconlist_release_buffer','iconlist_create_buffer','CalcHeight','iconlist_scale_options','iconlist_font_attr','iconlist_own_font','iconlist_scale_font','iconlist_resolve_fonts','IconList_GetIconImageRectangle','IconList_GetIconLabelRectangle','IconList_GetIconAreaRectangle','iconlist_update_font_metrics','IconList__MUIM_ScaleContextSupported'])
bodies+=function(read('workbench/system/Wanderer/Classes/iconlistview.c'),'IconListview__MUIM_Setup')
bodies+=''.join(function(read('workbench/system/Wanderer/iconwindow.c'),n) for n in ['IconWindow_ObserveGeometry','IconWindow_RememberGeometry','IconWindow_ApplyPointGeometry'])
bodies+=function(read('workbench/system/Wanderer/iconwindowbackfill.c'),'ImageBackFill_BackgroundDrawInfo')
bodies+=r'''
static void IconList__LabelFunc_CreateLabel(Object*o,struct IconList_DATA*d,struct IconEntry*e){(void)o;e->ie_TxtBuf_DisplayedLabelWidth=WandererScalePoint(48,d->icld_RenderScale);}
'''
main=r'''
int main(void){
 struct BitMap displaybitmap={1};struct RastPort display={&displaybitmap,NULL};
 struct IconList_DATA data={.icld_RenderScale=100,.icld_ViewWidth=300,.icld_ViewHeight=200,.icld_DisplayRastPort=&display,.icld_BufferRastPort=&display};Object obj={&data};
 struct DiskObject base={32,24,0,NULL};struct IconEntry entry={.ie_DiskObj=&base,.ie_IconNode={"drawer"}};
'''
main+='\n'.join('data.icld_Base%s=%d;'%(f,n+1) for n,f in enumerate(fields))
main+=r'''
 ULONG scales[]={100,150,200,300,400,100};
 entry.ie_IconListEntry.label="Drawer";entry.ie_TxtBuf_DisplayedLabel="Drawer";entry.ie_SplitParts=1;
 data.icld__Option_LabelTextMultiLine=1;data.icld__Option_LabelTextMode=ICON_TEXTMODE_PLAIN;
 for(unsigned phase=0;phase<6;phase++){
  data.icld_RenderScale=scales[phase];iconlist_scale_options(&data);
  assert(data.icld__Option_IconHorizontalSpacing==WandererScalePoint(1,scales[phase]));
  struct TextFont font={WandererScalePoint(13,scales[phase])};struct ListViewModeAttribs list;
  CalcHeight(&list,&font,scales[phase]);assert(list.lmva_RowHeight==(ULONG)(WandererScalePoint(2,scales[phase])+WandererScalePoint(16,scales[phase])));
  struct DiskObject*r=iconlist_render_icon(&obj,&data,&entry);assert(r);assert(r->width==WandererScalePoint(32,scales[phase]));assert(r->height==WandererScalePoint(24,scales[phase]));
  if(scales[phase]!=100){assert(r!=&base);assert(iconlist_render_icon(&obj,&data,&entry)==r);assert(r->pixels[0]==0x10001);assert(r->pixels[r->width*r->height-1]==0x180020);}
  data.icld_IconLabelFont=&font;data.icld_IconInfoFont=&font;data.icld_IconLargestHeight=0;
  entry.ie_TxtBuf_DisplayedLabelWidth=WandererScalePoint(48,scales[phase]);
  struct Rectangle bounds;IconList_GetIconAreaRectangle(&obj,&data,&entry,&bounds);
  assert(entry.ie_AreaWidth==(ULONG)(WandererScalePoint(48,scales[phase])+2*(WandererScalePoint(4,scales[phase])+WandererScalePoint(6,scales[phase]))));
  assert(entry.ie_AreaHeight==(ULONG)(WandererScalePoint(24,scales[phase])+WandererScalePoint(3,scales[phase])+font.tf_YSize+2*(WandererScalePoint(5,scales[phase])+WandererScalePoint(7,scales[phase]))));
  assert(base.width==32&&base.height==24&&!base.pixels);printf("scale=%u image=%dx%d font=%u row=%u spacing=%u area=%ux%u\n",scales[phase],r->width,r->height,font.tf_YSize,list.lmva_RowHeight,data.icld__Option_IconHorizontalSpacing,entry.ie_AreaWidth,entry.ie_AreaHeight);
 }
 iconlist_release_render_icon(&entry);assert(!live);
 struct IconList_DATA second={.icld_RenderScale=100};Object other={&second,100};
 struct IconEntry secondentry={.ie_DiskObj=&base,.ie_IconNode={"drawer"}};
 data.icld_RenderScale=200;assert(iconlist_render_icon(&obj,&data,&entry)!=&base);
 assert(iconlist_render_icon(&other,&second,&secondentry)==&base&&!secondentry.ie_RenderDiskObj);
 iconlist_release_render_icon(&entry);assert(!live);
 dense_available=1;data.icld_RenderScale=200;struct DiskObject*r=iconlist_render_icon(&obj,&data,&entry);assert(r&&r->dense&&r->pixels[r->width*r->height-1]==0x300040);iconlist_release_render_icon(&entry);dense_available=0;
 for(unsigned failure=1;failure<=4;failure++){
  data.icld_RenderScale=200;calls=0;failat=failure;struct DiskObject*r=iconlist_render_icon(&obj,&data,&entry);
  if(r)iconlist_release_render_icon(&entry);assert(!live);failat=0;
 }
 for(unsigned failure=1;failure<=5;failure++){
  calls=0;failat=failure;BOOL ok=iconlist_create_buffer(&obj,&data);
  assert(ok==(failure==5));if(ok)assert(data.icld_BufferRastPort!=&display);
  iconlist_release_buffer(&data);iconlist_release_buffer(&data);assert(data.icld_BufferRastPort==&display&&!live);
 }
 failat=0;
 for(LONG value=-32768;value<=32767;value++)for(ULONG scale=100;scale<=400;scale+=25){
  LONG pixel=WandererScalePoint(value,scale);assert(WandererUnscalePoint(pixel,scale)==value);
 }
 struct TagItem tags[]={{1,72},{0,0}};struct TextFontExtension extension={tags};
 struct TextFont font={.tf_YSize=13,.tf_Style=FSF_TAGGED,.tf_Message={{"test.font"}},.tf_Extension=&extension};
 data.icld_LabelBaseFont=&font;data.icld_InfoBaseFont=&font;
 for(unsigned phase=0;phase<6;phase++){
  obj.scale=scales[phase];wrongfont=phase==2;assert(iconlist_resolve_fonts(&obj,&data));
  assert(data.icld_IconLabelFont->tf_YSize==(ULONG)WandererScalePoint(13,obj.scale));
  assert(data.icld_IconInfoFont->tf_YSize==data.icld_IconLabelFont->tf_YSize);
 }
 assert(!live);wrongfont=0;persistent=1;
 for(unsigned failure=1;failure<=2;failure++){
  calls=0;failat=failure;obj.scale=200;assert(!iconlist_resolve_fonts(&obj,&data));
  assert(data.icld_RenderScale==100&&!live);failat=0;
 }
 font.tf_Style=0;font.tf_Extension=NULL;struct TextFont*owned=iconlist_own_font(&font);assert(owned&&owned->tf_YSize==13);CloseFont(owned);assert(!live);
 struct Screen scr={2560,1600,27,100};struct IconWindow_DATA geometry={.iwd_PointLeft=40,.iwd_PointTop=60,.iwd_PointWidth=300,.iwd_PointHeight=200,.iwd_Screen=&scr};
 for(unsigned phase=0;phase<6;phase++){
  obj.scale=scales[phase];IconWindow_ApplyPointGeometry(&obj,&geometry);
  assert(obj.width==WandererScalePoint(300,obj.scale)&&obj.height==WandererScalePoint(200,obj.scale));
  if(obj.scale==200)obj.width=500; /* native automatic viewport clamp */
  IconWindow_ObserveGeometry(&obj,&geometry);IconWindow_RememberGeometry(&obj,&geometry);
  assert(geometry.iwd_PointWidth==300&&geometry.iwd_PointLeft==40);
 }
 obj.scale=200;IconWindow_ApplyPointGeometry(&obj,&geometry);IconWindow_ObserveGeometry(&obj,&geometry);obj.width=700;obj.left=120;IconWindow_RememberGeometry(&obj,&geometry);assert(geometry.iwd_PointWidth==350&&geometry.iwd_PointLeft==60);
 obj.scale=100;IconWindow_ApplyPointGeometry(&obj,&geometry);assert(obj.width==350&&obj.left==60);
 geometry.iwd_Flags=IWDFLAG_ISROOT;scr.scale=200;context.src_LegacyScaleFactor=2;obj.scale=100;IconWindow_ApplyPointGeometry(&obj,&geometry);assert(obj.width==1280&&obj.top==14&&obj.height==786);
 obj.scale=200;IconWindow_ApplyPointGeometry(&obj,&geometry);assert(obj.width==2560&&obj.top==28&&obj.height==1572);
 /* Live metadata can belong to another publication; retain Setup snapshot. */
 scr.BarHeight=93;scr.scale=400;obj.scale=100;
 assert(IconWindow_ApplyPointGeometry(&obj,&geometry));assert(obj.width==1280&&obj.top==14);
 assert(ImageBackFill_BackgroundDrawInfo(&obj)==&contextDrawInfo);
 selectedContext=NULL;assert(!IconWindow_ApplyPointGeometry(&obj,&geometry));assert(!ImageBackFill_BackgroundDrawInfo(&obj));selectedContext=&context;

 struct IClass cl={0};data.icld_IconList.head=&entry;entry.next=NULL;
 assert(IconList__MUIM_ScaleContextSupported(&cl,&obj,NULL));entry.ie_IconListEntry.type=ILE_TYPE_APPICON;
 assert(!IconList__MUIM_ScaleContextSupported(&cl,&obj,NULL));entry.ie_IconListEntry.type=0;superclass_supported=FALSE;
 assert(!IconList__MUIM_ScaleContextSupported(&cl,&obj,NULL));superclass_supported=TRUE;
 data.icld__Option_IconListMode=ICON_LISTMODE_GRID;
 for(unsigned phase=0;phase<6;phase++){
  data.icld_RenderScale=scales[phase];font.tf_YSize=WandererScalePoint(13,scales[phase]);data.icld_IconLabelFont=data.icld_IconInfoFont=&font;
  iconlist_scale_options(&data);iconlist_update_font_metrics(&obj,&data);
  assert(data.icld_IconLargestHeight==(ULONG)WandererScalePoint(24,scales[phase]));
  assert(entry.ie_AreaHeight==(ULONG)(WandererScalePoint(24,scales[phase])+WandererScalePoint(3,scales[phase])+font.tf_YSize+2*(WandererScalePoint(5,scales[phase])+WandererScalePoint(7,scales[phase]))));
 }
 iconlist_release_render_icon(&entry);assert(!live);
 Object vertical={0},horizontal={0};struct IconListview_DATA vd={&vertical,&horizontal};Object view={.data=(void*)&vd};struct MUIP_Setup setup={0};
 for(unsigned phase=0;phase<6;phase++){view.scale=scales[phase];assert(IconListview__MUIM_Setup(&cl,&view,&setup));assert(vertical.delta==WandererScalePoint(20,view.scale)&&horizontal.delta==vertical.delta);}
 superclass_supported=FALSE;view.scale=400;assert(!IconListview__MUIM_Setup(&cl,&view,&setup)&&vertical.delta==20);superclass_supported=TRUE;
 puts("PASS actual helpers: immutable metadata, 1x/2x pixels, reset, allocator failures, screen bitmap ownership, signed point roundtrips");return 0;
}
'''
mutations={
    'stale-grid-maximum':('data->icld_IconLargestHeight = 0;','/* stale previous generation retained */'),
    'foreign-appicon-qualified':('if (entry->ie_IconListEntry.type == ILE_TYPE_APPICON) return FALSE;', 'if (entry->ie_IconListEntry.type == ILE_TYPE_APPICON) return TRUE;'),
    'unscaled-image':('width = WandererScalePoint(base.MaxX - base.MinX + 1, scale);','width = base.MaxX - base.MinX + 1;'),
    'compounded-spacing':('WandererScalePoint(data->icld_BaseIconHorizontalSpacing, data->icld_RenderScale)','WandererScalePoint(data->icld__Option_IconHorizontalSpacing, data->icld_RenderScale)'),
    'free-display-bitmap':('buffer && buffer != data->icld_DisplayRastPort','buffer'),
    'stale-font-size':('ta.tta_YSize = WandererScalePoint(base->tf_YSize, scale);','ta.tta_YSize = base->tf_YSize;'),
    'live-barheight-mix':('context->src_PhysicalBarHeight + factor - 1','data->iwd_Screen->BarHeight + 1 + factor - 1'),
    'copied-legacy-factor':('factor = context->src_LegacyScaleFactor;','factor = (data->iwd_Screen->scale + 50) / 100;'),
    'lost-context-pens':('return context ? context->src_LogicalDrawInfo : NULL;','return NULL;'),
}
with tempfile.TemporaryDirectory(prefix='wandererscale-') as d:
    c=Path(d)/'fixture.c';exe=Path(d)/'fixture'
    production=prefix+scale+'\n'+bodies+main
    cases=[('production',production)]
    if '--negative-controls' in sys.argv:
        for name,(old,new) in mutations.items():
            assert production.count(old)==1,(name,production.count(old))
            cases.append((name,production.replace(old,new)))
    for name,source in cases:
        c.write_text(source)
        subprocess.run(['clang','-std=c11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':
            print(result.stdout,end='');assert result.returncode==0,result.stderr
        else:
            assert result.returncode!=0,name+' escaped';print('REJECTED',name)
