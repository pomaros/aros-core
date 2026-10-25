#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual shipped metrics plus Menu124 glyph placement, ASan/UBSan boundaries.

--menu-root selects the exact consumer tree, independently of the producer.
No theme draw/backend, complete Screen constructor or native pixels implied.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[4]
def function(text,name):
    start=re.search(r'^static BOOL '+re.escape(name)+r'\(',text,re.M).start()
    begin=text.index('{',start);depth=1;end=begin+1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'
HEAD=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <limits.h>
typedef int BOOL;typedef int32_t LONG;typedef uint32_t ULONG;
#define TRUE 1
#define FALSE 0
struct DecorConfig {LONG BarHeight,SBarHeight,LeftBorder,RightBorder,BottomBorder,SBarGadPost_s;};
struct ScreenScaleMetrics {LONG BarHeight,BarVBorder,BarHBorder,MenuVBorder,MenuHBorder,WBorTop,WBorLeft,WBorRight,WBorBottom,DepthRightInset,DepthTop;};
struct TextFont {LONG tf_YSize;};struct RP {struct TextFont *Font;};
struct View {LONG barheight,barvborder;};struct Handler {struct View *view;};
'''
TEST=r'''
int main(void){
 struct DecorConfig config={18,18,3,4,5,2};unsigned cases=0;
 for(unsigned face=0;face<2;face++)for(unsigned phase=0;phase<14;phase++){
  ULONG scale=phase==13?100:100+25*phase,base=face?26:13,font=(base*scale+50)/100;
  LONG vertical=(scale+50)/100;struct ScreenScaleMetrics m={.BarVBorder=vertical};
  assert(scrdecor_scale_metrics(&config,base,font,scale,&m));
  assert(m.BarHBorder==(LONG)((scale+50)/100));
  assert(m.BarHeight+1>=(LONG)font+2*vertical);
  if(!face)assert(m.BarHeight+1==(LONG)((18*scale+50)/100));
  assert(m.WBorLeft==3&&m.WBorRight==4&&m.WBorBottom==5);
  assert(m.WBorTop==(face?1:4));
  struct View view={m.BarHeight,vertical};struct Handler h={&view};struct TextFont f={(LONG)font};struct RP rp={&f};
  LONG y=actual_title_y(&h,&rp);assert(y>=0&&y+font<=(ULONG)m.BarHeight+1);
  if(face&&scale==200)assert(m.BarHeight==55&&y==3&&m.BarHBorder==2);
  cases++;
 }
 struct ScreenScaleMetrics edge={.BarVBorder=1};
 assert(scrdecor_scale_metrics(&config,63,126,200,&edge)&&edge.BarHeight==127);
 edge.BarVBorder=1;assert(!scrdecor_scale_metrics(&config,63,127,200,&edge));
 edge.BarVBorder=2;assert(!scrdecor_scale_metrics(&config,62,125,200,&edge));
 edge.BarVBorder=0;assert(scrdecor_scale_metrics(&config,63,127,200,&edge)&&edge.BarHeight==127);
 edge.BarVBorder=INT_MAX;assert(!scrdecor_scale_metrics(&config,13,26,200,&edge));
 edge.BarVBorder=-1;assert(!scrdecor_scale_metrics(&config,13,26,200,&edge));
 assert(!scrdecor_scale_metrics(&config,13,26,200,NULL));
 printf("PASS actual metric/Menu124 placement: %u face/scale/reset cases, final127/128 boundaries and invalid padding\n",cases);return 0;}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--menu-root',type=Path,required=True);ap.add_argument('--output',type=Path);ap.add_argument('--baseline',default='73a52bb1fe');args=ap.parse_args()
 producer='workbench/c/Decoration/screendecorclass.c';source=(ROOT/producer).read_text();body=function(source,'scrdecor_scale_metrics')
 menu=(args.menu_root/'rom/intuition/menutask.c').read_text()
 placement=next(l.strip() for l in menu.splitlines() if 'y = mhd->view->barvborder' in l)
 actual='static LONG actual_title_y(struct Handler*mhd,struct RP*rp){LONG y;'+placement+'return y;}\n'
 baseline=subprocess.check_output(['git','-C',str(ROOT),'show',args.baseline+':'+producer],text=True)
 variants={'production':body,'original':function(baseline,'scrdecor_scale_metrics'),
  'constant-horizontal':body.replace('metrics->BarHBorder = (scale + 50) / 100;','metrics->BarHBorder = 1;'),
  'dropped-vertical-padding':body.replace('minimum = (LONG)fontheight + 2 * metrics->BarVBorder - 1;','minimum = (LONG)fontheight - 1;'),
  'final-overflow-accepted':body.replace('if (metrics->BarHeight < 0 || metrics->BarHeight > 127) return FALSE;','if (FALSE) return FALSE;'),
  'invalid-padding-accepted':body.replace('!metrics || metrics->BarVBorder < 0 || metrics->BarVBorder > 127','!metrics')}
 with tempfile.TemporaryDirectory(prefix='screenpadding-') as tmp:
  out=args.output or Path(tmp);out.mkdir(parents=True,exist_ok=True);records=[]
  for name,code in variants.items():
   assert name=='production' or code!=body,name
   c=out/(name+'.c');exe=out/name;c.write_text(HEAD+code+actual+TEST)
   cmd=[os.environ.get('CC','clang'),'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)]
   p=subprocess.run(cmd,capture_output=True,text=True);assert p.returncode==0,p.stderr
   r=subprocess.run([str(exe)],capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr)
   assert(r.returncode==0)==(name=='production'),name+':'+r.stderr
   records.append({'name':name,'returncode':r.returncode,'unit_sha256':hashlib.sha256(c.read_bytes()).hexdigest()});print(r.stdout.strip() if name=='production' else 'REJECT '+name)
  (out/'results.json').write_text(json.dumps({'producer_sha256':hashlib.sha256(source.encode()).hexdigest(),'menu_source_sha256':hashlib.sha256(menu.encode()).hexdigest(),'baseline':args.baseline,'cases':records},indent=2)+'\n')
if __name__=='__main__':main()
