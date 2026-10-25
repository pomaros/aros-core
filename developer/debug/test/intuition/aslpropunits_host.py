#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Actual PropGClass UWORD ABI, setter/getter/math and native input boundary.

Native geometry, capture, class input and notifications come from the current
checkout; superclass notification delivery and raster are explicit adapters.
The inline ASL adapter is included unchanged from production. Separate stable
ASL generation fixture exercises whole-adoption deferral and Process retry.
"""
from pathlib import Path
import re,json,subprocess,hashlib,argparse
ROOT=Path(__file__).resolve().parents[4]
def body(name, text):
 m=re.search(r'(?m)^(?:static |STATIC )?(?:IPTR|VOID|void|UWORD|LONG)\s+'+name+r'\([^;]*?\)\s*\{',text);assert m,name
 i=m.end();n=1
 while n:n+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[m.start():i]
import sys
sys.dont_write_bytecode=True
from propfixture import native_fixture, EVENT_HELPER
driver=r'''
static void native_set(Class*cl,struct Gadget*g,LONG total,LONG visible,LONG top){
 struct AslPropUnits u=AslPropEncode(total,visible,top);
 assert(u.total<=65535&&u.visible<=u.total&&u.top<=u.total-u.visible);
 struct TagItem tags[]={{PGA_Top,u.top},{PGA_Total,u.total},{PGA_Visible,u.visible},{TAG_END,0}};
 struct opSet set={OM_SET,tags,NULL};PropGClass__OM_SET(cl,g,&set);
 struct PropGData*d=g->SpecialInfo;assert(d->top==u.top&&d->total==u.total&&d->visible==u.visible);
 for(unsigned i=0;i<3;i++){IPTR value=0;struct opGet get={0,i==0?PGA_Top:i==1?PGA_Total:PGA_Visible,&value};assert(PropGClass__OM_GET(cl,g,&get));assert(value==(IPTR)(i==0?u.top:i==1?u.total:u.visible));}
 assert(AslPropDecode(total,visible,top,u.top)==top);
}
int main(void){
 struct IntuitionBase base={0};Class cl={(IPTR)&base};struct PropGData data={0};struct Gadget gadget={.SpecialInfo=&data};data.propinfo.Flags=FREEVERT;
 const LONG scales[]={100,150,200,400,100},rows[]={0,1,2,3000,10000,40000,1000000};unsigned cases=0;
 for(unsigned s=0;s<5;s++)for(unsigned n=0;n<7;n++){
  LONG line=(13*scales[s]+50)/100+2*((scales[s]+50)/100),total=rows[n]*line,visible=340*scales[s]/100;
  if(visible>total)visible=total;LONG hidden=total-visible,positions[]={0,total/2<hidden?total/2:hidden,hidden/2,hidden};
  for(unsigned i=0;i<4;i++){
   LONG current=positions[i];native_set(&cl,&gadget,total,visible,current);cases++;
   struct AslPropUnits u=AslPropEncode(total,visible,current);LONG range=u.total-u.visible;
   if(i==0)assert(!data.propinfo.VertPot);if(i==3&&hidden)assert(data.propinfo.VertPot==65535);
   for(LONG top=0;top<=range;top+=range/17+1){LONG decoded=AslPropDecode(total,visible,current,top);assert(decoded>=0&&decoded<=hidden);struct AslPropUnits back=AslPropEncode(total,visible,decoded);assert(back.top==top);}
   if(hidden){assert(AslPropDecode(total,visible,current,range)==hidden);assert(!AslPropDecode(total,visible,current,0));}
  }
  printf("rows%ld scale%ld total%ld native%u visible%u PASS\n",(long)rows[n],(long)scales[s],(long)total,data.total,data.visible);
 }
 native_set(&cl,&gadget,2147483647,680,1073741823);assert(!callbacks);
 /* Capture and all nine release/abort routes use the actual current native
  * dispatcher, source geometry, click capture and event provenance. */
 struct Screen screen={.Width=2000,.Height=2000};struct IntWindow win={.W={.WScreen=&screen,.Width=20,.Height=1000}};
 struct GadgetInfo gi={.gi_Window=&win.W,.gi_Screen=&screen};struct IIHData iih={0};struct Interrupt interrupt={&iih};base.InputHandler=&interrupt;
 gadget.GadgetType=GTYP_CUSTOMGADGET;gadget.Width=20;gadget.Height=1000;
 for(unsigned position=0;position<3;position++)for(unsigned ending=0;ending<3;ending++){
  data.propinfo.Flags=FREEVERT|AUTOKNOB|PROPBORDERLESS;
  native_set(&cl,&gadget,90000,680,position==0?0:position==1?45000:89320);
  struct BBox knob;CalcBBox(&win.W,NULL,&gadget,&knob);CalcKnobSize(&gadget,&knob);LONG y=knob.Top+2;
  native_event(&gadget,&gi,&base,10,y,GM_GOACTIVE,IECODE_NOBUTTON);assert(data.propinfo.Flags&KNOBHIT);
  UWORD pot=data.propinfo.VertPot;native_event(&gadget,&gi,&base,10,y,GM_HANDLEINPUT,IECODE_NOBUTTON);assert(data.propinfo.VertPot==pot);
  native_event(&gadget,&gi,&base,10,y+(position==2?-50:50),GM_HANDLEINPUT,IECODE_NOBUTTON);assert(data.propinfo.Flags&KNOBHIT);assert(data.top<=data.total-data.visible);
  if(ending<2)assert(native_event(&gadget,&gi,&base,10,ending==1?-100:y,GM_HANDLEINPUT,SELECTUP)==(GMR_NOREUSE|GMR_VERIFY));
  else{struct gpGoInactive inactive={&gi,1};PropGClass__GM_GOINACTIVE(&cl,&gadget,&inactive);}
  assert(!(data.propinfo.Flags&KNOBHIT));assert(!(gadget.Flags&GFLG_SELECTED));assert(!base.prop_drag.gadget);
 }
 printf("PASS %u native OM_SET/GET boundary positions, exact endpoints and canonical echoes, 9 actual held-input release/abort cases\n",cases);
}
'''

LIST_PREFIX=r'''
#include "asl_intern.h"
#include "layout.h"
#include "propunits.h"
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
extern void abort(void);
extern int printf(const char*,...);
#define CHECK(x) do{if(!(x)){printf("CHECK %d %s\n",__LINE__,#x);abort();}}while(0)
#define D(x)
struct Fake {struct Gadget gadget;union{struct AslListViewData list;struct AslPropData prop;}data;};
#undef INST_DATA
#define INST_DATA(cl,o) (&((struct Fake*)(o))->data)
static struct AslPropUnits notified;
static IPTR DoSuperMethodA(Class*c,Object*o,Msg m){if(m->MethodID==OM_NOTIFY){struct TagItem*t=((struct opUpdate*)m)->opu_AttrList;for(;t->ti_Tag;t++){if(t->ti_Tag==ASLLV_PropTop)notified.top=t->ti_Data;if(t->ti_Tag==ASLLV_PropTotal)notified.total=t->ti_Data;if(t->ti_Tag==ASLLV_PropVisible)notified.visible=t->ti_Data;}}return 0;}
#define DoSuperMethod(...) 0
static struct TagItem*NextTagItem(struct TagItem**t){struct TagItem*r=(*t)++;return r->ti_Tag?r:NULL;}
static struct RastPort*ObtainGIRPort(struct GadgetInfo*g){return NULL;}
static void ReleaseGIRPort(struct RastPort*r){}
static void rendersingleitem(Class*c,Object*o,struct GadgetInfo*g,LONG n){}
static void makenodetable(Class*c,Object*o){}
#define DoMethod(...) 0
static BOOL GetAttr(ULONG t,Object*o,IPTR*v){CHECK(0);return FALSE;}
static IPTR AslListView__OM_SET(Class*,Object*,struct opSet*);
static void DoMethodA(Object*o,Msg m){CHECK(m->MethodID==OM_SET);AslListView__OM_SET(NULL,o,(struct opSet*)m);}
'''
LIST_DRIVER=r'''
static void set_list(struct Fake*f,ULONG tag,IPTR value){struct TagItem t[]={{tag,value},{TAG_DONE,0}};struct opSet m={OM_SET,t,NULL};AslListView__OM_SET(NULL,(Object*)f,&m);}
int main(void){
 struct Fake list={0},prop={0};struct AslListViewData*d=&list.data.list;struct LayoutData ld={.ld_RenderScale=100};
 CHECK(prop_to_lv[0].ti_Tag==PGA_Top&&prop_to_lv[0].ti_Data==ASLLV_PropTop);
 CHECK(lv_to_prop[0].ti_Tag==ASLLV_PropTop&&lv_to_prop[0].ti_Data==PGA_Top);
 CHECK(lv_to_prop[1].ti_Tag==ASLLV_PropTotal&&lv_to_prop[1].ti_Data==PGA_Total);
 CHECK(lv_to_prop[2].ti_Tag==ASLLV_PropVisible&&lv_to_prop[2].ti_Data==PGA_Visible);
 struct Node nodes[40000]={0};struct Node*table[40000];for(LONG n=0;n<40000;n++){table[n]=&nodes[n];if(n<39999)nodes[n].ln_Succ=&nodes[n+1];}
 d->ld=&ld;d->nodetable=table;d->total=40000;d->lineheight=60;d->totalpixels=d->total*d->lineheight;d->visiblepixels=1360;d->active=12345;d->minx=d->miny=0;d->maxx=200;d->maxy=1363;
 d->layouted=TRUE;struct List labels={0};labels.lh_Head=nodes;nodes[39999].ln_Succ=(struct Node*)&labels.lh_Tail;set_list(&list,ASLLV_Labels,(IPTR)&labels);CHECK(d->total==40000);d->active=12345;
 prop.data.prop.listview=(Object*)&list;prop.data.prop.deltafactor=d->lineheight;
 const LONG positions[]={0,1200000,2398640};
 for(unsigned i=0;i<3;i++){
  set_list(&list,ASLLV_TopPixel,positions[i]);CHECK(d->toppixel==positions[i]&&d->active==12345);notifyall(NULL,(Object*)&list,NULL,0);
  if(i==1)CHECK(d->scroll==positions[i]);
  struct AslPropUnits u=AslPropEncode(d->totalpixels,d->visiblepixels,d->toppixel);CHECK(notified.top==u.top&&notified.total==u.total&&notified.visible==u.visible);
  set_list(&list,ASLLV_PropTop,u.top);CHECK(d->toppixel==positions[i]);
  set_list(&list,ASLLV_PropTop,u.total-u.visible);CHECK(d->toppixel==d->totalpixels-d->visiblepixels);
  set_list(&list,ASLLV_PropTop,0);CHECK(d->toppixel==0);
 }
 set_list(&list,ASLLV_MakeVisible,39999);CHECK(d->toppixel==d->totalpixels-d->visiblepixels);CHECK(findnode(NULL,(Object*)&list,39999)==&nodes[39999]);d->nodetable=NULL;CHECK(findnode(NULL,(Object*)&list,39999)==&nodes[39999]);d->nodetable=table;
 LONG lasty=BORDERLVSPACINGY+39999*d->lineheight-d->toppixel;CHECK(mouseitem(NULL,(Object*)&list,20,lasty)==39999);
 set_list(&list,ASLLV_Active,39999);CHECK(d->active==39999&&IS_SELECTED(&nodes[39999]));
 set_list(&list,ASLLV_Top,20000);LONG before=d->toppixel;
 struct TagItem arrow[]={{ASLSC_Inc,ID_ARROWINC},{TAG_DONE,0}};struct opUpdate update={OM_UPDATE,arrow,NULL,OPUF_INTERIM};AslProp__OM_SET(NULL,(Object*)&prop,(struct opSet*)&update);CHECK(d->toppixel==before+d->lineheight);
 arrow[0]=(struct TagItem){ASLSC_Dec,ID_ARROWDEC};AslProp__OM_SET(NULL,(Object*)&prop,(struct opSet*)&update);CHECK(d->toppixel==before);
 CHECK(d->active==39999);CHECK(d->scroll==-d->lineheight);
 d->totalpixels=2147483647;d->visiblepixels=680;d->toppixel=d->totalpixels-d->visiblepixels;set_list(&list,ASLLV_ScrollPixels,60);CHECK(d->toppixel==2147482967);
 d->total=1;d->totalpixels=60;d->visiblepixels=10;d->toppixel=0;set_list(&list,ASLLV_MakeVisible,0);CHECK(d->toppixel==0);
 set_list(&list,ASLLV_TopPixel,999);CHECK(d->toppixel==50);set_list(&list,ASLLV_MakeVisible,0);CHECK(d->toppixel==0);
 d->total=0;d->totalpixels=0;set_list(&list,ASLLV_MakeVisible,0);CHECK(d->toppixel==0);
 LONG oldline=d->lineheight;struct List*oldlabels=d->labels;LONG oldtotal=d->total;d->lineheight=2147483647;set_list(&list,ASLLV_Labels,(IPTR)&labels);CHECK(d->labels==oldlabels&&d->total==oldtotal);d->lineheight=oldline;
 printf("PASS actual list OM_SET/notify/MakeVisible/hit/findnode/selection and exact one-row arrow routes, 40000 rows and tiny/empty viewport\n");
}
'''

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);args=ap.parse_args();out=args.out;out.mkdir(parents=True,exist_ok=True)
 native,sources=native_fixture()
 native+='\n#define callbacks notifications\n#include "propunits.h"\n'+EVENT_HELPER+driver
 (out/'native.c').write_text(native)
 cmd=['clang','-std=c11','-g','-O1','-fsanitize=address,undefined,unsigned-integer-overflow','-I',str(ROOT/'workbench/libs/asl'),str(out/'native.c'),'-o',str(out/'native')]
 p=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stderr
 p=subprocess.run([cmd[-1]],capture_output=True,text=True);(out/'run.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stdout+p.stderr
 print(p.stdout,end='')
 maps=(ROOT/'workbench/libs/asl/gadgets.c').read_text()
 maps='\n'.join(re.search(r'static const struct TagItem '+name+r'\s*\[\]\s*=\s*\{.*?\n\};',maps,re.S).group(0) for name in ['prop_to_lv','lv_to_prop'])
 listsource=LIST_PREFIX+'\n'+maps+'\n'+body('FindListNode',(ROOT/'workbench/libs/asl/basicfuncs.c').read_text().replace('struct Node *FindListNode','IPTR FindListNode')).replace('IPTR FindListNode','struct Node *FindListNode')+'\n'+body('CountNodes',(ROOT/'workbench/libs/asl/basicfuncs.c').read_text())+'\n'
 lv=(ROOT/'workbench/libs/asl/listviewclass.c').read_text()
 for name in ['findnode','mouseitem','notifyall','notifytop','AslListView__OM_SET']:
  # The source extractor accepts pointer return types for list node lookup.
  if name=='findnode':
   text=lv.replace('static struct Node *findnode','static IPTR findnode')
   extracted=body(name,text).replace('static IPTR findnode','static struct Node *findnode')
  else:extracted=body(name,lv)
  listsource+=extracted+'\n'
 listsource+=body('AslProp__OM_SET',(ROOT/'workbench/libs/asl/propclass.c').read_text())+'\n'+LIST_DRIVER
 (out/'list.c').write_text(listsource)
 sdk=ROOT.parent/'Build/display-composition-native'
 listcmd=['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined,unsigned-integer-overflow','-Wno-incompatible-pointer-types','-I',str(ROOT/'workbench/libs/asl'),'-I',str(ROOT/'compiler/include'),'-I',str(sdk/'bin/apple-aarch64/gen/include'),str(out/'list.c'),'-o',str(out/'list')]
 p=subprocess.run(listcmd,capture_output=True,text=True);(out/'list-compile.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stderr
 p=subprocess.run([listcmd[-1]],capture_output=True,text=True);(out/'list-run.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stdout+p.stderr
 print(p.stdout,end='')
 rejected=[]
 helper=(ROOT/'workbench/libs/asl/propunits.h').read_text()
 mutations={
  'raw_pixel_total':('units.total = total <= 65535 ? total : 65535;', 'units.total = total;'),
  'canonical_echo':('if (top == (IPTR)units.top) return current;', 'if (0) return current;'),
  'inverse_range':('((QUAD)top * hidden + range / 2) / range', 'top'),
  'narrow_product':('(QUAD)top * (units.total - units.visible)', 'top * (units.total - units.visible)'),
 }
 for name,(old,new) in mutations.items():
  assert old in helper,name
  folder=out/name;folder.mkdir(exist_ok=True);(folder/'propunits.h').write_text(helper.replace(old,new,1))
  command=cmd.copy();command[command.index('-I'):command.index('-I')]=['-I',str(folder)];command[-1]=str(folder/'control')
  p=subprocess.run(command,capture_output=True,text=True);assert not p.returncode,p.stderr
  p=subprocess.run([command[-1]],capture_output=True,text=True);(folder/'run.log').write_text(p.stdout+p.stderr);assert p.returncode,'surviving '+name
  rejected.append(name)
 listmutations={
  'old_pixel_map':('ASLLV_PropTop}', 'ASLLV_TopPixel}'),
  'word_hit_result':('LONG                        result = -5;', 'WORD                        result = -5;'),
  'word_count':('LONG        result = 0;', 'WORD        result = 0;'),
  'word_scroll':('data->scroll    = redraw ? 0 : newtop - data->toppixel;', 'data->scroll = (WORD)(newtop-data->toppixel);'),
  'rounded_arrows':('-data->deltafactor : data->deltafactor', '-1 : 1'),
 }
 for name,(old,new) in listmutations.items():
  assert old in listsource,name
  path=out/(name+'.c');path.write_text(listsource.replace(old,new,1));command=listcmd.copy();command[-3]=str(path);command[-1]=str(out/name)
  p=subprocess.run(command,capture_output=True,text=True);assert not p.returncode,p.stderr
  p=subprocess.run([command[-1]],capture_output=True,text=True);(out/(name+'.log')).write_text(p.stdout+p.stderr);assert p.returncode,'surviving '+name
  rejected.append(name)
 print('Rejected actual-source mutations:',','.join(rejected))
 result={'status':'PASS','scope':__doc__,'command':cmd,'list_command':listcmd,'rejected_mutations':rejected,'native_sources':sources,'sources':{str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [ROOT/'rom/intuition/propgclass.c',ROOT/'rom/intuition/propgadgets.c',ROOT/'workbench/libs/asl/propunits.h',ROOT/'workbench/libs/asl/listviewclass.c',ROOT/'workbench/libs/asl/propclass.c',ROOT/'workbench/libs/asl/basicfuncs.c',ROOT/'workbench/libs/asl/gadgets.c',Path(__file__).resolve()]}}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
