#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual drag constructor/pixels and mixed-domain event path; external mocks only."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[4]
drag=(root/'workbench/libs/muimaster/dragndrop.c').read_text(errors='surrogateescape')
node=drag[drag.index('struct BitMapNode\n{'):drag.index('#define bmn_Succ')]
constructor=drag[drag.index('struct BitMapNode *CreateBitMapNodeA('):drag.index('struct BitMap *GetBitMap(')]
pixels=(root/'workbench/libs/muimaster/dragpixels.h').read_text()
window=(root/'workbench/libs/muimaster/classes/window.c').read_text()
rebase=window[window.index('static LONG WindowRebaseCoordinate'):window.index('static void WindowRebasePosition')]
coordinates=window[window.index('static ULONG WindowCanvasFactor'):window.index('static APTR WindowVisualInfo')]
reservation=window[window.index('static IPTR ReserveNativeWindowGeneration('):window.index('static BOOL DisplayWindow(')]
handler=window[window.index('static BOOL LiveDragWindow('):window.index('/* Reply to imsg if handled */')]
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef uint8_t UBYTE;typedef int16_t WORD;
typedef uintptr_t IPTR;typedef void *APTR;typedef int BOOL;typedef void VOID;
typedef uint64_t UQUAD;typedef int64_t QUAD;
#define TRUE 1
#define FALSE 0
#define MEMF_CLEAR 1
#define MEMF_ANY 0
#define BMA_WIDTH 1
#define BMA_HEIGHT 2
#define RECTFMT_ARGB 1
#define GUI_BitMap 1
#define GUI_Mask 2
#define GUI_LeftOffset 3
#define GUI_TopOffset 4
#define GUI_Width 5
#define GUI_Height 6
#define GUI_SourceAlpha 7
#define GUI_IntegerScale 8
struct MinNode {struct MinNode *mln_Succ,*mln_Pred;};
struct BitMap {ULONG width,height;UBYTE pixels[16];};
struct RastPort {struct BitMap *BitMap;};struct TagItem {ULONG ti_Tag;IPTR ti_Data;};
static unsigned allocations,live,failat,reads,rpdepth;static BOOL readfail;
static void *allocate(size_t n){if(++allocations==failat)return NULL;void *p=calloc(1,n);assert(p);++live;return p;}
static void *AllocMem(size_t n,ULONG flags){(void)flags;return allocate(n);}
static void *AllocVec(size_t n,ULONG flags){(void)flags;return allocate(n);}
static void FreeVec(void *p){if(p){assert(live);--live;free(p);}}
static void FreeMem(void *p,size_t n){(void)n;FreeVec(p);}
struct Window;
static void CloseWindow(struct Window *window){(void)window;abort();}
static ULONG GetBitMapAttr(struct BitMap *bm,ULONG attr){return attr==BMA_WIDTH?bm->width:bm->height;}
static void InitRastPort(struct RastPort *rp){memset(rp,0,sizeof(*rp));++rpdepth;}
static void DeinitRastPort(struct RastPort *rp){(void)rp;assert(rpdepth);--rpdepth;}
static ULONG ReadPixelArray(void *out,ULONG sx,ULONG sy,ULONG stride,struct RastPort *rp,ULONG x,ULONG y,ULONG w,ULONG h,ULONG fmt){
 assert(!sx&&!sy&&!x&&!y&&fmt==RECTFMT_ARGB&&stride==w*4);++reads;if(readfail)return 0;
 assert(w==2&&h==2&&rp->BitMap);memcpy(out,rp->BitMap->pixels,16);return 4;
}
static struct TagItem *NextTagItem(struct TagItem **state){struct TagItem *p=*state;if(!p->ti_Tag)return NULL;++*state;return p;}
'''
pixeltests=r'''
static struct BitMap bitmap={2,2,{0,1,2,3, 64,4,5,6, 128,7,8,9, 255,10,11,12}};
static struct BitMapNode *make(ULONG factor,BOOL alpha){struct TagItem tags[]={
 {GUI_BitMap,(IPTR)&bitmap},{GUI_Width,2},{GUI_Height,2},{GUI_LeftOffset,1},{GUI_TopOffset,(IPTR)-2},
 {GUI_IntegerScale,factor},{GUI_SourceAlpha,alpha},{0,0}};return CreateBitMapNodeA(tags);}
