#!/usr/bin/env python3
# SPDX-License-Identifier: APL-1.1
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
"""Test the detached aware-geometry calculator; no WindowFit/runtime claim."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

TEST = r'''
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "awaregeometry.h"
static struct AwareGeometry initial(void) {
    struct AwareGeometry c;
    LONG v[]={100,120,640,240,80,80,-1,-1};
    for(unsigned i=0;i<AG_COUNT;i++) c.axis[i]=(struct AwareGeometryValue){v[i],100};
    return c;
}
int main(void) {
    struct AwareGeometry c=initial(), saved=c;
    struct AwareGeometryFrame f={4,20,4,4};
    struct AwareGeometryPlan p;
    unsigned phases[]={100,200,400,100};
    for(unsigned cycle=0;cycle<100;cycle++) for(unsigned i=0;i<4;i++) {
        assert(aware_geometry_prepare(&c,phases[i],1440,885,&f,&p));
        if(phases[i]==100) assert(p.left==100&&p.top==120&&p.width==640&&p.height==240);
        if(phases[i]==400) assert(p.left==0&&p.width==1440&&p.top==0&&p.height==885);
        assert(!memcmp(&saved,&c,sizeof c));
    }
    for(unsigned old=100;old<=400;old+=25) for(unsigned next=100;next<=400;next+=25) {
        struct AwareGeometryValue v={503,old}; LONG got;
        assert(aware_geometry_value(&v,next,&got));
        assert(got==(LONG)roundl(503.0L*next/old));
        assert(aware_geometry_value(&v,old,&got)&&got==503);
        v.value=-503;assert(aware_geometry_value(&v,next,&got));
        assert(got==(LONG)roundl(-503.0L*next/old));
    }
    /* User changed only x and width at 200%; all other anchors survive. */
    c.axis[AG_LEFT]=(struct AwareGeometryValue){203,200};
    c.axis[AG_WIDTH]=(struct AwareGeometryValue){503,200};
    for(unsigned i=0;i<100;i++) {
        assert(aware_geometry_prepare(&c,100,2560,1585,&f,&p));
        assert(p.left==102&&p.width==252&&p.top==120&&p.height==240);
        assert(aware_geometry_prepare(&c,200,2560,1570,&f,&p));
        assert(p.left==203&&p.width==503&&p.top==240&&p.height==480);
    }
    /* Proposed frame, not the old live frame, sets the border minimum. */
    c=initial();c.axis[AG_WIDTH].value=10;c.axis[AG_MINWIDTH].value=0;
    f.left=12;f.right=12;
    assert(aware_geometry_prepare(&c,100,2560,1585,&f,&p)&&p.width==25&&p.minwidth==25);
    /* Failure does not change the output or the canonical state. */
    saved=c;struct AwareGeometryPlan oldp=p;
    f.top=400;f.bottom=400;
    assert(!aware_geometry_prepare(&c,200,2560,600,&f,&p));
    assert(!memcmp(&p,&oldp,sizeof p)&&!memcmp(&c,&saved,sizeof c));
    f=(struct AwareGeometryFrame){4,20,4,4};c=initial();
    c.axis[AG_MINWIDTH].value=800;
    assert(!aware_geometry_prepare(&c,200,1440,885,&f,&p));
    assert(!memcmp(&p,&oldp,sizeof p));
    c=initial();c.axis[AG_MAXWIDTH].value=700;
    assert(aware_geometry_prepare(&c,200,2560,1585,&f,&p)&&p.maxwidth==1400);
    c.axis[AG_WIDTH].value=900;
    assert(aware_geometry_prepare(&c,200,2560,1585,&f,&p)&&p.width==1400);
    unsigned bad[]={0,99,101,401,UINT_MAX};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++) {
        oldp=p;assert(!aware_geometry_prepare(&c,bad[i],2560,1585,&f,&p));
        assert(!memcmp(&p,&oldp,sizeof p));
    }
    c=initial();c.axis[AG_MINWIDTH].scale=0;
    assert(!aware_geometry_prepare(&c,100,2560,1585,&f,&p));
    c=initial();c.axis[AG_MAXWIDTH].scale=0;
    assert(!aware_geometry_prepare(&c,100,2560,1585,&f,&p));
    c=initial();f.left=INT_MAX;f.right=INT_MAX;
    assert(!aware_geometry_prepare(&c,100,2560,1585,&f,&p));
    struct AwareGeometryValue large={INT_MAX,100};LONG result=123;
    assert(!aware_geometry_value(&large,400,&result)&&result==123);
    large.value=INT_MIN;assert(!aware_geometry_value(&large,400,&result)&&result==123);
    puts("PASS actual detached aware geometry: canonical reset, thirteen scales, odd physical anchors, proposed borders, min/max, failure immutability, wide overflow");
}
'''

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[4])
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    header=args.source/'rom/intuition/awaregeometry.h'
    actual=header.read_text()
    (out/'exec').mkdir(exist_ok=True)
    (out/'exec/types.h').write_text('#include <stdint.h>\ntypedef int32_t LONG; typedef uint32_t ULONG; typedef uint16_t UWORD; typedef int64_t QUAD; typedef int BOOL;\n#define TRUE 1\n#define FALSE 0\n')
    (out/'fixture.c').write_text(TEST)
    variants={'actual':actual,
        'ignore-reference-scale':actual.replace('reference->scale / 2) / reference->scale','100 / 2) / 100'),
        'truncate-odd-pixels':actual.replace('reference->scale / 2','0'),
        'ignore-proposed-border':actual.replace('(QUAD)frame->left + frame->right + 1','1'),
        'ignore-minimum':actual.replace('values[AG_MINWIDTH] > minwidth ? values[AG_MINWIDTH] : minwidth','minwidth'),
        'ignore-maximum':actual.replace('if (extent > maximum) extent = maximum;',''),
        'write-on-failure':actual.replace('    struct AwareGeometryPlan plan;','    result->left = 777;\n    struct AwareGeometryPlan plan;'),
        'overflow-narrow-product':actual.replace('(QUAD)reference->value * scale','reference->value * (LONG)scale')}
    results=[]
    for name,text in variants.items():
        assert name=='actual' or text!=actual,name
        (out/'awaregeometry.h').write_text(text)
        cmd=['clang','-O1','-g','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',str(out),str(out/'fixture.c'),'-lm','-o',str(out/name)]
        subprocess.run(cmd,check=True)
        run=subprocess.run([str(out/name)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        (out/(name+'.log')).write_text(run.stdout)
        if (run.returncode==0)!=(name=='actual'):raise AssertionError(name+' wrong verdict')
        print('PASS' if name=='actual' else 'REJECT',name)
        results.append({'variant':name,'exit':run.returncode,'command':cmd})
    (out/'awaregeometry.h').write_text(actual)
    (out/'result.json').write_text(json.dumps({'scope':'detached calculator only; not WindowFit/Intuition integration or rendering','source':str(header.resolve()),'source_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'results':results},indent=2)+'\n')

if __name__=='__main__':main()
