#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
"""Compile production layer ordering bodies with a recording list fixture."""
import argparse,pathlib,re,subprocess,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4];hashes={}
def source(name):
 s=(root/name).read_text(encoding='latin1');hashes[name]=hashlib.sha256((root/name).read_bytes()).hexdigest();return s
unit=(here/'fixture.h').read_text()
for file,sig in [('upfrontlayer.c','int upfront(struct Layer *l)'),('behindlayer.c','int behind(struct Layer *l)'),('movelayerinfrontof.c','int move(struct Layer *layer_to_move,struct Layer *other_layer)')]:
 s=source('rom/hyperlayers/'+file);body=s.split('AROS_LIBFUNC_INIT',1)[1].split('AROS_LIBFUNC_EXIT',1)[0];unit+='\n'+sig+'{'+body+'}\n'
for file,name in [('createupfrontlayertaglist.c','front_priority'),('createbehindlayertaglist.c','back_priority')]:
 s=source('rom/hyperlayers/'+file);expr=re.search(r'int priority = .*?;',s,re.S).group();unit+='int '+name+'(int flags){'+expr+'return priority;}\n'
s=source('rom/intuition/openwindow.c');validation=s[s.index('    if (tileoverlay &&'):];validation=validation[:validation.index('goto exit;')+10];unit+='int allowed(int tileoverlay,int parentl,int flags){struct {int Flags;} nw={flags};'+validation+'\nreturn 1;exit:return 0;}\n'
unit+=(here/'test.c').read_text();(a.output/'unit.c').write_text(unit)
cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-g',str(a.output/'unit.c'),'-o',str(a.output/'test')];(a.output/'command.json').write_text(json.dumps(cmd))
r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(a.output/'compile.log').write_text(r.stdout+r.stderr)
if r.returncode:raise SystemExit(r.stderr)
r=subprocess.run([str(a.output/'test')],capture_output=True,text=True,timeout=20);(a.output/'run.log').write_text(r.stdout+r.stderr)
if r.returncode or r.stderr or r.stdout!='PASS protected layer priority\n':raise SystemExit(r.stdout+r.stderr)
(a.output/'result.json').write_text(json.dumps({'result':'PASS','hashes':hashes,'scope':'Production priority selection, reorder bodies and window validation; mocked list mutation/graphics, no guest layer proof'},indent=2)+'\n');print(r.stdout,end='')