static void test_pixels(void){
 for(ULONG factor=1;factor<=4;++factor)for(BOOL alpha=0;alpha<2;++alpha){allocations=0;struct BitMapNode *n=make(factor,alpha);assert(n);
  assert(n->bmn_Width==(LONG)(2*factor)&&n->bmn_Height==(LONG)(2*factor)&&n->bmn_Left==(LONG)factor&&n->bmn_Top==-(LONG)(factor*2));
  assert(n->bmn_BitMap==&bitmap&&!memcmp(bitmap.pixels,(UBYTE[]){0,1,2,3,64,4,5,6,128,7,8,9,255,10,11,12},16));
  for(ULONG y=0;y<2*factor;++y)for(ULONG x=0;x<2*factor;++x){
   UBYTE *p=(UBYTE *)n->bmn_BitMapBuffer+(y*2*factor+x)*4,*s=bitmap.pixels+((y/factor)*2+x/factor)*4;
   assert(p[0]==(!alpha?255:s[0])&&!memcmp(p+1,s+1,3));}
  DeleteBitMapNode(n);assert(!live&&!rpdepth);
 }
 for(failat=1;failat<=3;++failat){allocations=0;assert(!make(2,TRUE));assert(!live&&!rpdepth);}failat=0;
 readfail=TRUE;assert(!make(2,TRUE));assert(!live&&!rpdepth);readfail=FALSE;
 assert(!make(0,TRUE)&&!make(5,TRUE)&&!live);
 struct BitMapNode n={.bmn_BitMap=&bitmap,.bmn_Width=3,.bmn_Height=2};unsigned before=reads;
 assert(!PrepareDragPixels(&n,TRUE,2)&&reads==before&&!live);
 n.bmn_Width=2;n.bmn_Mask=&bitmap;assert(!PrepareDragPixels(&n,TRUE,2)&&!live);
 n.bmn_Mask=NULL;n.bmn_Left=INT32_MAX;assert(!PrepareDragPixels(&n,TRUE,2)&&!live);
 n.bmn_Left=0;n.bmn_Width=8192;bitmap.width=8192;assert(!PrepareDragPixels(&n,TRUE,2)&&reads==before&&!live);bitmap.width=2;
 puts("scaledrag actual constructor/pixels PASS: factors1..4 opaque/alpha exactpixels+hotspots, sourceimmutable,3allocationfaults/readfault/bounds refuse without leaks");
}
'''
events=r'''
enum {IDCMP_MOUSEMOVE=1,IDCMP_MOUSEBUTTONS,IDCMP_CLOSEWINDOW,IDCMP_CHANGESCALE};
enum {MENUDOWN=1,SELECTUP};
enum {MUIA_Application_WindowList=1,MUIA_Window_Window,MUIA_Window_RootObject,MUIA_Window_NativeGeneration};
enum {MUIM_DragFinish=1,MUIM_DragQueryExtended,MUIM_DragBegin,MUIM_DragReport,MUIM_DragDrop,MUIM_UnknownDropDestination,MUIM_DeleteDragImage,MUIM_ScaleContextChildren};
enum {MUIV_DragReport_Abort=1,MUIV_DragReport_Continue,MUIV_DragReport_Lock,MUIV_DragReport_Refresh};
#define MADF_DRAGGING 1
struct Layer_Info {int lock;};struct Window;struct Layer {struct Window *Window;};
struct Screen {struct Layer_Info LayerInfo;ULONG factor;};
struct Window {LONG LeftEdge,TopEdge;struct Screen *WScreen;struct Layer *WLayer;};
struct MUI_RenderInfo {BOOL mri_ScaleAware;struct Screen *mri_Screen;struct Window *mri_Window;void *mri_WindowObject;};
typedef struct Object {struct MUI_RenderInfo mri;struct Object *next,*root;ULONG mad_Flags;IPTR generation;struct MinList *children;} Object;
struct MinList {Object *mlh_Head;};struct DragNDrop {int dummy;};
struct MUI_WindowData {struct MUI_RenderInfo wd_RenderInfo;Object *wd_DropObject,*wd_DragObject;
 Object *wd_DropWindowObject;IPTR wd_DropGeneration;struct Window *wd_DropWindow;struct DragNDrop *wd_dnd;void *wd_DragImage;};
