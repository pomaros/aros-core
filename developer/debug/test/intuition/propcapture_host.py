#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Exercise native capture with real geometry/input/knob functions, no OS runtime.
Only rendering, notification, lock and object-dispatch adapters are supplied.
Native dispatch retains exact event points. A synthetic logical event cannot
reveal subpixel phase after its input frame changes; its first ambiguous delta
is conservatively rebased, then ordinary supplied-coordinate movement resumes.
Run: propcapture_host.py OUTPUT_DIRECTORY [--mutant NAME]
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess
ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);ap.add_argument('--mutant',default='');args=ap.parse_args()
core=Path(__file__).resolve().parents[4];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
import sys
sys.dont_write_bytecode=True
from propfixture import native_fixture
c,sources=native_fixture(args.mutant)
sources[str(Path(__file__).resolve())]=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
c+=r'''
static unsigned cases;
static UWORD pot(struct Gadget*g,BOOL horizontal){struct PropInfo*p=g->SpecialInfo;return horizontal?p->HorizPot:p->VertPot;}
static void input(struct Gadget*g,struct GadgetInfo*gi,struct IntuitionBase*base,LONG x,LONG y,ULONG method,BOOL native){
 LONG lx,ly;assert(WindowCompositionMouse(gi->gi_Window,x,y,&lx,&ly));gi->gi_Window->MouseX=lx;gi->gi_Window->MouseY=ly;
 GetGadgetDomain(g,gi->gi_Screen,gi->gi_Window,gi->gi_Requester,&gi->gi_Domain);
 struct InputEvent ie={IECLASS_RAWMOUSE,method==3?SELECTUP:IECODE_NOBUTTON,0};BOOL reuse=FALSE;if(method==3)method=GM_HANDLEINPUT;
 struct PropInputPosition position={gi->gi_Screen,x,y};
 if(native==2){struct gpInput in={.gpi_GInfo=gi};SetGPIMouseCoords(&in,g);
  if(method==GM_GOACTIVE)HandlePropSelectDown(g,gi->gi_Window,gi->gi_Requester,in.gpi_Mouse.X,in.gpi_Mouse.Y,&position,base);
  else if(ie.ie_Code==SELECTUP)HandlePropSelectUp(g,gi->gi_Window,gi->gi_Requester,base);
  else HandlePropMouseMove(g,gi->gi_Window,gi->gi_Requester,in.gpi_Mouse.X,in.gpi_Mouse.Y,&position,base);
 }else if(native)DoGPInputAt(gi,g,&ie,method,&reuse,&position,base);else DoGPInput(gi,g,&ie,method,&reuse,base);
 assert(!base->prop_input.message);cases++;
}
int main(void){struct Screen screen={.Width=32767,.Height=32767};struct WindowComposition composition={.transform={31,23,1}};struct IntWindow iw={.W={.WScreen=&screen,.LeftEdge=31,.TopEdge=23,.Width=500,.Height=500,.BorderLeft=7,.BorderTop=13,.BorderRight=9,.BorderBottom=11}};struct Window*w=&iw.W;
 struct PropGData data={0};struct Gadget g={.SpecialInfo=&data};struct Requester req={.LeftEdge=17,.TopEdge=29,.Width=450,.Height=450};struct GadgetInfo gi={.gi_Window=w,.gi_Screen=&screen};struct IIHData iih={0};struct Interrupt interrupt={&iih};struct IntuitionBase base={.InputHandler=&interrupt};Class cl={(IPTR)&base};
 for(unsigned hidden=0;hidden<=65535;hidden++)for(unsigned sample=0;sample<=32;sample++){
  UWORD visible=hidden<65535?1:0,total=hidden+visible,top=(uint32_t)hidden*sample/32,body,potvalue;
  FindScrollerValues(total,visible,top,0,&body,&potvalue);assert(FindScrollerTop(total,visible,potvalue)==top);
  if(!top)assert(!potvalue);if(hidden&&top==hidden)assert(potvalue==65535);
 }
 for(unsigned native=0;native<3;native++)for(unsigned horizontal=0;horizontal<2;horizontal++)for(unsigned domain=0;domain<4;domain++)for(unsigned legacy=0;legacy<2;legacy++){
  iw.Composition=legacy?&composition:NULL;composition.transform=(struct ScaleComposeTransform){31,23,1};w->LeftEdge=31;w->TopEdge=23;w->Width=500;w->Height=500;req.Width=450;req.Height=450;
  w->Flags=domain==1?WFLG_GIMMEZEROZERO:0;gi.gi_Requester=domain==3?&req:NULL;
  g.GadgetType=domain==2?GTYP_GZZGADGET:domain==3?GTYP_REQGADGET:GTYP_CUSTOMGADGET;
  g.Flags=horizontal?GFLG_RELWIDTH|GFLG_RELBOTTOM:GFLG_RELHEIGHT|GFLG_RELRIGHT;
  g.LeftEdge=horizontal?20:-30;g.TopEdge=horizontal?-30:20;g.Width=horizontal?-100:20;g.Height=horizontal?20:-100;
  data.propinfo=(struct PropInfo){.Flags=(horizontal?FREEHORIZ:FREEVERT)|AUTOKNOB|PROPBORDERLESS,.HorizPot=32767,.VertPot=32767,.HorizBody=1000,.VertBody=1000};
  struct IBox box,domainbox;GetWinGadgetIBox(&g,&screen,w,gi.gi_Requester,&box);GetGadgetDomain(&g,&screen,w,gi.gi_Requester,&domainbox);struct BBox knob;CalcBBox(w,gi.gi_Requester,&g,&knob);CalcKnobSize(&g,&knob);
  LONG x=31+domainbox.Left+knob.Left+2,y=23+domainbox.Top+knob.Top+2;
  input(&g,&gi,&base,x,y,GM_GOACTIVE,native);if(!(data.propinfo.Flags&KNOBHIT)) fprintf(stderr,"miss native%u h%u domain%u legacy%u x%ld y%ld\n",native,horizontal,domain,legacy,(long)x,(long)y);assert(data.propinfo.Flags&KNOBHIT);assert(base.prop_drag.gadget==&g);UWORD initial=pot(&g,horizontal);
  /* A resized track must rebase an already displaced rational anchor. */
  input(&g,&gi,&base,x+(horizontal?5:0),y+(horizontal?0:5),GM_HANDLEINPUT,native);UWORD held=pot(&g,horizontal);assert(held>initial);
  if(horizontal){w->Width+=100;req.Width+=100;}else{w->Height+=100;req.Height+=100;}
  input(&g,&gi,&base,x+(horizontal?5:0),y+(horizontal?0:5),GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==held);
  if(horizontal){w->Width-=100;req.Width-=100;}else{w->Height-=100;req.Height-=100;}
  input(&g,&gi,&base,x+(horizontal?5:0),y+(horizontal?0:5),GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==held);
  input(&g,&gi,&base,x,y,GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==initial);
  for(unsigned percent=100;percent<=400;percent+=25){
   UWORD factor=legacy?(percent+50)/100:1;
   if(legacy)composition.transform=(struct ScaleComposeTransform){31+(LONG)(percent/25),23+(LONG)(percent/25),factor};
   else{w->Width=500*percent/100;w->Height=500*percent/100;w->LeftEdge=31+percent/25;w->TopEdge=23+percent/25;req.Width=450*percent/100;req.Height=450*percent/100;}
   CalcBBox(w,gi.gi_Requester,&g,&knob);CalcKnobSize(&g,&knob); /* rendering overwrites caches */
   input(&g,&gi,&base,x,y,GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==initial);
   input(&g,&gi,&base,x+(horizontal?factor:0),y+(horizontal?0:factor),GM_HANDLEINPUT,native);assert(pot(&g,horizontal)>initial);
   input(&g,&gi,&base,x,y,GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==initial);
  }
  /* Nonaligned 4->3->2->1 phase changes, including capture at factor3 true
   * point10 (represented9) then factor2. Native must retain exact point. */
  for(int factor=4;legacy&&factor>=1;factor--){composition.transform.factor=factor;input(&g,&gi,&base,x,y,GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==initial);}
  if(horizontal)data.propinfo.HorizPot=17000;else data.propinfo.VertPot=17000;
  input(&g,&gi,&base,x,y,GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==17000);
  if(horizontal)data.propinfo.HorizBody=5000;else data.propinfo.VertBody=5000;
  input(&g,&gi,&base,x,y,GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==17000);
  input(&g,&gi,&base,x+(horizontal?10000:0),y+(horizontal?0:10000),GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==65535);
  input(&g,&gi,&base,x+(horizontal?9999:0),y+(horizontal?0:9999),GM_HANDLEINPUT,native);assert(pot(&g,horizontal)<65535);
  input(&g,&gi,&base,x-(horizontal?10000:0),y-(horizontal?0:10000),GM_HANDLEINPUT,native);assert(pot(&g,horizontal)==0);
  input(&g,&gi,&base,x,y,3,native);assert(!base.prop_drag.gadget);assert(!(data.propinfo.Flags&KNOBHIT));
  data.propinfo.Flags|=KNOBHIT;base.prop_drag.gadget=&g;struct gpGoInactive inactive={&gi,0};PropGClass__GM_GOINACTIVE(&cl,&g,&inactive);assert(!base.prop_drag.gadget);
 }
 /* Capture at a nonzero unknown logical-pixel phase (true point10 in
  * factor3 cell9..11), then change frame to factor2 and1. */
 for(unsigned native=0;native<2;native++){
  iw.Composition=&composition;w->Width=500;w->Height=500;w->Flags=0;gi.gi_Requester=NULL;
  g.GadgetType=GTYP_CUSTOMGADGET;g.Flags=0;g.LeftEdge=0;g.TopEdge=0;g.Width=100;g.Height=400;
  composition.transform=(struct ScaleComposeTransform){0,-600,3};
  data.propinfo=(struct PropInfo){.Flags=FREEVERT|AUTOKNOB|PROPBORDERLESS,.VertPot=32767,.VertBody=1000};
  struct BBox knob;CalcBBox(w,NULL,&g,&knob);CalcKnobSize(&g,&knob);LONG y=-600+(knob.Top+2)*3+1;
  input(&g,&gi,&base,10,y,GM_GOACTIVE,native);assert(data.propinfo.Flags&KNOBHIT);UWORD old=data.propinfo.VertPot;
  composition.transform.factor=2;input(&g,&gi,&base,10,y,GM_HANDLEINPUT,native);assert(data.propinfo.VertPot==old);
  composition.transform.factor=1;input(&g,&gi,&base,10,y,GM_HANDLEINPUT,native);assert(data.propinfo.VertPot==old);
  /* Move window and pointer together: the supplied local coordinate is
   * identical, but the real event carries +1 pixel and must be processed. */
  composition.transform.y++;input(&g,&gi,&base,10,y+1,GM_HANDLEINPUT,native);assert(data.propinfo.VertPot>old);
  input(&g,&gi,&base,10,y+1,3,native);
 }
 /* A native geometry transition plus a real first-event +1 delta cannot be
  * dropped. Supply exact event coordinates despite stale global screen mouse. */
 iw.Composition=&composition;composition.transform=(struct ScaleComposeTransform){0,0,3};g.TopEdge=0;g.Height=400;
 data.propinfo=(struct PropInfo){.Flags=FREEVERT|AUTOKNOB|PROPBORDERLESS,.VertPot=32767,.VertBody=1000};
 struct BBox knob;CalcBBox(w,NULL,&g,&knob);CalcKnobSize(&g,&knob);LONG y=(knob.Top+2)*3+1;
 input(&g,&gi,&base,10,y,GM_GOACTIVE,TRUE);UWORD old=data.propinfo.VertPot;
 composition.transform.factor=2;g.Height=800;input(&g,&gi,&base,10,y+1,GM_HANDLEINPUT,TRUE);assert(data.propinfo.VertPot>old&&data.propinfo.VertPot<old+100);
 input(&g,&gi,&base,10,y,GM_HANDLEINPUT,TRUE);assert(data.propinfo.VertPot==old);
 input(&g,&gi,&base,10,y,3,TRUE);
 /* Actual class release/UpdateTop, using the same normalized PGA model as
  * owner OM_SET, must not lose a unit without any motion. */
 composition.transform=(struct ScaleComposeTransform){0,0,1};g.Height=400;
 data.total=65535;data.visible=1;data.top=32768;UWORD body,potvalue;
 FindScrollerValues(data.total,data.visible,data.top,0,&body,&potvalue);
 data.propinfo=(struct PropInfo){.Flags=FREEVERT|AUTOKNOB|PROPBORDERLESS,.VertPot=potvalue,.VertBody=body};
 CalcBBox(w,NULL,&g,&knob);CalcKnobSize(&g,&knob);y=knob.Top+2;
 input(&g,&gi,&base,10,y,GM_GOACTIVE,TRUE);input(&g,&gi,&base,10,y,3,TRUE);assert(data.top==32768);
 /* Clearing input capture also covers no-event activation and an owner
  * clearing KNOBHIT; enabling a second freedom uses the previous event point. */
 base.prop_drag.gadget=&g;struct gpInput noevent={.gpi_GInfo=&gi};PropGClass__GM_GOACTIVE(&cl,&g,&noevent);assert(!base.prop_drag.gadget);
 base.prop_drag.gadget=&g;data.propinfo.Flags&=~KNOBHIT;HandlePropMouseMove(&g,w,NULL,0,0,NULL,&base);assert(!base.prop_drag.gadget);
 /* Exact native snapshot is in the target window screen, even if the
  * active screen differs. No callback can replace the copied point. */
 struct Screen other={.LeftEdge=-10,.TopEdge=7,.sx=2,.sy=3};w->WScreen=&other;base.MouseX=105;base.MouseY=77;
 struct PropInputPosition target=button_point(w,&screen,&base);assert(target.screen==&other&&target.x==62&&target.y==18);
 base.MouseX=-999;base.MouseY=-999;assert(target.x==62&&target.y==18);
 struct InputEvent normalized={0};normalized.ie_X=111;normalized.ie_Y=-33;target=movement_point(target,&normalized);assert(target.x==65&&target.y==-18);w->WScreen=&screen;
 /* Invalid snapshot refuses without changing caller outputs. */
 iw.Composition=&composition;composition.transform.factor=0;LONG ox=101,oy=102;UWORD factor=103;assert(!WindowCompositionInputFrame(w,&ox,&oy,&factor));assert(ox==101&&oy==102&&factor==103);
 assert(nested_dispatches==nested_checks);
 printf("PASS %u actual native/synthetic dispatches; %u provenance nested identity/coordinate checks; axes, actual GZZ/requester/border domains, 100..400/reset, exact endpoints/owner changes; 2162688 normalized roundtrips\n",cases,nested_checks);
}
'''
(out/'actual.c').write_text(c)
cmd=['clang','-std=c11','-g','-O1','-fsanitize=address,undefined,unsigned-integer-overflow',str(out/'actual.c'),'-o',str(out/'actual')]
p=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stderr
p=subprocess.run([str(out/'actual')],capture_output=True,text=True);(out/'run.log').write_text(p.stdout+p.stderr)
status='PASS' if not p.returncode else 'FAIL'
(out/'result.json').write_text(json.dumps(dict(status=status,mutant=args.mutant,sources=sources,command=cmd,scope='actual prop helpers/class dispatch, native point snapshot statements, SetGPIMouseCoords, gadget domains, composition inverse/frame; graphics/locks/notification adapters; full event-loop/native unverified',artifact_sha256={n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in ['actual.c','run.log']}),indent=2)+'\n')
assert not p.returncode,p.stdout+p.stderr
print(p.stdout,end='')
