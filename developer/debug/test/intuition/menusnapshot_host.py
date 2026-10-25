#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual menu preparation under controlled scale publication scheduling.

The scheduler/font/Exec adapters are bounded; no real SMP or pixels claimed.
Runs the original129 body, missing-lock/mixed-snapshot negatives, and the real
fixed body with a publication request inside capture or completed before it.
"""
from pathlib import Path
import argparse,hashlib,json,os,runpy,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[4]

def units():
 ns=runpy.run_path(str(Path(__file__).with_name('menuscale_host.py')),run_name='snapshot_adapter')
 base=ns['build_source']()
 old_lock='static ULONG LockIBase(ULONG flags) { assert(!flags&&!iblock&&!lilock);iblock=1;return 1; }'
 new_lock=r'''
static int publicationTiming,publicationRequests,publicationCompleted;
static struct Window *publicationWindow;static struct Screen *publicationScreen;
static const struct ScreenRenderContext *publicationB;
static struct Image *publicationImages[3];
static void publish_B(void){assert(!iblock&&!lilock);publicationScreen->context=publicationB;
 publicationWindow->Checkmark=publicationImages[0];publicationWindow->AmigaKey=publicationImages[1];publicationWindow->SubMenuImage=publicationImages[2];
 publicationScreen->Width=1920;publicationScreen->Height=1080;publicationCompleted++;}
static ULONG LockIBase(ULONG flags){assert(!flags&&!iblock&&!lilock);
 if(publicationTiming==1&&!publicationCompleted)publish_B();iblock=1;return 1;}
'''
 old_unlock='static void UnlockIBase(ULONG token) { assert(token==1&&iblock&&!lilock);iblock=0; }'
 new_unlock='static void UnlockIBase(ULONG token){assert(token==1&&iblock&&!lilock);iblock=0;if(publicationTiming==2&&publicationRequests&&!publicationCompleted)publish_B();}'
 old_current='static const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *s) { assert(iblock&&lilock);return s->context; }'
 new_current='''static const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *s){assert(iblock&&lilock);
 if(publicationTiming==2&&!publicationCompleted)publicationRequests++;return s->context;}'''
 old_get='static BOOL __attribute__((unused)) GetAttr(ULONG attr,Object *o,IPTR *value) { assert(attr==SA_RenderContext); *value=(IPTR)((struct Screen*)o)->context; return TRUE; }'
 new_get='''static BOOL __attribute__((unused)) GetAttr(ULONG attr,Object *o,IPTR *value){assert(attr==SA_RenderContext);
 if(publicationTiming==2&&!publicationCompleted){assert(!iblock&&!lilock);publicationScreen->context=publicationB;publicationRequests++;}
 *value=(IPTR)((struct Screen*)o)->context;return TRUE;}'''
 for old,new in [(old_lock,new_lock),(old_unlock,new_unlock),(old_current,new_current),(old_get,new_get)]:
  assert old in base,old;base=base.replace(old,new)
 main=r'''
static void schedule_case(int timing,BOOL custom){
 struct TextFont base={.tf_Message.mn_Node.ln_Name="base.font",.tf_YSize=13,.tf_XSize=7},fa=base,fb=base;fa.tf_YSize=26;fb.tf_YSize=39;
 struct Image lc={.Width=10,.Height=13},lk={.Width=14,.Height=14},la={.Width=7,.Height=13};
 struct Image ac={.Width=20,.Height=26},ak={.Width=28,.Height=28},aa={.Width=14,.Height=26};
 struct Image bc={.Width=30,.Height=39},bk={.Width=42,.Height=42},ba={.Width=21,.Height=39};
 struct IntuitionCustomize lcustom={&la},acustom={&aa},bcustom={&ba};UWORD pens[8]={0};
 struct Screen screen={.Width=2560,.Height=1600};struct IntDrawInfo logical={.dri_Screen=&screen,.dri_Font=&base,.dri_CheckMark=&lc,.dri_AmigaKey=&lk,.dri_Pens=pens,.dri_Customize=&lcustom};
 struct IntDrawInfo pa=logical,pb=logical;pa.dri_Font=&fa;pa.dri_CheckMark=&ac;pa.dri_AmigaKey=&ak;pa.dri_Customize=&acustom;
 pb.dri_Font=&fb;pb.dri_CheckMark=&bc;pb.dri_AmigaKey=&bk;pb.dri_Customize=&bcustom;
 struct ScreenRenderContext a={.src_Scale=200,.src_LegacyScaleFactor=2,.src_PhysicalBarHeight=34,.src_LogicalDrawInfo=(struct DrawInfo*)&logical,.src_PhysicalDrawInfo=(struct DrawInfo*)&pa};
 struct ScreenRenderContext b=a;b.src_Scale=300;b.src_LegacyScaleFactor=3;b.src_PhysicalBarHeight=51;b.src_PhysicalDrawInfo=(struct DrawInfo*)&pb;
 struct IntuiText text={.IText="Stock"};struct TextAttr foreign={"foreign.font",11,0,0};if(custom)text.ITextFont=&foreign;
 struct MenuItem item={.Width=100,.Height=16,.Flags=ITEMTEXT|ITEMENABLED,.ItemFill=&text};struct Menu menu={.MenuName="Console",.Width=60,.Height=13,.FirstItem=&item};
 struct Window w={.ScaleAware=TRUE,.MenuRenderLogical=TRUE,.MenuRenderContext=&a,.WScreen=&screen,.MenuStrip=&menu,.Checkmark=&ac,.AmigaKey=&ak,.SubMenuImage=&aa};struct IntuitionBase ib={0};screen.context=&a;
 publicationTiming=timing;publicationCompleted=publicationRequests=0;publicationWindow=&w;publicationScreen=&screen;publicationB=&b;publicationImages[0]=&bc;publicationImages[1]=&bk;publicationImages[2]=&ba;
 if(timing==1)publish_B(); /* The publisher completed before entry. */
 struct MenuScaleGeneration*g=PrepareMenuScale(&w,&ib);assert(g&&!iblock&&!lilock);
 if(timing==2&&publicationRequests&&!publicationCompleted)publish_B();
 printf("schedule=%d custom=%d captured-scale=%u sharp=%d logical=%d factor=%u image=%d\n",timing,custom,g->view.context->src_Scale,g->view.sharp,g->view.logical,g->view.factor,g->view.checkmark->Width);fflush(stdout);
 const struct ScreenRenderContext*chosen=timing==1?&b:&a;
 assert(g->view.context==chosen&&g->view.sharp==!custom&&g->view.logical==custom);
 assert(g->view.factor==(custom?chosen->src_LegacyScaleFactor:1));
 assert(g->view.checkmark==(timing==1?&bc:&ac));
 assert(g->view.width==(timing==1?1920:2560)/g->view.factor&&g->view.height==(timing==1?1080:1600)/g->view.factor);
 assert(g->sourceScale==100);DisposeMenuScale(g);assert_clean();
 if(timing==2)assert(publicationRequests==1&&publicationCompleted==1&&screen.context==&b);
 if(timing==1)assert(publicationCompleted==1);
 publicationTiming=0;publicationWindow=NULL;publicationScreen=NULL;publicationB=NULL;
}
int main(void){for(int custom=0;custom<2;custom++)for(int timing=0;timing<3;timing++)schedule_case(timing,custom);
 puts("PASS actual prepare snapshot:6 stock/custom scheduling cases, entirelyA/B target+images+extents, source logical100 preserved, all fallible work outside locks");return 0;}
'''
 base=base.replace('int main(void){exercise();return 0;}',main)
 # The original129 source has no lock capture and reproduces the same request
 # inside its real GetAttr boundary. Never used as a production fallback.
 original=subprocess.check_output(['git','-C',str(ROOT),'show','f8e2e8008454:rom/intuition/menuscale.c'],text=True)
 original=__import__('re').sub(r'^#include[^\n]*\n','',original,flags=__import__('re').M)
 current=__import__('re').sub(r'^#include[^\n]*\n','',(ROOT/'rom/intuition/menuscale.c').read_text(),flags=__import__('re').M)
 assert current in base
 variants={'production':base,'original129':base.replace(current,original),
 'missing-IBase':base.replace('lock = LockIBase(0);','lock = 0;'),
 'mixed-image-read':base.replace('snapshot.checkmark != sourceinfo->dri_CheckMark','IW(window)->Checkmark != sourceinfo->dri_CheckMark').replace('snapshot.checkmark != context->src_PhysicalDrawInfo->dri_CheckMark','IW(window)->Checkmark != context->src_PhysicalDrawInfo->dri_CheckMark'),
 'live-extents-after-capture':base.replace('snapshot.width / generation->view.factor','window->WScreen->Width / generation->view.factor'),
 'context-resample-after-capture':base.replace('context = snapshot.target;','context = snapshot.screen->context;'),
 'provider-callback-under-lock':base.replace('snapshot->height = screen->Height;', 'snapshot->height = screen->Height;struct mdpQueryCapabilities forbidden={MDM_QUERYCAPABILITIES};DoMethodA(snapshot->provider,(Msg)&forbidden);')}
 return variants

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path);args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='menusnapshot-') as temp:
  out=args.output or Path(temp);out.mkdir(parents=True,exist_ok=True);records=[];variants=units()
  for name,body in variants.items():
   assert name=='production' or body!=variants['production'],name
   c=out/(name+'.c');exe=out/name;c.write_text(body)
   cmd=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)]
   r=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'.compile.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
   r=subprocess.run([str(exe)],capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr);assert(r.returncode==0)==(name=='production'),name+':'+r.stderr
   records.append({'name':name,'returncode':r.returncode,'source_sha256':hashlib.sha256(c.read_bytes()).hexdigest()});print(r.stdout.strip() if name=='production' else 'REJECT '+name)
  (out/'results.json').write_text(json.dumps(records,indent=2)+'\n')
if __name__=='__main__':main()