struct IntuiMessage {ULONG Class,Code;struct Window *IDCMPWindow;LONG MouseX,MouseY;};struct Message {int dummy;};
static struct Layer hit;static struct MinList windowlist;static LONG expectedX,expectedY,expectedPhysical;
static unsigned queries,reports,drops,finishes,replies,deleted,draws,images,begins,unknowns;
static ULONG disposeAt;static Object *disposeWindow,*disposeRoot,*disposeLeaf,*queryReturn;
static BOOL disposeChild;
static ULONG closeAt;static BOOL reopenTarget,failDraw;static Object *closeDest,*closeRoot;
static struct MUI_WindowData *reentrant,*cancelData;static ULONG cancelAt;
static void CancelWindowDrag(struct MUI_WindowData *data);
#define muiRenderInfo(o) (&(o)->mri)
#define muiAreaData(o) (o)
#define _window(o) ((o)->mri.mri_Window)
#define _app(o) (o)
#define _left(o) ((void)(o),0)
#define _top(o) ((void)(o),0)
#define _right(o) ((void)(o),500)
#define _bottom(o) ((void)(o),500)
static ULONG zune_legacy_factor(struct Screen *screen){return screen->factor;}
static void LockLayerInfo(struct Layer_Info *info){assert(!info->lock);++info->lock;}
static void UnlockLayerInfo(struct Layer_Info *info){assert(info->lock);--info->lock;}
static struct Layer *WhichLayer(struct Layer_Info *info,LONG x,LONG y){assert(info->lock&&x==expectedPhysical&&y==expectedPhysical);return &hit;}
static void get_attr(Object *object,ULONG attr,void *out){if(attr==MUIA_Window_NativeGeneration){*(IPTR*)out=object->generation;return;}void *p=attr==MUIA_Application_WindowList?(void *)&windowlist:attr==MUIA_Window_Window?(void *)object->mri.mri_Window:(void *)object->root;memcpy(out,&p,sizeof(p));}
#define get(o,a,p) get_attr(o,a,p)
static Object *NextObject(Object **state){Object *obj=*state;if(obj)*state=obj->next;return obj;}
static IPTR method(Object *obj,ULONG id,IPTR *args){assert(obj);IPTR queryresult=(IPTR)(queryReturn?queryReturn:obj);
 if(id==disposeAt&&disposeWindow){if(disposeChild){disposeRoot->children->mlh_Head=NULL;free(disposeLeaf);disposeLeaf=NULL;}else{windowlist.mlh_Head=NULL;free(disposeRoot);free(disposeWindow);}disposeWindow=disposeRoot=NULL;}
 if(id==closeAt&&closeDest){++closeDest->generation;struct Window *old=closeDest->mri.mri_Window;closeDest->mri.mri_Window=closeRoot->mri.mri_Window=reopenTarget?old:NULL;closeDest=NULL;}
 if(id==cancelAt&&cancelData){struct MUI_WindowData *p=cancelData;cancelData=NULL;CancelWindowDrag(p);}
 switch(id){
 case MUIM_DragQueryExtended:++queries;assert((LONG)args[1]==expectedX&&(LONG)args[2]==expectedY);return queryresult;
 case MUIM_ScaleContextChildren:return (IPTR)obj->children;
 case MUIM_DragBegin:++begins;return 0;
 case MUIM_UnknownDropDestination:++unknowns;return 0;
 case MUIM_DragReport:++reports;assert((LONG)args[1]==expectedX&&(LONG)args[2]==expectedY);return MUIV_DragReport_Continue;
 case MUIM_DragDrop:++drops;assert((LONG)args[1]==expectedX&&(LONG)args[2]==expectedY);return 0;
 case MUIM_DragFinish:++finishes;if(reentrant){struct MUI_WindowData *p=reentrant;reentrant=NULL;assert(!p->wd_DragObject&&!p->wd_DropObject&&!p->wd_dnd);CancelWindowDrag(p);}return 0;
 case MUIM_DeleteDragImage:++images;return 0;
 default:abort();}}
