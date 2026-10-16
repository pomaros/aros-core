#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);out=pathlib.Path(p.parse_args().output).resolve();out.mkdir(parents=True,exist_ok=False)
source=root/'workbench/system/Wanderer/Classes/iconlist.c';s=source.read_text(encoding='latin1');(out/'iconlist.c').write_bytes(source.read_bytes())
def extract(marker):
 assert s.count(marker)==1,marker
 start=s.index(marker);end=s.index('{',start)+1;depth=1
 while depth:
  if s[end]=='{':depth+=1
  elif s[end]=='}':depth-=1
  end+=1
 return s[start:end]
(out/'functions.inc').write_text('\n'.join(extract(m) for m in ('static void IconList_GetIconImageOffsets(', 'static void IconList_GetIconAreaRectangle(Object *obj,', 'static BOOL IconList_AnchorGroupSurface(')))
start=s.index('            struct IconTileRequest anchor={ICONTILE_ANCHOR,0,0,NULL,0,0};');end=s.index('            struct IconTileBounds surface',start);body=s[start:end];(out/'sample.inc').write_text(body)
header=root/'workbench/system/Wanderer/Classes/iconlist_visibility.h';(out/header.name).write_bytes(header.read_bytes());(out/'fixture.c').write_bytes((here/'fixture.c').read_bytes());results={}
functions=(out/'functions.inc').read_text()
for name in ('production','label-centering-mutant','anchor-offset-mutant'):
 (out/'sample.inc').write_text(body);(out/'functions.inc').write_text(functions)
 if name=='label-centering-mutant':
  mutant=body.replace('IconList_GetIconAreaRectangle(obj,data,entry,&area);','(void)area;entry->ie_AreaWidth=size.width;entry->ie_AreaHeight=size.height;');assert mutant!=body;(out/'sample.inc').write_text(mutant);(out/'sample-mutant.inc').write_text(mutant)
 if name=='anchor-offset-mutant':
  mutant=functions.replace('originX+offsetX,originY+offsetY','originX+offsetX*0,originY+offsetY*0');assert mutant!=functions;(out/'functions.inc').write_text(mutant);(out/'functions-mutant.inc').write_text(mutant)
 cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-fsanitize=address,undefined',str(out/'fixture.c'),'-o',str(out/name)]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=30);(out/(name+'.run.log')).write_text(r.stdout+r.stderr)
 if name=='production':assert not r.returncode and r.stdout=='tiles-group-geometry PASS\n' and not r.stderr,r
 else:assert r.returncode,'old mismatched query unexpectedly passed'
 results[name]={'command':cmd,'exit':r.returncode}
(out/'sample.inc').write_text(body);(out/'functions.inc').write_text(functions);(out/'result.json').write_text(json.dumps({'result':'PASS','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'runs':results},indent=2)+'\n');print('tiles-group-geometry PASS; wrong-area and anchor-offset mutants rejected')
