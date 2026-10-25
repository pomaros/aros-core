#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Current native PropGClass + ASL wrappers, canonical pixels and held geometry.

Actual native setter/getter/notify/capture/geometry/provenance methods share one
source fixture. Only list notification mapping, raster, locks and object dispatch
are adapters. The stable generation fixture complements this test with actual
KNOBHIT adoption deferral/retry. Full event-loop/OS/native runtime is unverified.
"""
from pathlib import Path
import argparse,re,subprocess,json,hashlib
import sys
sys.dont_write_bytecode=True
from propfixture import native_fixture,EVENT_HELPER,fn,structure
from aslpropunits_host import driver as units_driver
ROOT=Path(__file__).resolve().parents[4];SRC=ROOT/'workbench/libs/asl'
EXTRA=r'''
#define ASLLV_TotalPixels 500
#define ASLLV_VisiblePixels 501
#define ASLLV_TopPixel 502
struct LayoutData;
ASL_DATA
static Class native_class,asl_class;
static struct AslPropData asl_data;
static struct Gadget *native_gadget;
static LONG pixel_total,pixel_visible,pixel_top;
static unsigned snaps,interim_notifies,final_notifies;
static BOOL GetAttr(ULONG,Object*,IPTR*);
static IPTR PropGClass__GM_GOACTIVE(Class*,struct Gadget*,struct gpInput*);
static IPTR PropGClass__GM_HANDLEINPUT(Class*,struct Gadget*,struct gpInput*);
static IPTR AslProp__GM_GOACTIVE(Class*,Object*,struct gpInput*);
static IPTR AslProp__GM_HANDLEINPUT(Class*,Object*,struct gpInput*);
#define INST_DATA(c,g) ((c)==&asl_class?(void*)&asl_data:((struct Gadget*)(g))->SpecialInfo)
'''
SUPER=r'''
static IPTR DoSuperMethodA(Class*c,Object*o,Msg msg){
 if(c==&asl_class){struct gpInput*m=msg;if(m->MethodID==GM_GOACTIVE)return PropGClass__GM_GOACTIVE(&native_class,(struct Gadget*)o,m);assert(m->MethodID==GM_HANDLEINPUT);return PropGClass__GM_HANDLEINPUT(&native_class,(struct Gadget*)o,m);}
 if(*(ULONG*)msg==OM_NOTIFY){
  struct opUpdate*m=msg;notifications++;if(m->opu_Flags&OPUF_INTERIM)interim_notifies++;else final_notifies++;
  for(struct TagItem*t=m->opu_AttrList;t->ti_Tag;t++)if(t->ti_Tag==PGA_Top)pixel_top=AslPropDecode(pixel_total,pixel_visible,pixel_top,t->ti_Data);
 }
 return 0;
}
'''
LIST_ADAPTER=r'''
static void DoMethodA(Object*o,Msg msg){
 struct opSet*m=msg;assert(o==asl_data.listview&&m->MethodID==OM_SET);assert(m->ops_AttrList[0].ti_Tag==ASLLV_TopPixel);LONG next=m->ops_AttrList[0].ti_Data;
 if(next>pixel_total-pixel_visible)next=pixel_total-pixel_visible;if(next<0)next=0;pixel_top=next;snaps++;
}
'''
GET_ADAPTER=r'''
static BOOL GetAttr(ULONG tag,Object*o,IPTR*value){
 if(o==asl_data.listview){*value=tag==ASLLV_TotalPixels?pixel_total:pixel_visible;assert(tag==ASLLV_TotalPixels||tag==ASLLV_VisiblePixels);return TRUE;}
 assert(o==native_gadget);struct opGet get={0,tag,value};return PropGClass__OM_GET(&native_class,native_gadget,&get);
}
'''
DRIVER=r'''
static unsigned cases,geometry_cases;
static void setup(struct PropGData*d,struct Gadget*g,LONG top){
 pixel_total=60000000;pixel_visible=1360;pixel_top=top;snaps=interim_notifies=final_notifies=0;
 d->propinfo.Flags=FREEVERT|AUTOKNOB|PROPBORDERLESS;d->flags=PRIVFLAG_NICENOTIFY;
 native_set(&native_class,g,pixel_total,pixel_visible,top);assert(pixel_top==top&&!snaps);
 NotifyTop(&native_class,g,NULL,FALSE);NotifyTop(&native_class,g,NULL,TRUE);assert(pixel_top==top&&!snaps);
}
static void capture(struct PropGData*d,struct Gadget*g,struct GadgetInfo*gi,struct IntuitionBase*base,LONG*x,LONG*y){
 struct BBox knob;struct IBox domain;LONG ox,oy;UWORD factor;
 CalcBBox(gi->gi_Window,gi->gi_Requester,g,&knob);CalcKnobSize(g,&knob);GetGadgetDomain(g,gi->gi_Screen,gi->gi_Window,gi->gi_Requester,&domain);
 assert(WindowCompositionInputFrame(gi->gi_Window,&ox,&oy,&factor));*x=ox+(domain.Left+knob.Left+2)*factor+factor-1;*y=oy+(domain.Top+knob.Top+1)*factor+factor-1;
 assert(native_event(g,gi,base,*x,*y,GM_GOACTIVE,IECODE_NOBUTTON)==GMR_MEACTIVE);assert(d->propinfo.Flags&KNOBHIT);assert(!prop_adoptable(g));cases++;
}
int main(void){
 struct IIHData iih={0};struct Interrupt interrupt={&iih};struct IntuitionBase base={.InputHandler=&interrupt};native_class.cl_UserData=(IPTR)&base;
 struct PropGData d={0};struct Gadget g={.SpecialInfo=&d,.GadgetType=GTYP_CUSTOMGADGET,.Width=20,.Height=1000};native_gadget=&g;
 int list;asl_data.listview=&list;struct Screen screen={.Width=32767,.Height=32767};struct IntWindow window={.W={.WScreen=&screen,.Width=500,.Height=1100,.BorderLeft=7,.BorderTop=13,.BorderRight=9,.BorderBottom=11}};
 struct GadgetInfo gi={.gi_Window=&window.W,.gi_Screen=&screen};struct Requester req={.LeftEdge=17,.TopEdge=29,.Width=450,.Height=1050};
 struct WindowComposition composition={.transform={31,23,1}};LONG x,y;LONG positions[]={340,59998300,30000000};
 for(unsigned i=0;i<3;i++)for(unsigned outside=0;outside<2;outside++){
  setup(&d,&g,positions[i]);capture(&d,&g,&gi,&base,&x,&y);unsigned final_before=final_notifies;
  native_event(&g,&gi,&base,x,y,GM_HANDLEINPUT,IECODE_NOBUTTON);assert(!snaps&&pixel_top==positions[i]);
  assert(native_event(&g,&gi,&base,x,outside?-100:y,GM_HANDLEINPUT,SELECTUP)==(GMR_NOREUSE|GMR_VERIFY));assert(final_notifies>final_before);
  assert(pixel_top==(i==0?0:i==1?59998640:positions[i]));assert(snaps==(i!=2));assert(!base.prop_drag.gadget&&prop_adoptable(&g));cases++;
 }
 for(unsigned i=0;i<3;i++){
  setup(&d,&g,positions[i]);capture(&d,&g,&gi,&base,&x,&y);struct gpGoInactive inactive={&gi,1};
  PropGClass__GM_GOINACTIVE(&native_class,&g,&inactive);assert(pixel_top==positions[i]&&!snaps&&!(d.propinfo.Flags&KNOBHIT)&&!base.prop_drag.gadget);cases++;
 }
 /* Real knob/body/page resolution: one pixel remains below the knob, and the
  * native page-click path reaches the normalized endpoint before ASL snaps. */
 struct AslPropUnits u=AslPropEncode(pixel_total,pixel_visible,0);LONG before_bottom=AslPropDecode(pixel_total,pixel_visible,0,u.total-u.visible-1);
 setup(&d,&g,before_bottom);struct BBox knob;CalcBBox(&window.W,NULL,&g,&knob);CalcKnobSize(&g,&knob);
 assert(native_event(&g,&gi,&base,10,knob.Top+knob.Height,GM_GOACTIVE,IECODE_NOBUTTON)==(GMR_NOREUSE|GMR_VERIFY));assert(pixel_top==59998640&&snaps==1);cases++;
 setup(&d,&g,340);struct gpInput noevent={.MethodID=GM_GOACTIVE,.gpi_GInfo=&gi};assert(AslProp__GM_GOACTIVE(&asl_class,&g,&noevent)==GMR_NOREUSE);assert(pixel_top==340&&!snaps);cases++;
 /* Held client model stays canonical while native relative track/window
  * geometry changes before client adoption. GZZ inner/border and requester
  * domains are actual GetGadgetDomain/GetWinGadgetIBox, not offset adapters. */
 for(unsigned legacy=0;legacy<2;legacy++)for(unsigned domain=0;domain<4;domain++)for(unsigned ending=0;ending<2;ending++){
  window.Composition=legacy?&composition:NULL;composition.transform=(struct ScaleComposeTransform){31,23,3};window.W.LeftEdge=31;window.W.TopEdge=23;window.W.Width=500;window.W.Height=1100;req.Width=450;req.Height=1050;
  window.W.Flags=domain==1?WFLG_GIMMEZEROZERO:0;gi.gi_Requester=domain==3?&req:NULL;
  g.GadgetType=(domain==2?GTYP_GZZGADGET:domain==3?GTYP_REQGADGET:0)|GTYP_CUSTOMGADGET;g.Flags=GFLG_RELRIGHT|GFLG_RELHEIGHT;g.LeftEdge=-30;g.TopEdge=20;g.Width=20;g.Height=-100;
  setup(&d,&g,30000000);capture(&d,&g,&gi,&base,&x,&y);UWORD held_pot=d.propinfo.VertPot,held_top=d.top;
  for(unsigned step=0;step<14;step++){
   unsigned percent=step==13?100:100+25*step;
   if(legacy)composition.transform=(struct ScaleComposeTransform){31+(LONG)step,23+(LONG)step,(percent+50)/100};
   else{window.W.Width=500*percent/100;window.W.Height=1100*percent/100;window.W.LeftEdge=31+step;window.W.TopEdge=23+step;req.Width=450*percent/100;req.Height=1050*percent/100;}
   CalcBBox(&window.W,gi.gi_Requester,&g,&knob);CalcKnobSize(&g,&knob); /* render cache changed before input */
   native_event(&g,&gi,&base,x,y,GM_HANDLEINPUT,IECODE_NOBUTTON);assert(d.propinfo.VertPot==held_pot&&d.top==held_top&&pixel_top==30000000&&!snaps&&!prop_adoptable(&g));
   geometry_cases++;
  }
  if(ending){struct gpGoInactive inactive={&gi,1};PropGClass__GM_GOINACTIVE(&native_class,&g,&inactive);assert(!snaps&&pixel_top==30000000);}
  else{native_event(&g,&gi,&base,x,y,GM_HANDLEINPUT,SELECTUP);assert(pixel_top==30000000&&!snaps);}
  assert(!(d.propinfo.Flags&KNOBHIT)&&!base.prop_drag.gadget&&prop_adoptable(&g));
  /* Motion is a separate capture: stationary holds above retain exact
   * canonical pixels. A genuine normalized motion can quantize large-list
   * pixels; release/abort must then preserve that actual resulting pixel. */
  for(unsigned step=0;step<14;step++){
   unsigned percent=step==13?100:100+25*step;
   if(legacy)composition.transform=(struct ScaleComposeTransform){31+(LONG)step,23+(LONG)step,(percent+50)/100};
   else{window.W.Width=500*percent/100;window.W.Height=1100*percent/100;window.W.LeftEdge=31+step;window.W.TopEdge=23+step;req.Width=450*percent/100;req.Height=1050*percent/100;}
   setup(&d,&g,30000000);capture(&d,&g,&gi,&base,&x,&y);UWORD before=d.propinfo.VertPot,top=d.top;
   if(gi.gi_Requester)req.Height++;else window.W.Height++;
   native_event(&g,&gi,&base,x,y+1,GM_HANDLEINPUT,IECODE_NOBUTTON);assert(d.propinfo.VertPot>before&&d.propinfo.VertPot<before+100);assert(pixel_top>=30000000&&pixel_top<30100000&&!snaps);
   native_event(&g,&gi,&base,x,y,GM_HANDLEINPUT,IECODE_NOBUTTON);assert(d.propinfo.VertPot==before&&d.top==top);
   LONG actual_pixel=pixel_top;native_event(&g,&gi,&base,x,y,GM_HANDLEINPUT,IECODE_NOBUTTON);assert(pixel_top==actual_pixel&&!snaps);
   if(ending){struct gpGoInactive inactive={&gi,1};PropGClass__GM_GOINACTIVE(&native_class,&g,&inactive);}
   else native_event(&g,&gi,&base,x,y,GM_HANDLEINPUT,SELECTUP);
   assert(pixel_top==actual_pixel&&!snaps&&!(d.propinfo.Flags&KNOBHIT)&&!base.prop_drag.gadget);geometry_cases+=3;
  }
  /* Fresh verified input, through the same current geometry, reaches exact
   * canonical top/bottom rather than preserving a quantized endpoint echo. */
  for(unsigned bottom=0;bottom<2;bottom++){
   setup(&d,&g,30000000);capture(&d,&g,&gi,&base,&x,&y);native_event(&g,&gi,&base,x,y+(bottom?20000:-20000),GM_HANDLEINPUT,IECODE_NOBUTTON);native_event(&g,&gi,&base,x,y,GM_HANDLEINPUT,SELECTUP);assert(pixel_top==(bottom?59998640:0));assert(snaps==1);cases++;
  }
 }
 printf("PASS %u ASL input/endpoints/release/abort cases, %u coupled held geometry events (actual relative/GZZ/requester/legacy100..400/reset + nativefirst+1), passive canonical echoes and endpoint wrappers\n",cases,geometry_cases);
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 config=(SRC/'asl.conf').read_text();block=config[config.index('basename AslProp'):config.index('##end class',config.index('basename AslProp'))];assert all(x in block for x in ['GM_GOACTIVE','GM_HANDLEINPUT'])
 actual,sources=native_fixture();actual=actual.replace('#define INST_DATA(cl,g) ((struct PropGData*)(g)->SpecialInfo)', '#include "propunits.h"\n'+EXTRA.replace('ASL_DATA',structure((SRC/'asl_intern.h').read_text(),'AslPropData')))
 actual=actual.replace(fn(actual,'DoSuperMethodA'),SUPER).replace('static void DoMethodA(Object*g,Msg m){assert(0);}',LIST_ADAPTER)
 actual=actual.replace('return in->MethodID==GM_GOACTIVE?PropGClass__GM_GOACTIVE(&cl,g,in):PropGClass__GM_HANDLEINPUT(&cl,g,in);','return in->MethodID==GM_GOACTIVE?AslProp__GM_GOACTIVE(&asl_class,g,in):AslProp__GM_HANDLEINPUT(&asl_class,g,in);')
 actual+=GET_ADAPTER;source=(SRC/'propclass.c').read_text()
 for name in ['SnapInputEndpoint','AslProp__GM_GOACTIVE','AslProp__GM_HANDLEINPUT']:actual+=fn(source,name)+'\n'
 # Exercise the exact client guard against the native capture bit. The
 # complete generation publication/retry graph is complementary aslscale proof.
 publication=(SRC/'aslpublish.c').read_text();guard=re.search(r'            struct PropInfo \*info = \(\(struct Gadget \*\)old->object\)->SpecialInfo;\n            if \(info && \(info->Flags & KNOBHIT\)\) return FALSE;',publication).group(0)
 actual+='struct GuardObjectRecord{Object*object;};static BOOL prop_adoptable(struct Gadget*g){struct GuardObjectRecord record={g},*old=&record;'+guard+'return TRUE;}\n'
 actual+=units_driver[:units_driver.index('int main(void)')]+EVENT_HELPER+DRIVER
 (a.out/'actual.c').write_text(actual);cmd=['clang','-std=c11','-g','-O1','-fsanitize=address,undefined,unsigned-integer-overflow','-fno-sanitize-recover=all','-I',str(SRC),str(a.out/'actual.c'),'-o',str(a.out/'actual')]
 p=subprocess.run(cmd,capture_output=True,text=True);(a.out/'compile.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stderr
 p=subprocess.run([cmd[-1]],capture_output=True,text=True);(a.out/'run.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stdout+p.stderr;print(p.stdout,end='')
 rejected=[];activebody=fn(source,'AslProp__GM_GOACTIVE');inputbody=fn(source,'AslProp__GM_HANDLEINPUT')
 mutations={
  'drop_verified_release':(inputbody,inputbody.replace('if (result & GMR_VERIFY)','if (FALSE)')),
  'drop_verified_page':(activebody,activebody.replace('if (result & GMR_VERIFY)','if (FALSE)')),
  'snap_interim':(inputbody,inputbody.replace('if (result & GMR_VERIFY)','if (TRUE)')),
  'snap_interior':('if (top != 0 && (total <= visible || top != total - visible)) return;', 'if (FALSE) return;'),
  'drop_client_hold_guard':('if (info && (info->Flags & KNOBHIT)) return FALSE;','if (FALSE) return FALSE;'),
  'native_floor_pot':('(((ULONG)top * MAXPOT) + hidden / 2)','((ULONG)top * MAXPOT)'),
  'old_absolute_basis':('value = (QUAD)axis->anchorPot +\n            (((QUAD)pointer - axis->anchor) * MAXPOT) / track;', 'value = (QUAD)pointer * MAXPOT / track;'),
  'no_native_movement':(fn(actual,'HandlePropMouseMove'),fn(actual,'HandlePropMouseMove').split('{',1)[0]+'{ return; }'),
 }
 for name,(old,new) in mutations.items():
  assert old in actual and old!=new,name
  path=a.out/(name+'.c');path.write_text(actual.replace(old,new,1));mutcmd=cmd.copy();mutcmd[-3]=str(path);mutcmd[-1]=str(a.out/name)
  p=subprocess.run(mutcmd,capture_output=True,text=True);assert not p.returncode,p.stderr
  p=subprocess.run([mutcmd[-1]],capture_output=True,text=True);(a.out/(name+'.log')).write_text(p.stdout+p.stderr);assert p.returncode,'survived '+name;rejected.append(name)
 print('Rejected combined input mutations:',','.join(rejected))
 sources.update({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [SRC/'propclass.c',SRC/'asl.conf',SRC/'propunits.h',SRC/'aslpublish.c',SRC/'asl_intern.h',Path(__file__).resolve(),Path(__file__).with_name('aslpropunits_host.py')]})
 (a.out/'result.json').write_text(json.dumps({'status':'PASS_BOUNDED','scope':__doc__,'command':cmd,'rejected_mutations':rejected,'native_source_policy':'All native methods from this same checkout via propfixture.py; no selective math replacement','common_capture_geometry':'PASS_BOUNDED_ACTUAL_FUNCTIONS','input_cases':324,'held_geometry_events':896,'generation_adoption_guard':'Complementary aslscale_host.py actual hold guard/retry; no full OS execution','sources':sources},indent=2)+'\n')
if __name__=='__main__':main()
