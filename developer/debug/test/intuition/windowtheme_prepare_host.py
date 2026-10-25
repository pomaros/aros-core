#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL) Version 1.1.
"""Actual themed WindowData generation builder/config and image ownership.
No layout/render-provider substitutes claim actual target rendering.
"""
import argparse,pathlib,re,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[4]);a=p.parse_args()
s=(a.source/'workbench/c/Decoration/windowdecorclass.c').read_text();theme=(a.source/'workbench/libs/decortheme/decortheme.h').read_text();decor=(a.source/'workbench/libs/decorator/decorator.h').read_text();container=(a.source/'workbench/libs/decorator/decorimagefuncs.c').read_text();wd=(a.source/'workbench/c/Decoration/windowdecorclass.h').read_text();sd=(a.source/'workbench/c/Decoration/screendecorclass.h').read_text()
def block(source,start):
 left=source.index('{',start);depth=1;right=left+1
 while depth:
  depth+=(source[right]=='{')-(source[right]=='}');right+=1
 return source[start:right]
def structure(source,name):
 match=re.search(r'struct\s+'+name+r'\s*\{',source);assert match,name
 return block(source,match.start())+';\n'
def function(source,name):
 start=source.index(name+'(');return block(source,source.index('{',start))
hdr=(a.source/'compiler/include/intuition/windecorclass.h').read_text(encoding='latin1')
refusal=hdr[hdr.index('struct wdpScaleRefusal\n{'):hdr.index('#define WDP_REFUSE_NONE')].replace('STACKED ','')+''.join(l.split('/*')[0].rstrip()+'\n' for l in hdr.splitlines() if re.match(r'#define\s+WDP_REFUSE_\w+\s',l))
macros='\n'.join(x for x in theme.splitlines()+decor.splitlines() if x.startswith('#define DECOR_') or x.startswith('#define DE_TYPE_'))
windowmacro=s[s.index('#define WINDOW_SCALE_IMAGE_LIST'):s.index('\n\nstruct WindowScaleData')]
structs=structure(decor,'DecorImage')+structure(decor,'DecoratorElement')+structure(theme,'DecorConfig')+structure(theme,'DecorImages')+structure(theme,'DecorThemeScreen')
structs+=''.join(structure(wd,n) for n in ['CachedPropGadget','CachedTitleBar','CachedTitleBarShape','WindowData'])+structure(sd,'ScreenData')+structure(s,'windecor_data')+windowmacro+'\n'+structure(s,'WindowScaleData')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
typedef int BOOL;typedef uint32_t ULONG;typedef int32_t LONG;typedef int16_t WORD;typedef uint16_t UWORD;typedef uint8_t UBYTE;typedef uint64_t UQUAD;typedef int64_t QUAD;typedef uintptr_t IPTR;typedef void Object;typedef void*APTR;typedef char*STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_ANY 1
#define MEMF_CLEAR 2
struct TextFont{UWORD tf_YSize;};struct Screen{int unused;};struct DrawInfo{struct TextFont*dri_Font;};struct ScreenRenderContext{UWORD src_Scale;struct DrawInfo*src_LogicalDrawInfo,*src_PhysicalDrawInfo;};typedef struct {int unused;} Class;
static Class *actualClass;static struct windecor_data *instance;
#define OCLASS(obj) actualClass
#define INST_DATA(cl,obj) instance
struct wdpInitWindow{ULONG MethodID;BOOL wdp_TrueColor;IPTR wdp_UserBuffer;struct Screen*wdp_Screen;IPTR wdp_ScreenUserBuffer;};
REFUSAL_RECORD
struct wdpPrepareScaleWindow{struct wdpInitWindow base;const struct ScreenRenderContext*context;BOOL logical;struct TextFont*font;LONG width,height,borderTop;struct wdpScaleRefusal*refusal;};
struct wdpExitWindow{ULONG MethodID;BOOL wdp_TrueColor;IPTR wdp_UserBuffer;};
static unsigned ordinal,failAt,allocations,frees;
static void*AllocVec(size_t bytes,unsigned flags){(void)flags;if(++ordinal==failAt)return NULL;void*p=calloc(1,bytes);assert(p);allocations++;return p;}
static void FreeVec(void*p){if(p){free(p);frees++;}}
static void FreeBitMap(void*p){assert(!p);}
static void DisposeDTObject(void*p){assert(!p);}
static void DisposeRegion(void*p){assert(!p);}
'''
functions='static struct DecorImage *DNewDecorImageContainer(UWORD w,UWORD h)'+function(container,'NewDecorImageContainer')+'\nstatic void DDisposeImageContainer(struct DecorImage*di)'+function(container,'DisposeImageContainer')+'\n'
functions+=(a.source/'workbench/c/Decoration/scaledimage.h').read_text()+'\n'
for name,decl in [('window_scale_image_name','static const char *window_scale_image_name(const char *id)'),('window_scale_refuse','static void window_scale_refuse(struct wdpScaleRefusal *refusal, ULONG code,\n    LONG value0, LONG value1, LONG value2, const char *name)'),('scale_config_pair','static BOOL scale_config_pair(LONG *size, LONG sourceOffset, LONG sourceSize,\n    LONG scaledOffset, ULONG scale, const char *name, struct wdpScaleRefusal *refusal)'),('scale_window_config','static BOOL scale_window_config(struct DecorConfig*out,const struct DecorConfig*source,ULONG scale,struct wdpScaleRefusal*refusal)'),('release_window_scale_data','static void release_window_scale_data(struct WindowScaleData*scaled)'),('windecor_preparescalewindow','static IPTR windecor_preparescalewindow(Class*cl,Object*obj,struct wdpPrepareScaleWindow*msg)'),('windecor_exitwindow','static IPTR windecor_exitwindow(Class*cl,Object*obj,struct wdpExitWindow*msg)')]:functions+=decl+function(s,name)+'\n'
main=r'''
int main(void){Class builtin,unknown;actualClass=&builtin;struct Screen screen={0};struct DecorConfig config={.BarHeight=10,.BarFrame=1,.RightBorderGadgets=8,.BottomBorderGadgets=8,.UpDownAddY=1,.LeftRightAddX=1,.ActivatedGradientColor_s=0x12345678,.BarPre_o=2,.BarPre_s=3};struct DecorImages sourceImages={0};
#define BUILD_SOURCE(id) sourceImages.img_##id=DNewDecorImageContainer(28,10);assert(sourceImages.img_##id);sourceImages.img_##id->ok=TRUE;sourceImages.img_##id->subimagescols=4;
WINDOW_SCALE_IMAGE_LIST(BUILD_SOURCE)
#undef BUILD_SOURCE
DDisposeImageContainer(sourceImages.img_winbar_normal);sourceImages.img_winbar_normal=DNewDecorImageContainer(128,20);assert(sourceImages.img_winbar_normal);sourceImages.img_winbar_normal->ok=TRUE;sourceImages.img_winbar_normal->subimagescols=1;sourceImages.img_winbar_normal->subimagesrows=2;
struct DecorThemeScreen theme={.dts_Screen=&screen,.dts_Images=&sourceImages};theme.dts_Elements[0]=(struct DecoratorElement){.de_Image=sourceImages.img_winbar_normal,.de_Type=DE_TYPE_TILED_TITLE,.de_SubImageCols=1,.de_SubImageRows=2,.de_SrcOffset=2,.de_SrcSize=3};struct ScreenData sd={.dts=&theme,.di=&sourceImages,.ActivePen=4,.DeactivePen=5};struct windecor_data data={.dc=&config};instance=&data;struct DecorConfig originalConfig=config;struct DecorThemeScreen originalTheme=theme;struct DecorImages originalImages=sourceImages;unsigned sourceOwners=allocations-frees,totalFaults=0;for(unsigned scale=100;scale<=400;scale+=25){struct TextFont basefont={8};struct DrawInfo baseDI={&basefont};struct ScreenRenderContext context={scale,&baseDI,&baseDI};struct TextFont font={(8*scale+50)/100};struct WindowData wd={0},zero={0};struct wdpPrepareScaleWindow prepare={.base={.wdp_UserBuffer=(IPTR)&wd,.wdp_ScreenUserBuffer=(IPTR)&sd,.wdp_Screen=&screen,.wdp_TrueColor=TRUE},.context=&context,.font=&font,.width=600,.height=400,.borderTop=font.tf_YSize+(2*scale+50)/100};ordinal=0;failAt=0;assert(windecor_preparescalewindow(&builtin,NULL,&prepare));unsigned maximum=ordinal;assert(maximum==41);assert(wd.scaleData&&wd.dts==&wd.scaleData->theme&&wd.scaleData->config.BarHeight==prepare.borderTop);assert(wd.scaleData->config.ActivatedGradientColor_s==config.ActivatedGradientColor_s);assert(wd.img_winbar_normal->h==prepare.borderTop*2);assert(wd.scaleData->config.BarPre_o==(2*scale+50)/100);assert(wd.scaleData->config.BarPre_s==(5*scale+50)/100-(2*scale+50)/100);struct wdpExitWindow exit={.wdp_UserBuffer=(IPTR)&wd};assert(windecor_exitwindow(&builtin,NULL,&exit));assert(!wd.scaleData);memset(&wd,0,sizeof(wd));assert(allocations-frees==sourceOwners);for(unsigned failure=1;failure<=maximum;failure++){ordinal=0;failAt=failure;assert(!windecor_preparescalewindow(&builtin,NULL,&prepare));failAt=0;assert(!memcmp(&wd,&zero,sizeof(wd)));assert(allocations-frees==sourceOwners);assert(!memcmp(&config,&originalConfig,sizeof(config)));assert(!memcmp(&theme,&originalTheme,sizeof(theme)));assert(!memcmp(&sourceImages,&originalImages,sizeof(sourceImages)));totalFaults++;}actualClass=&unknown;ordinal=0;assert(!windecor_preparescalewindow(&builtin,NULL,&prepare));assert(!ordinal&&!memcmp(&wd,&zero,sizeof(wd)));actualClass=&builtin;}
/* Every refusal names its cause and the THEME: resource. */
{struct TextFont basefont={8};struct DrawInfo baseDI={&basefont};struct ScreenRenderContext context={200,&baseDI,&baseDI};struct TextFont font={16};struct WindowData wd={0};struct wdpScaleRefusal r;struct wdpPrepareScaleWindow prepare={.base={.wdp_UserBuffer=(IPTR)&wd,.wdp_ScreenUserBuffer=(IPTR)&sd,.wdp_Screen=&screen,.wdp_TrueColor=TRUE},.context=&context,.font=&font,.width=600,.height=400,.borderTop=20,.refusal=&r};
#define REFUSED(code,a0,a1,a2,nm) (memset(&r,0,sizeof r),!windecor_preparescalewindow(&builtin,NULL,&prepare)&&r.wsr_Code==(code)&&r.wsr_Value[0]==(a0)&&r.wsr_Value[1]==(a1)&&r.wsr_Value[2]==(a2)&&!strcmp((char*)r.wsr_Name,nm)&&allocations-frees==sourceOwners)
 actualClass=&unknown;assert(REFUSED(WDP_REFUSE_UNSUPPORTED,0,0,0,""));actualClass=&builtin;
 prepare.base.wdp_UserBuffer=0;assert(REFUSED(WDP_REFUSE_STATE,0,0,0,""));prepare.base.wdp_UserBuffer=(IPTR)&wd;
 prepare.width=0;assert(REFUSED(WDP_REFUSE_ARGUMENTS,200,0,400,""));prepare.width=600;
 prepare.font=NULL;assert(REFUSED(WDP_REFUSE_FONT,0,10,0,""));prepare.font=&font;
 ordinal=0;failAt=1;assert(REFUSED(WDP_REFUSE_NO_MEMORY,(LONG)sizeof(struct WindowScaleData),0,0,""));failAt=0;
 config.BarPre_o=20000;assert(REFUSED(WDP_REFUSE_CONFIG,20000,200,0,"BarPre_o"));config.BarPre_o=2;
 config.BarPre_s=-1;assert(REFUSED(WDP_REFUSE_CONFIG,-1,200,0,"BarPre"));config.BarPre_s=3;
 sourceImages.img_close->w=29;assert(REFUSED(WDP_REFUSE_IMAGE_GRID,29,10,401,"System/Close/Default"));sourceImages.img_close->w=28;
 sourceImages.img_winbar_normal->subimagesrows=1;assert(REFUSED(WDP_REFUSE_IMAGE_GRID,128,20,101,"System/Titlebar/Default"));sourceImages.img_winbar_normal->subimagesrows=2;
 ordinal=0;failAt=3;assert(REFUSED(WDP_REFUSE_IMAGE_EXTENT,14,20,0,"System/Size/Default"));failAt=0;
 theme.dts_Elements[0].de_SrcSize=200;assert(REFUSED(WDP_REFUSE_ELEMENT,4,400,256,"System/Titlebar/Default"));theme.dts_Elements[0].de_SrcSize=3;
 memset(&r,0,sizeof r);assert(windecor_preparescalewindow(&builtin,NULL,&prepare)&&!r.wsr_Code);struct wdpExitWindow exit={.wdp_UserBuffer=(IPTR)&wd};windecor_exitwindow(&builtin,NULL,&exit);
 puts("PASS actual Decoration prepare refusals: unsupported class, state, arguments, font, allocation, named config value and pair, image grid, title bar rows, image extent, theme element with THEME: names");}
#define FREE_SOURCE(id) DDisposeImageContainer(sourceImages.img_##id);
WINDOW_SCALE_IMAGE_LIST(FREE_SOURCE)
#undef FREE_SOURCE
assert(allocations==frees);printf("PASS actual themed WindowData/config builder:13scales,%u actual allocation failures, unchanged source/private failure buffers, ownership-balanced successful close, exactclass refusal\n",totalFaults);return 0;}
'''
variants=[('actual',functions),('mutate-source-config',functions.replace('*out = *source;','*out = *source; ((struct DecorConfig*)source)->BarFrame++;')),('lose-private-image',functions.replace('DDisposeImageContainer(scaled->images.img_##id);','(void)scaled->images.img_##id;')),('wrong-bar-height',functions.replace('height = msg->borderTop ? msg->borderTop : 1;','height = 1;')),('allow-inherited',functions.replace('    if (OCLASS(obj) != cl)\n    {\n        window_scale_refuse(refusal, WDP_REFUSE_UNSUPPORTED','    if (FALSE)\n    {\n        window_scale_refuse(refusal, WDP_REFUSE_UNSUPPORTED')),
 ('config-name-lost',functions.replace('window_scale_refuse(refusal, WDP_REFUSE_CONFIG, old, scale, 0, offsets[i].name);','window_scale_refuse(refusal, WDP_REFUSE_CONFIG, old, scale, 0, NULL);')),
 ('image-name-generic',functions.replace('{"close", "System/Close/Default"}','{"close", "close"}')),
 ('element-unreported',functions.replace('window_scale_refuse(refusal, WDP_REFUSE_ELEMENT, offset, size, extent, imageName);','(void)0;'))]
with tempfile.TemporaryDirectory(prefix='window-theme-prepare-') as tmp:
 tmp=pathlib.Path(tmp)
 for name,body in variants:
  c=tmp/(name+'.c');exe=tmp/name;c.write_text(pre.replace('REFUSAL_RECORD',refusal)+macros+'\n'+structs+body+main);subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if name=='actual':
   if q.returncode:raise SystemExit(q.stderr)
   print(q.stdout,end='')
  elif q.returncode==0:raise SystemExit('escaped mutation '+name)
  else:print('REJECTED '+name)
