#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL) Version 1.1.
"""Actual theme shade-line geometry versus independent enlarged100% pixels.
DShadeLine color/gradient blending is replaced by a rectangular pen writer;
this checks actual bounds/order/stroke coverage, not the gradient algorithm.
"""
import argparse,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[4]);a=p.parse_args();s=(a.source/'workbench/c/Decoration/windowdecorclass.c').read_text()
def block(start):
 left=s.index('{',start);depth=1;end=left+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
shade=block(s.index('static inline void ShadeLine('))
start=s.index('        /* Shading borders */');region=block(s.index('        if (data->dc->WinFrameStyle > 0)',start))
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int32_t LONG;typedef uint32_t ULONG;typedef uint16_t UWORD;typedef int BOOL;
struct Rectangle{LONG MinX,MinY,MaxX,MaxY;};struct DecorImage{int unused;};
struct RastPort{unsigned char pixels[160*160];LONG width,height,bl,br,bb,bt;};
struct Config{LONG WinFrameStyle,UseGradients;UWORD ShadeValues_d,ShadeValues_l,ShadeValues_m;};struct Data{struct Config*dc;};
static void DShadeLine(LONG pen,BOOL tc,BOOL gradients,struct RastPort*rp,struct DecorImage*di,ULONG color,UWORD factor,UWORD offy,const struct Rectangle*r){(void)tc;(void)gradients;(void)di;(void)color;(void)factor;(void)offy;assert(r->MinX>=0&&r->MinY>=rp->bt&&r->MaxX<rp->width&&r->MaxY<rp->height);assert(r->MinX<=r->MaxX&&r->MinY<=r->MaxY);for(LONG y=r->MinY;y<=r->MaxY;y++)for(LONG x=r->MinX;x<=r->MaxX;x++){assert(x<rp->bl||x>=rp->width-rp->br||y>=rp->height-rp->bb);rp->pixels[y*160+x]=pen;}}
'''
render='''static void render(struct RastPort*rp,LONG scale,LONG border){LONG unit=(scale+50)/100,ww=(40*scale+50)/100,wh=(40*scale+50)/100,bt=(14*scale+50)/100,bl=(border*scale+50)/100,br=bl,bb=bl;LONG dpen=1,lpen=2,mpen=3;BOOL tc=1;ULONG bc=0;struct DecorImage*di=NULL;struct Config cfg={.WinFrameStyle=1};struct Data inst={&cfg},*data=&inst;memset(rp,0,sizeof(*rp));rp->width=ww;rp->height=wh;rp->bl=bl;rp->br=br;rp->bb=bb;rp->bt=bt;'''+region+'}\n'
main=r'''
int main(void){unsigned cases=0;struct RastPort baseline,actual;for(LONG border=0;border<=9;border++){render(&baseline,100,border);for(LONG scale=100;scale<=400;scale+=25){render(&actual,scale,border);if(scale%100==0){LONG f=scale/100;for(LONG y=0;y<actual.height;y++)for(LONG x=0;x<actual.width;x++)assert(actual.pixels[y*160+x]==baseline.pixels[(y/f)*160+x/f]);}cases++;}}printf("PASS actual theme shading bounds/order: %u cases all13scales, integer enlarged100%% whole-pixel oracle, client/window bounds and corner coverage\n",cases);return 0;}
'''
variants=[('actual',shade),('drop-long-axis-end',shade.replace('i : thickness - 1','i : 0')),('single-pixel-stroke',shade.replace('i < thickness','i < 1'))]
with tempfile.TemporaryDirectory(prefix='window-theme-border-') as tmp:
 tmp=pathlib.Path(tmp)
 for name,body in variants:
  c=tmp/(name+'.c');exe=tmp/name;c.write_text(pre+body+render+main);subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if name=='actual':
   if q.returncode:raise SystemExit(q.stderr)
   print(q.stdout,end='')
  elif not q.returncode:raise SystemExit('escaped mutation '+name)
  else:print('REJECTED '+name)