#define DoMethod(o,m,...) method(o,m,(IPTR[]){__VA_ARGS__})
static void UndrawDragNDrop(struct DragNDrop *drag){assert(drag);}
static BOOL DrawDragNDrop(struct DragNDrop *drag,LONG x,LONG y){assert(drag&&x==expectedPhysical&&y==expectedPhysical);++draws;return !failDraw;}
static void DeleteDragNDrop(struct DragNDrop *drag){assert(drag);++deleted;}
static ULONG GetDefaultEvents(void){return 0;}
static void ChangeEvents(struct MUI_WindowData *data,ULONG events){(void)data;(void)events;}
static void ReplyMsg(struct Message *message){assert(message);++replies;}
'''
events+=r'''
struct MUIMasterBase_intern {int ZuneSemaphore;IPTR NextNativeWindowGeneration;};
static struct MUIMasterBase_intern librarybase;
#define MUIMasterBase (&librarybase)
#define MUIMB(p) (p)
static void ObtainSemaphore(int *s){assert(!*s);*s=1;}
static void ReleaseSemaphore(int *s){assert(*s==1);*s=0;}
'''
eventtests=r'''
static void test_events(void){
 for(ULONG factor=1;factor<=4;++factor)for(BOOL aware=0;aware<2;++aware)for(BOOL targetAware=0;targetAware<2;++targetAware){
  struct Screen screen={.factor=factor};struct Layer privateLayer={0};struct Window source={10,10,&screen,NULL},target={5,5,&screen,&privateLayer};
  Object from={.mri={aware,&screen,&source,NULL},.mad_Flags=MADF_DRAGGING},to={.mri={targetAware,&screen,&target,NULL}},dest={.mri={targetAware,&screen,&target,NULL},.root=&to};
  struct DragNDrop dragstate;struct MUI_WindowData data={.wd_RenderInfo=from.mri,.wd_DragObject=&from,.wd_dnd=&dragstate,.wd_DragImage=&from};
  struct IntuiMessage message={IDCMP_MOUSEMOVE,0,&source,15,15};
  windowlist.mlh_Head=&dest;hit.Window=&target;
  expectedPhysical=25*(aware?1:factor);expectedX=expectedY=expectedPhysical/(targetAware?1:factor)-5;
  queries=reports=drops=finishes=replies=deleted=draws=images=0;
  data.wd_RenderInfo.mri_WindowObject=&from;assert(!HandleDragging(&from,&data,&message));assert(queries==1&&reports==2&&replies==1&&draws==1&&data.wd_DropObject==&to&&!screen.LayerInfo.lock);
  reentrant=&data;message.Class=IDCMP_MOUSEBUTTONS;message.Code=SELECTUP;
  assert(HandleDragging(&from,&data,&message));assert(drops==1&&finishes==1&&deleted==1&&images==1&&!data.wd_DragObject&&!data.wd_DropObject&&!(from.mad_Flags&MADF_DRAGGING));
 }
 for(ULONG action=MUIM_DragQueryExtended;action<=MUIM_DragDrop;++action){
  struct Screen screen={.factor=2};struct Window source={10,10,&screen,NULL},target={5,5,&screen,NULL};
  Object from={.mri={FALSE,&screen,&source,NULL},.mad_Flags=MADF_DRAGGING},to={.mri={TRUE,&screen,&target,NULL}},dest={.mri=to.mri,.root=&to};
  struct DragNDrop dragstate;struct MUI_WindowData data={.wd_RenderInfo=from.mri,.wd_DragObject=&from,.wd_dnd=&dragstate,.wd_DragImage=&from};
  struct IntuiMessage message={IDCMP_MOUSEMOVE,0,&source,15,15};windowlist.mlh_Head=&dest;hit.Window=&target;
  expectedPhysical=50;expectedX=expectedY=45;queries=reports=drops=finishes=replies=deleted=draws=images=0;
  cancelAt=action;cancelData=&data;reentrant=&data;
  data.wd_RenderInfo.mri_WindowObject=&from;assert(!HandleDragging(&from,&data,&message));
  if(action==MUIM_DragDrop){assert(data.wd_DragObject);message.Class=IDCMP_MOUSEBUTTONS;message.Code=SELECTUP;assert(HandleDragging(&from,&data,&message));}
  assert(!data.wd_DragObject&&!data.wd_DropObject&&!data.wd_dnd&&deleted==1&&images==1);
  CancelWindowDrag(&data);assert(deleted==1&&images==1);cancelAt=0;cancelData=reentrant=NULL;
 }

 for(ULONG action=MUIM_DragQueryExtended;action<=MUIM_DragReport;++action)for(int reopen=0;reopen<2;++reopen){
  struct Screen screen={.factor=2};struct Window source={10,10,&screen,NULL},target={5,5,&screen,NULL};
  Object from={.mri={FALSE,&screen,&source,NULL},.mad_Flags=MADF_DRAGGING},to={.mri={TRUE,&screen,&target,NULL}},dest={.mri=to.mri,.root=&to};
  struct DragNDrop dragstate;struct MUI_WindowData data={.wd_RenderInfo=from.mri,.wd_DragObject=&from,.wd_dnd=&dragstate,.wd_DragImage=&from};
  struct IntuiMessage message={IDCMP_MOUSEMOVE,0,&source,15,15};windowlist.mlh_Head=&dest;hit.Window=&target;
  expectedPhysical=50;expectedX=expectedY=45;queries=reports=drops=finishes=replies=deleted=draws=images=begins=unknowns=0;
  closeAt=action;closeDest=&dest;closeRoot=&to;reopenTarget=reopen;
  data.wd_RenderInfo.mri_WindowObject=&from;assert(!HandleDragging(&from,&data,&message));assert(data.wd_DragObject&&!data.wd_DropObject&&!drops&&!finishes);
  assert(begins==(unsigned)(action!=MUIM_DragQueryExtended)&&reports==(unsigned)(action==MUIM_DragReport));
  message.Class=IDCMP_MOUSEBUTTONS;message.Code=SELECTUP;assert(HandleDragging(&from,&data,&message));assert(unknowns==1&&!drops&&!finishes&&deleted==1&&images==1&&!data.wd_DragObject);
  closeAt=0;closeDest=closeRoot=NULL;
 }
 {
  struct Screen screen={.factor=2};struct Window source={10,10,&screen,NULL},target={5,5,&screen,NULL};
  Object from={.mri={FALSE,&screen,&source,NULL},.mad_Flags=MADF_DRAGGING},to={.mri={TRUE,&screen,&target,NULL}},dest={.mri=to.mri,.root=&to};
  struct DragNDrop dragstate;struct MUI_WindowData data={.wd_RenderInfo=from.mri,.wd_DragObject=&from,.wd_dnd=&dragstate,.wd_DragImage=&from};
  struct IntuiMessage message={IDCMP_MOUSEMOVE,0,&source,15,15};windowlist.mlh_Head=&dest;hit.Window=&target;
  expectedPhysical=50;expectedX=expectedY=45;queries=reports=drops=finishes=replies=deleted=draws=images=unknowns=0;
  data.wd_RenderInfo.mri_WindowObject=&from;assert(!HandleDragging(&from,&data,&message));assert(data.wd_DropObject);dest.mri.mri_Window=NULL;to.mri.mri_Window=NULL;
  message.Class=IDCMP_MOUSEBUTTONS;message.Code=SELECTUP;assert(HandleDragging(&from,&data,&message));assert(!drops&&!finishes&&unknowns==1&&deleted==1);
 }
 {
  struct Screen screen={.factor=2};struct Window source={10,10,&screen,NULL};Object from={.mri={FALSE,&screen,&source,NULL},.mad_Flags=MADF_DRAGGING};
  struct DragNDrop dragstate;struct MUI_WindowData data={.wd_RenderInfo=from.mri,.wd_DragObject=&from,.wd_dnd=&dragstate,.wd_DragImage=&from};struct IntuiMessage message={IDCMP_MOUSEMOVE,0,&source,15,15};
  windowlist.mlh_Head=NULL;hit.Window=NULL;expectedPhysical=50;deleted=images=0;failDraw=TRUE;data.wd_RenderInfo.mri_WindowObject=&from;assert(!HandleDragging(&from,&data,&message));assert(!data.wd_DragObject&&deleted==1&&images==1);failDraw=FALSE;
 }

 for(ULONG action=MUIM_DragQueryExtended;action<=MUIM_DragReport;++action)for(int child_only=0;child_only<2;++child_only){
  struct Screen screen={.factor=2};struct Window source={10,10,&screen,NULL},target={5,5,&screen,NULL};
  Object from={.mri={FALSE,&screen,&source,NULL},.mad_Flags=MADF_DRAGGING};
  Object *dest=calloc(1,sizeof(*dest)),*root=calloc(1,sizeof(*root)),*leaf=child_only?calloc(1,sizeof(*leaf)):NULL;assert(dest&&root&&(!child_only||leaf));
  dest->mri=(struct MUI_RenderInfo){TRUE,&screen,&target,NULL};dest->root=root;dest->generation=ReserveNativeWindowGeneration();root->mri=dest->mri;
  struct MinList children={leaf};if(child_only){leaf->mri=dest->mri;root->children=&children;}
  struct DragNDrop dragstate;struct MUI_WindowData data={.wd_RenderInfo=from.mri,.wd_DragObject=&from,.wd_dnd=&dragstate,.wd_DragImage=&from};
  struct IntuiMessage message={IDCMP_MOUSEMOVE,0,&source,15,15};windowlist.mlh_Head=dest;hit.Window=&target;
  expectedPhysical=50;expectedX=expectedY=45;queries=reports=drops=finishes=replies=deleted=draws=images=begins=unknowns=0;
  disposeAt=action;disposeChild=child_only;disposeWindow=dest;disposeRoot=root;disposeLeaf=leaf;queryReturn=child_only?leaf:root;
  data.wd_RenderInfo.mri_WindowObject=&from;assert(!HandleDragging(&from,&data,&message));assert(data.wd_DragObject&&!data.wd_DropObject&&!drops&&!finishes);
  assert(begins==(unsigned)(action!=MUIM_DragQueryExtended)&&reports==(unsigned)(action==MUIM_DragReport));
  message.Class=IDCMP_MOUSEBUTTONS;message.Code=SELECTUP;assert(HandleDragging(&from,&data,&message));assert(unknowns==1&&!drops&&!finishes&&deleted==1&&images==1&&!data.wd_DragObject);
  if(child_only){free(root);free(dest);}disposeAt=0;queryReturn=disposeWindow=disposeRoot=disposeLeaf=NULL;
 }
 IPTR first=ReserveNativeWindowGeneration(),second=ReserveNativeWindowGeneration();assert(first&&second>first);librarybase.NextNativeWindowGeneration=(IPTR)-1;assert(!ReserveNativeWindowGeneration()&&librarybase.NextNativeWindowGeneration==(IPTR)-1);librarybase.NextNativeWindowGeneration=0;
 puts("scaledrag actual HandleDragging PASS:16 mixed source/target/factor paths,physical proxyowner lookup,logical query/report/drop coordinates,physical ghostposition,cleanup; Query/Begin/Report target Window/leaf disposal, global lifetime cookie+wrap");
}
int main(void){test_pixels();test_events();return 0;}
'''
text=head+node+pixels+constructor+pixeltests+events+reservation+rebase+coordinates+handler+eventtests
mutants={
 'target-generation-query':('generation != expectedgeneration','0'),
 'target-generation-live':('!drop || !LiveDragWindow(source, windowobject)','!drop'),
 'target-object-tree':('DragObjectOwnedBy(root, drop, 0)','1'),
 'global-cookie':('generation = ++base->NextNativeWindowGeneration;','generation = 1;'),
 'source-domain':('return (coordinate + origin) * (LONG)WindowCanvasFactor(mri);','return coordinate + origin;'),
 'target-domain':('return WindowRebaseCoordinate(coordinate, 1, WindowCanvasFactor(mri)) - origin;','return coordinate - origin;'),
 'proxy-owner':('if (wnd == hovered)','if (wnd->WLayer == layer)'),
 'ghost-scale':('scale = data;','scale = 1;'),
 'ghost-hotspot':('node->bmn_Left = left; node->bmn_Top = top;','node->bmn_Left = node->bmn_Left; node->bmn_Top = node->bmn_Top;'),
 'opaque-alpha':('sourcealpha ? src[0] : 255','src[0]'),
 'cancel-ownership':('data->wd_DropWindow = NULL; data->wd_dnd = NULL;', 'data->wd_DropWindow = NULL;'),
 'allocation-cleanup':('if (!scaled) { FreeVec(pixels); return FALSE; }','if (!scaled) { return FALSE; }'),
}
with tempfile.TemporaryDirectory(prefix='aros-scaledrag-') as directory:
    def check(label,program,positive):
        path=Path(directory)/(label+'.c');exe=Path(directory)/label;path.write_text(program,errors='surrogateescape')
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(path),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if positive:
            assert result.returncode==0,result.stdout+result.stderr;print(result.stdout,end='')
        else:assert result.returncode!=0,label+' survived';print('REJECT mutant '+label)
    check('production',text,True)
    for name,(before,after) in mutants.items():
        assert before in text,name
        check(name,text.replace(before,after),False)
