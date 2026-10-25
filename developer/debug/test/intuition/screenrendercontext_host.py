#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual immutable screen-context lifecycle with font/image/theme adapters.
No native drawing, full Screen constructor or SMP qualification is implied.
"""
from pathlib import Path
import argparse,re,subprocess
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def strip(s):return re.sub(r'^#include[^\n]*\n','',s,flags=re.M)
public=(ROOT/'compiler/include/intuition/screens.h').read_text(encoding='latin1')
context=re.search(r'struct ScreenRenderContext\s*\{.*?\};',public,re.S).group(0)
metrics=re.search(r'struct ScreenScaleMetrics\s*\{.*?\};',(ROOT/'compiler/include/intuition/scrdecorclass.h').read_text(),re.S).group(0)
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef int BOOL;typedef int32_t LONG;typedef uint32_t ULONG;typedef uint16_t UWORD;typedef uint8_t UBYTE;typedef int64_t QUAD;typedef uintptr_t IPTR;typedef void*APTR;typedef const char*CONST_STRPTR;typedef void*Msg;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define NUMDRIPENS 12
#define SCREENQUIET 1
#define SCREENHIRES 2
#define TAG_DONE 0
#define SYSIA_Which 1
#define SYSIA_DrawInfo 2
#define SYSIA_ReferenceFont 3
#define SYSIA_RenderContext 4
#define SYSIA_Logical 5
#define IA_Height 6
#define SYSIA_Size 7
#define SYSISIZE_MEDRES 1
#define SYSISIZE_LOWRES 0
#define MENUCHECK 1
#define AMIGAKEY 2
#define SUBMENUIMAGE 3
#define SDEPTHIMAGE 4
#define SDM_PREPARESCALE 8
#define SDM_QUERYCAPABILITIES 9
#define SDM_CAP_PREPARESCALE 1
struct TagItem{IPTR ti_Tag,ti_Data;};
struct TTextAttr{char*tta_Name;UWORD tta_YSize;UBYTE tta_Style,tta_Flags;struct TagItem*tta_Tags;};
struct TextFont{UWORD tf_YSize;unsigned refs;struct TTextAttr attr;};
struct Image;typedef struct Image Object;
struct Screen {int Flags;LONG BarHeight,BarVBorder,BarHBorder,MenuVBorder,MenuHBorder,WBorTop,WBorLeft,WBorRight,WBorBottom;};
struct IntuitionCustomize{struct Image*submenu;};
struct IntDrawInfo{UWORD dri_Version,dri_Depth;ULONG dri_Flags;struct {UWORD X,Y;}dri_Resolution;UWORD dri_NumPens;UWORD*dri_Pens;struct TextFont*dri_Font;struct Image*dri_CheckMark,*dri_AmigaKey;struct IntuitionCustomize*dri_Customize;void*dri_Colors;struct Screen*dri_Screen;};
#define DrawInfo IntDrawInfo
'''
prefix+=context+';\n'+metrics+';\n'
prefix+=r'''
struct ScreenRenderGeneration;
struct IntScreen {struct Screen Screen;struct IntDrawInfo DInfo;UWORD Scale;struct TextFont*ScaleBaseFont;Object*ScrDecorObj,*WinDecorObj,*MenuDecorObj;APTR Decorator;IPTR DecorUserBuffer;struct ScreenRenderGeneration*RenderContext,*RenderGenerations;};
struct Image{LONG Width,Height;const struct ScreenRenderContext*context;};
struct Library{int unused;};struct GfxBase{int unused;};struct IntuitionBase{struct Library*UtilityBase;struct GfxBase*GfxBase;};
#define GetPrivIBase(b) (b)
#define IS(s) ((struct IntScreen*)(s))
struct sdpPrepareScale {ULONG MethodID;struct Screen*sdp_Screen;struct TextFont*sdp_Font,*sdp_BaseFont;ULONG sdp_Scale;IPTR sdp_UserBuffer;struct ScreenScaleMetrics*sdp_Metrics;};
static unsigned point,failat,live,hardtrap,images;static BOOL hidd=TRUE,capable=TRUE;
#define WDA_PreparedScaleWindow 12345
static BOOL GetAttr(ULONG a,Object*provider,IPTR*value){assert(!hardtrap&&a==WDA_PreparedScaleWindow);*value=provider==(Object*)2;return TRUE;}
static BOOL gate(void){assert(!hardtrap);return ++point!=failat;}
static void*AllocVec(size_t n,ULONG flags){if(!gate())return NULL;void*p=malloc(n);assert(p);if(flags&MEMF_CLEAR)memset(p,0,n);live++;return p;}
static void FreeVec(void*p){assert(!hardtrap);if(p){assert(live);live--;free(p);}}
static struct TagItem*clone_tags(struct TagItem*t){unsigned n=0;while(t[n].ti_Tag)n++;struct TagItem*c=AllocVec((n+1)*sizeof(*c),0);if(c)memcpy(c,t,(n+1)*sizeof(*c));return c;}
#define CloneTagItems(t) ((void)UtilityBase,clone_tags(t))
#define FreeTagItems(t) ((void)UtilityBase,FreeVec(t))
static void close_font(struct TextFont*f){assert(!hardtrap&&f&&f->refs);f->refs--;}
#define CloseFont(f) ((void)GfxBase,close_font(f))
static struct TextFont*SafeReopenFont(struct IntuitionBase*b,struct TextFont**p){(void)b;if(!gate())return NULL;(*p)->refs++;return *p;}
static void ScreenFontTextAttr(struct TextFont*f,struct TTextAttr*a){*a=f->attr;}
static UWORD ScreenLegacyScaleFactorForScale(struct Screen*s,ULONG scale){(void)s;if(!hidd)return 1;ULONG v=(scale+50)/100;return v<1?1:v>4?4:v;}
static IPTR theme_prepare(Object*o,Msg raw){(void)o;if(*(ULONG*)raw==SDM_QUERYCAPABILITIES)return capable?SDM_CAP_PREPARESCALE:0;struct sdpPrepareScale*m=raw;assert(m->sdp_Scale==100&&m->sdp_Font==m->sdp_BaseFont);return gate();}
#define DoMethodA(o,m) ((void)IntuitionBase,theme_prepare(o,m))
static Object*new_image(struct TagItem*t){
 const struct ScreenRenderContext*c=NULL;struct IntDrawInfo*di=NULL;struct TextFont*font=NULL;BOOL logical=FALSE;LONG height=0;unsigned which=0,size=0;
 for(;t->ti_Tag;t++)switch(t->ti_Tag){case SYSIA_RenderContext:c=(void*)t->ti_Data;break;case SYSIA_DrawInfo:di=(void*)t->ti_Data;break;case SYSIA_ReferenceFont:font=(void*)t->ti_Data;break;case SYSIA_Logical:logical=t->ti_Data;break;case IA_Height:height=t->ti_Data;break;case SYSIA_Which:which=t->ti_Data;break;case SYSIA_Size:size=t->ti_Data;break;}
 assert(c&&c->src_BaseFont.tta_Name&&c->src_BaseFont.tta_Tags&&c->src_LogicalDrawInfo&&c->src_PhysicalDrawInfo);
 assert(di==(logical?c->src_LogicalDrawInfo:c->src_PhysicalDrawInfo)&&font==di->dri_Font&&font->refs);
 assert(di->dri_Screen&&c->src_LogicalDrawInfo->dri_Screen==di->dri_Screen&&c->src_PhysicalDrawInfo->dri_Screen==di->dri_Screen);
 if(which==SDEPTHIMAGE)assert(!logical&&height==(LONG)c->src_PhysicalBarHeight);
 Object*im=AllocVec(sizeof(*im),MEMF_CLEAR);if(im){im->Width=font->tf_YSize+size;im->Height=height?height:font->tf_YSize;im->context=c;images++;}return im;
}
#define NewObjectA(a,b,t) ((void)IntuitionBase,new_image(t))
static void DisposeObject(Object*o){if(o){assert(!hardtrap&&images);assert(o->context->src_LogicalDrawInfo->dri_Font->refs&&o->context->src_PhysicalDrawInfo->dri_Font->refs);images--;FreeVec(o);}}
'''
source=strip((ROOT/'rom/intuition/screenrendercontext.c').read_text())
tests=r'''
static struct TagItem tags[]={{10,1234},{0,0}};
static char name[]="Example.font";
static struct TextFont fonts[13];static struct IntScreen screen;static UWORD pens[12];static struct IntuitionBase base;
static struct ScreenScaleMetrics metric(unsigned scale){return(struct ScreenScaleMetrics){(LONG)(13*scale/100+2*scale/100),1,5,2,4,1,2,3,4,-1,0};}
static unsigned generation_count(void){unsigned n=0;for(struct ScreenRenderGeneration*g=screen.RenderGenerations;g;g=g->next)n++;return n;}
static void setup(void){assert(!live&&!images);memset(&screen,0,sizeof screen);for(unsigned i=0;i<13;i++)fonts[i]=(struct TextFont){13*(100+i*25)/100,1,{name,13*(100+i*25)/100,128,0,tags}};for(unsigned i=0;i<12;i++)pens[i]=i;
 screen.DInfo=(struct IntDrawInfo){.dri_NumPens=12,.dri_Pens=pens,.dri_Font=&fonts[0],.dri_Screen=&screen.Screen};screen.ScrDecorObj=(void*)1;screen.WinDecorObj=(void*)2;screen.MenuDecorObj=(void*)3;screen.Scale=100;screen.ScaleBaseFont=&fonts[0];screen.Screen=(struct Screen){.BarHeight=15,.BarVBorder=1,.BarHBorder=5,.MenuVBorder=2,.MenuHBorder=4,.WBorTop=1,.WBorLeft=2,.WBorRight=3,.WBorBottom=4};point=failat=0;hidd=capable=TRUE;
}
static struct PreparedScreenRenderContext*prepare(unsigned scale){struct ScreenScaleMetrics m=metric(scale);return PrepareScreenRenderContext(&screen,&fonts[0],&fonts[(scale-100)/25],scale,&m,&base);}
static void publish(unsigned scale){struct PreparedScreenRenderContext*p=prepare(scale);assert(p);hardtrap=1;InstallScreenRenderContext(p,FALSE);hardtrap=0;ReleaseScreenRenderContext(p,TRUE,&base);}
static void run(void){setup();publish(100);const struct ScreenRenderContext*first=CurrentScreenRenderContext(&screen.Screen);struct ScreenRenderGeneration saved=*screen.RenderContext;unsigned retained=live,oldimages=images;
 unsigned refs[13];for(unsigned i=0;i<13;i++)refs[i]=fonts[i].refs;
 struct IntScreen oldscreen=screen;
 point=0;struct PreparedScreenRenderContext*p=prepare(200);assert(p);unsigned attempts=point;ReleaseScreenRenderContext(p,FALSE,&base);
 for(unsigned f=1;f<=attempts;f++){point=0;failat=f;p=prepare(200);assert(!p);failat=0;assert(live==retained&&images==oldimages&&!memcmp(&oldscreen,&screen,sizeof screen)&&!memcmp(&saved,screen.RenderContext,sizeof saved));for(unsigned i=0;i<13;i++)assert(fonts[i].refs==refs[i]);}
 p=prepare(200);assert(p&&PreparedScreenRenderContextView(p)!=first);hardtrap=1;InstallScreenRenderContext(p,FALSE);InstallScreenRenderContext(p,TRUE);hardtrap=0;ReleaseScreenRenderContext(p,FALSE,&base);assert(!memcmp(&oldscreen,&screen,sizeof screen)&&live==retained);
 for(unsigned scale=125;scale<=400;scale+=25)publish(scale);
 assert(generation_count()==13&&images==13*7);unsigned cachedlive=live;
 for(unsigned round=0;round<20;round++)for(unsigned scale=100;scale<=400;scale+=25){publish(scale);assert(live==cachedlive&&generation_count()==13);const struct ScreenRenderContext*c=CurrentScreenRenderContext(&screen.Screen);assert(c->src_Scale==scale&&c->src_LegacyScaleFactor==(scale+50)/100);LONG l,t,r,b;assert(ScreenRenderContextBorders(c,TRUE,&l,&t,&r,&b)&&l==2&&t==1&&r==3&&b==4);assert(ScreenRenderContextBorders(c,FALSE,&l,&t,&r,&b)&&l==(LONG)((2*scale+50)/100)&&t==(LONG)((scale+50)/100));}
 assert(!memcmp(&saved,(struct ScreenRenderGeneration*)first,sizeof saved));
 assert(first->src_BaseFont.tta_Name!=name&&first->src_BaseFont.tta_Tags!=tags&&first->src_BaseFont.tta_Tags[0].ti_Data==1234);
 assert(first->src_LogicalDrawInfo->dri_Pens!=pens);pens[0]=9;publish(100);assert(generation_count()==14&&first->src_LogicalDrawInfo->dri_Pens[0]==0);
 screen.Decorator=(void*)1;publish(100);assert(generation_count()==15);
 const struct ScreenRenderContext*plain=CurrentScreenRenderContext(&screen.Screen);
 Object*olddepth=ScreenRenderContextDepthImage(plain);assert(olddepth);
 screen.Screen.Flags|=SCREENQUIET;publish(100);assert(generation_count()==16&&!ScreenRenderContextDepthImage(CurrentScreenRenderContext(&screen.Screen)));
 screen.Screen.Flags=SCREENHIRES;publish(100);assert(generation_count()==17&&ScreenRenderContextDepthImage(CurrentScreenRenderContext(&screen.Screen))->Width==olddepth->Width+1);
 screen.Screen.Flags=0;publish(100);assert(CurrentScreenRenderContext(&screen.Screen)==plain);
 publish(200);const struct ScreenRenderContext*hiddcontext=CurrentScreenRenderContext(&screen.Screen);assert(hiddcontext->src_LegacyScaleFactor==2);
 unsigned beforebackend=generation_count();hidd=FALSE;publish(200);assert(generation_count()==beforebackend+1&&CurrentScreenRenderContext(&screen.Screen)!=hiddcontext&&CurrentScreenRenderContext(&screen.Screen)->src_LegacyScaleFactor==1);hidd=TRUE;publish(200);assert(CurrentScreenRenderContext(&screen.Screen)==hiddcontext);
 publish(100);unsigned beforemetadata=generation_count();
 screen.DInfo.dri_NumPens=11;publish(100);assert(generation_count()==++beforemetadata&&CurrentScreenRenderContext(&screen.Screen)->src_LogicalDrawInfo->dri_NumPens==11);screen.DInfo.dri_NumPens=12;
 screen.DInfo.dri_Depth=32;publish(100);assert(generation_count()==++beforemetadata&&CurrentScreenRenderContext(&screen.Screen)->src_LogicalDrawInfo->dri_Depth==32);screen.DInfo.dri_Depth=0;
 screen.DInfo.dri_Version=2;publish(100);assert(generation_count()==++beforemetadata&&CurrentScreenRenderContext(&screen.Screen)->src_LogicalDrawInfo->dri_Version==2);screen.DInfo.dri_Version=0;
 screen.DInfo.dri_Flags=1;publish(100);assert(generation_count()==++beforemetadata&&CurrentScreenRenderContext(&screen.Screen)->src_LogicalDrawInfo->dri_Flags==1);screen.DInfo.dri_Flags=0;
 screen.DInfo.dri_Resolution.X=2;publish(100);assert(generation_count()==++beforemetadata&&CurrentScreenRenderContext(&screen.Screen)->src_LogicalDrawInfo->dri_Resolution.X==2);screen.DInfo.dri_Resolution.X=0;
 screen.DInfo.dri_Resolution.Y=3;publish(100);assert(generation_count()==++beforemetadata&&CurrentScreenRenderContext(&screen.Screen)->src_LogicalDrawInfo->dri_Resolution.Y==3);screen.DInfo.dri_Resolution.Y=0;
 publish(100);assert(CurrentScreenRenderContext(&screen.Screen)==plain&&olddepth==ScreenRenderContextDepthImage(plain));
 assert(plain->src_PreparedScaleWindow);
 screen.WinDecorObj=(Object*)4;publish(100);
 assert(!CurrentScreenRenderContext(&screen.Screen)->src_PreparedScaleWindow&&plain->src_PreparedScaleWindow);
 screen.WinDecorObj=(Object*)2;publish(100);assert(CurrentScreenRenderContext(&screen.Screen)==plain);
 struct IntScreen other={0};assert(!CurrentScreenRenderContext(&other.Screen));
 FreeScreenRenderContexts(&screen,&base);assert(!live&&!images&&!screen.RenderContext&&!screen.RenderGenerations);for(unsigned i=0;i<13;i++)assert(fonts[i].refs==1);
 setup();assert(InitializeScreenRenderContext(&screen,&base));assert(CurrentScreenRenderContext(&screen.Screen)->src_PhysicalBarHeight==16);FreeScreenRenderContexts(&screen,&base);assert(!live&&!images&&fonts[0].refs==1);
 setup();capable=FALSE;assert(!prepare(200));screen.Screen.BarHBorder=9;assert(InitializeScreenRenderContext(&screen,&base));const struct ScreenRenderContext*legacy=CurrentScreenRenderContext(&screen.Screen);assert(legacy->src_LogicalBarHBorder==9);assert(!prepare(200));FreeScreenRenderContexts(&screen,&base);assert(!live&&!images);
 setup();point=0;failat=1;assert(!InitializeScreenRenderContext(&screen,&base));failat=0;assert(!live&&!images&&!screen.RenderContext);
 printf("PASS actual screen context: %u preparation faults; 13-scale cache +260 revisits; copied fonts/tags/pens, physical/logical borders, pure publish/rollback, prior generation retention, other screen, constructor and balanced teardown\n",attempts);
}
int main(void){run();return 0;}
'''
mutants={
 'window-capability-forced':source.replace('GetAttr(WDA_PreparedScaleWindow, screen->WinDecorObj, &supported) && supported;', '(GetAttr(WDA_PreparedScaleWindow, screen->WinDecorObj, &supported), TRUE);'),
 'font-ref-lost':source.replace('g->logical.dri_Font = SafeReopenFont(IntuitionBase, &base);','g->logical.dri_Font = base;'),
 'tag-borrowed':source.replace('attr->tta_Tags = CloneTagItems(source.tta_Tags);','(void)UtilityBase; attr->tta_Tags = source.tta_Tags;'),
 'pens-borrowed':source.replace('g->logical.dri_Pens = g->physical.dri_Pens = g->pens;','g->logical.dri_Pens = g->physical.dri_Pens = screen->DInfo.dri_Pens;'),
 'wrong-proposed-factor':source.replace('ScreenLegacyScaleFactorForScale(&screen->Screen, scale)','ScreenLegacyScaleFactorForScale(&screen->Screen, screen->Scale)'),
 'cache-imageflags-ignored':source.replace('g->imageflags == imageflags && ',' '),
 'cache-factor-ignored':source.replace('g->view.src_LegacyScaleFactor == legacyfactor &&',''),
 'cache-numpens-ignored':source.replace('g->logical.dri_NumPens == screen->DInfo.dri_NumPens &&',''),
 'cache-depth-ignored':source.replace('g->logical.dri_Depth == screen->DInfo.dri_Depth &&',''),
 'cache-version-ignored':source.replace('g->logical.dri_Version == screen->DInfo.dri_Version &&',''),
 'cache-flags-ignored':source.replace('g->logical.dri_Flags == screen->DInfo.dri_Flags &&',''),
 'cache-resolution-X-ignored':source.replace('g->logical.dri_Resolution.X == screen->DInfo.dri_Resolution.X &&',''),
 'cache-resolution-Y-ignored':source.replace('g->logical.dri_Resolution.Y == screen->DInfo.dri_Resolution.Y &&',''),
 'unknown-provider-scaling-accepted':source.replace('if (scale != 100 || screen->RenderContext) return FALSE;', 'if (((void)scale, screen->RenderContext)) return FALSE;'),
 'supported-prepare-failure-retried':source.replace('if (!DoMethodA(screen->ScrDecorObj, (Msg)&message)) return FALSE;', 'DoMethodA(screen->ScrDecorObj, (Msg)&message);'),
 'cache-disabled':source.replace('g->view.src_Scale == scale','0 && g->view.src_Scale == scale'),
 'wrong-logical-borders':source.replace('if (!logical)','if (((void)logical, 1))'),
 'rollback-current-lost':source.replace('__atomic_store_n(&p->screen->RenderContext, p->previous, __ATOMIC_RELEASE);','__atomic_store_n(&p->screen->RenderContext, p->generation, __ATOMIC_RELEASE);'),
 'old-generation-freed':source.replace('if (!committed && p->created && !p->linked)','if (((void)committed, p->created))'),
}
for label,code in [('production',source),*mutants.items()]:
 assert label=='production' or code!=source,label
 c=a.output/(label+'.c');exe=a.output/label;c.write_text(prefix+code+tests)
 cmd=['clang','-std=c11','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Wall','-Wextra','-Werror','-Wno-unused-function',str(c),'-o',str(exe)]
 r=subprocess.run(cmd,capture_output=True,text=True);(a.output/(label+'.compile.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 r=subprocess.run([str(exe)],capture_output=True,text=True);(a.output/(label+'.log')).write_text(r.stdout+r.stderr)
 assert (r.returncode==0)==(label=='production'),label+':'+r.stdout+r.stderr
 print(r.stdout if label=='production' else 'REJECT '+label,end='' if label=='production' else '\n')
