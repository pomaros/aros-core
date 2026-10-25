#!/usr/bin/env python3
# SPDX-License-Identifier: APL-1.1
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
"""Actual pre-window legacy factor getter and engine formula, host boundaries.
No pixel rendering, driver or real screen lifecycle is exercised here.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess

def extract(text,name):
    match=re.search(r'(?:static\s+)?(?:inline\s+)?UWORD '+name+r'\([^;]*?\)\s*\{',text,re.S)
    if not match:raise ValueError(name)
    start=match.start();pos=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
    return text[start:pos]

PREFIX=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef uint16_t UWORD;typedef int BOOL;typedef uintptr_t IPTR;
struct BitMap { unsigned hidd; };
struct Screen { struct { struct BitMap *BitMap; } RastPort; };
struct IntScreen { struct Screen Screen;UWORD Scale; };
#define IS(x) ((struct IntScreen *)(x))
#define IS_HIDD_BM(bitmap) ((bitmap)->hidd)
#define SA_LegacyScaleFactor 163
'''
TEST=r'''
static void test(void) {
 struct BitMap hidd={1},planar={0};struct IntScreen screen={0};
 assert(ScreenLegacyScaleFactor(NULL)==1);assert(ScreenLegacyScaleFactor(&screen.Screen)==1);
 for(unsigned backend=0;backend<2;backend++)for(unsigned percentage=0;percentage<=450;percentage++){
  screen.Screen.RastPort.BitMap=backend?&hidd:&planar;screen.Scale=percentage;
  unsigned expected=percentage<150?1:percentage<250?2:percentage<350?3:4;
  if(!backend)expected=1;
  assert(ScreenLegacyScaleFactor(&screen.Screen)==expected);
  assert(query(&screen)==expected);
  screen.Scale=100;assert(ScreenLegacyScaleFactorForScale(&screen.Screen,percentage)==expected);screen.Scale=percentage;
  assert(child_factor(&screen.Screen,0)==expected);
  assert(child_factor(&screen.Screen,1)==1);
 }
 puts("PASS actual legacy factor getter:902 backend/percentage combinations; nearest ties upward; non-HIDD/NULL factor1; aware children factor1");
}
int main(void){test();return 0;}
'''

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[4]);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    paths=['rom/intuition/windowcomposition.c','rom/graphics/scalecompose.h','rom/intuition/screenclass.c']
    sources={p:(args.source/p).read_text(encoding='latin1') for p in paths}
    factor=extract(sources[paths[1]],'ScaleComposeFactor')
    engine=extract(sources[paths[0]],'ScreenLegacyScaleFactorForScale')+'\n'+extract(sources[paths[0]],'ScreenLegacyScaleFactor')+'\n'+extract(sources[paths[0]],'child_factor')
    query=sources[paths[2]];a=query.index('    case SA_LegacyScaleFactor:');b=query.index('        break;',a)+len('        break;')
    getter='static IPTR query(struct IntScreen *screen){IPTR result=0;struct { IPTR *opg_Storage; } storage={&result},*msg=&storage;switch(SA_LegacyScaleFactor){\n'+query[a:b]+'\n}return result;}\n'
    mutations={'nearest-to-floor':factor.replace('(percent + 50) / 100','percent / 100'),'ignore-nonhidd':engine.replace(' || !IS_HIDD_BM(screen->RastPort.BitMap)','')}
    for name in ['production',*mutations]:
        code=PREFIX+(mutations[name] if name=='nearest-to-floor' else factor)+'\n'+(mutations[name] if name=='ignore-nonhidd' else engine)+'\n'+getter+TEST
        source=args.output/(name+'.c');source.write_text(code);binary=args.output/name
        cmd=['clang','-O1','-g','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(source),'-o',str(binary)]
        subprocess.run(cmd,check=True)
        result=subprocess.run([str(binary.resolve())],capture_output=True,text=True);(args.output/(name+'.log')).write_text(result.stdout+result.stderr)
        if name=='production':
            if result.returncode:raise RuntimeError(result.stdout+result.stderr)
            print(result.stdout.strip())
        elif not result.returncode:raise RuntimeError('survived '+name)
        else:print('REJECT',name)
    (args.output/'sources.json').write_text(json.dumps({p:hashlib.sha256((args.source/p).read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
if __name__=='__main__':main()
