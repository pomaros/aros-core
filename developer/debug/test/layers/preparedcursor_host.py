#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Sanitize actual Layers cursor-owner lookup; pixel binder is a boundary model."""
from pathlib import Path
import argparse
import subprocess
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
source=(ROOT/'rom/hyperlayers/preparedbatch.c').read_text()
start=source.index('AROS_LH3(struct HIDD_PixelMap *, PrepareLayerGeometryCursor,')
lt=(ROOT/'compiler/include/graphics/layertransaction.h').read_text()
import re
lgr='\n'.join(l.split('/*')[0] for l in lt.splitlines() if re.match(r'#define\s+LGR_',l))+'\n'
start=source.index('{',start);end=source.index('\nAROS_LH1(BOOL, SealLayerGeometryBatch,',start)
body=source[start:end]
preamble=r'''
#define NULL ((void*)0)
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef int BOOL;
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define BATCH_PREPARING 0
struct BitMap {int value;};struct HIDD_CursorState {int value;};struct LayersBase {int value;};
struct HIDD_PixelMap {int value;};
struct PreparedPixels {struct HIDD_PixelMap map;struct HIDD_CursorState *bound;BOOL writable;};
struct PreparedStack {struct BitMap *bitmap;struct PreparedPixels *pixels;};
struct LayerGeometryBatch {struct PreparedStack *stacks;ULONG stack_count;int state;};
struct Layer;
/* The recorded check; the real recorder is covered by preparedbatch_host. */
static ULONG recorded;static long recorded_value;
static BOOL batch_refuse(struct LayerGeometryBatch *b,ULONG check,struct Layer *l,long v0,long v1,long v2,long v3)
{(void)b;(void)l;(void)v1;(void)v2;(void)v3;recorded=check;recorded_value=v0;return 0;}
static unsigned calls;
static struct HIDD_PixelMap *PreparePixelsCursor(struct PreparedPixels *p,struct HIDD_CursorState *c) {
 ++calls;if(!p||!p->writable||p->bound)return NULL;p->bound=c;return &p->map;
}
static struct HIDD_PixelMap *PrepareLayerGeometryCursor(struct LayerGeometryBatch *batch,struct BitMap *bitmap,struct HIDD_CursorState *proposed,struct LayersBase *LayersBase)
'''
tests=r'''
#define REFUSED(c) (recorded==(c)&&(recorded=0,1))
int main(void) {
 struct BitMap root={1},private={2},unknown={3};struct HIDD_CursorState next={0};
 struct PreparedPixels a={.writable=1},b={.writable=1};
 struct PreparedStack stacks[3]={{&root,&a},{&private,&b},{&root,&b}};
 struct LayerGeometryBatch batch={stacks,2,BATCH_PREPARING};
 assert(!PrepareLayerGeometryCursor(NULL,&root,&next,NULL)&&!recorded);
 assert(!PrepareLayerGeometryCursor(&batch,NULL,&next,NULL)&&REFUSED(LGR_ARGUMENTS));
 assert(!PrepareLayerGeometryCursor(&batch,&root,NULL,NULL)&&REFUSED(LGR_ARGUMENTS));
 assert(!PrepareLayerGeometryCursor(&batch,&unknown,&next,NULL)&&REFUSED(LGR_CURSOR_OWNER));
 assert(!PrepareLayerGeometryCursor(&batch,&private,&next,NULL)&&!b.bound&&REFUSED(LGR_CURSOR_OWNER));
 batch.stack_count=0;assert(!PrepareLayerGeometryCursor(&batch,&root,&next,NULL));
 batch.stack_count=3;assert(!PrepareLayerGeometryCursor(&batch,&root,&next,NULL)&&!a.bound&&!b.bound&&!calls);
 assert(recorded==LGR_CURSOR_OWNER&&recorded_value==2&&REFUSED(LGR_CURSOR_OWNER));
 batch.stack_count=2;batch.state=1;assert(!PrepareLayerGeometryCursor(&batch,&root,&next,NULL)&&!calls&&REFUSED(LGR_STATE));
 batch.state=0;stacks[0].pixels=NULL;assert(!PrepareLayerGeometryCursor(&batch,&root,&next,NULL)&&!calls&&REFUSED(LGR_CURSOR_LEASE));stacks[0].pixels=&a;
 a.writable=0;assert(!PrepareLayerGeometryCursor(&batch,&root,&next,NULL)&&!a.bound&&REFUSED(LGR_CURSOR_LEASE));a.writable=1;
 assert(PrepareLayerGeometryCursor(&batch,&root,&next,NULL)==&a.map&&a.bound==&next&&!b.bound&&!recorded);
 assert(!PrepareLayerGeometryCursor(&batch,&root,&next,NULL)&&a.bound==&next&&REFUSED(LGR_CURSOR_LEASE));
 puts("PASS actual batch cursor lookup: physical owner only, unknown/private/ambiguous/missing/sealed/read-only/duplicate refused");
}
'''
mutants={
 'private-owner':body.replace('if (batch->stacks[0].bitmap != bitmap)\n    { batch_refuse(batch, LGR_CURSOR_OWNER, NULL, 0, 0, 0, 0); return NULL; }',''),
 'ambiguous-owner':body.replace('if (owner) { batch_refuse(batch, LGR_CURSOR_OWNER, NULL, i, 0, 0, 0); return NULL; }','/* missing ambiguity refusal */'),
 'lease-unrecorded':body.replace('if (!map) batch_refuse(batch, LGR_CURSOR_LEASE, NULL, 1, 0, 0, 0);',''),
 'sealed-owner':body.replace('batch->state != BATCH_PREPARING','0'),
 'wrong-bitmap':body.replace('if (batch->stacks[i].bitmap == bitmap)','if (1)'),
}
for name,code in [('production',body),*mutants.items()]:
 assert name=='production' or code!=body,name
 c=args.output/(name+'.c');exe=args.output/name;c.write_text(lgr+preamble+code+tests)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True);(args.output/(name+'.log')).write_text(r.stdout+r.stderr)
 assert (r.returncode==0)==(name=='production'),name
 print(r.stdout if name=='production' else 'REJECT '+name,end='' if name=='production' else '\n')
