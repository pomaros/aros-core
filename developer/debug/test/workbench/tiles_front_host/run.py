#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,pathlib,subprocess,hashlib,json
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);args=p.parse_args()
out=pathlib.Path(args.output);out.mkdir(parents=True,exist_ok=False)
s=(root/'workbench/system/Tiles/main.c').read_text()
def extract(start,end):
 assert s.count(start)==1,(start,s.count(start))
 a=s.index(start);return s[a:s.index(end,a)]
parts={
 'HELPER':extract('static BOOL lift_screen_front(', 'static BOOL lift_tiles('),
 'OPEN':extract('    if (service->lift) { drop_tiles(service);return TRUE; }','    if(service->trace)TILE_LOG("[TILEL] begin'),
 'ROUTE':extract('            while(TileInputNext(service->input,&input)) {','            while(service->lift && TileLiftPump'),
 'TICK':extract('    if(service->lift && !lift_screen_front(service))drop_tiles(service);','    TileSourcesExpire(')}
unit=(here/'fixture.c').read_text()
for name,part in parts.items():assert unit.count('/* EXTRACT_'+name+' */')==1;unit=unit.replace('/* EXTRACT_'+name+' */',part)
(out/'main.c').write_text(s);results={}
for name in ['production','ignore_screen_identity','omit_queued_guard','omit_tick_guard']:
 code=unit
 replacements={'ignore_screen_identity':('return front;','return front || 1;'),'omit_queued_guard':('if(!lift_screen_front(service)) {drop_tiles(service);close_panels(service);continue;}','if(0) {drop_tiles(service);close_panels(service);continue;}'),'omit_tick_guard':('if(service->lift && !lift_screen_front(service))drop_tiles(service);','if(0)drop_tiles(service);')}
 if name in replacements:
  old,new=replacements[name];assert code.count(old)==1;code=code.replace(old,new)
 path=out/(name+'.c');path.write_text(code)
 cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(path),'-o',str(out/name)]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=20);(out/(name+'.run.log')).write_text(r.stdout+r.stderr)
 if name=='production':assert r.returncode==0 and r.stdout=='tiles-front-host PASS\n' and not r.stderr,r
 else:assert r.returncode!=0 and 'Assertion failed:' in r.stderr,r
 results[name]={'exit':r.returncode,'command':cmd,'sha256':hashlib.sha256(code.encode()).hexdigest()}
(out/'result.json').write_text(json.dumps({'result':'PASS','source_sha256':hashlib.sha256(s.encode()).hexdigest(),'runs':results},indent=2)+'\n');print('tiles-front-host PASS: actual helper/routing and three negative controls')
