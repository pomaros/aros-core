#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);out=pathlib.Path(p.parse_args().output).resolve();out.mkdir(parents=True,exist_ok=False)
source=root/'workbench/system/Tiles/main.c';s=source.read_text();(out/'main.c').write_text(s)
def function(marker):
 assert s.count(marker)==1,marker
 begin=s.index(marker);end=s.index('{',begin)+1;depth=1
 while depth:
  if s[end]=='{':depth+=1
  elif s[end]=='}':depth-=1
  end+=1
 return s[begin:end]
functions='\n'.join(function(m) for m in ('static BOOL publish_tile_cache(','static struct BitMap *snapshot_lift_bitmap(','static int draw_tile('))
(out/'functions.inc').write_text(functions)
begin=s.index('        if(tile->publishPending &&');end=s.index('        if (!tile->config.env[0]',begin);(out/'retry.inc').write_text(s[begin:end])
begin=s.index('    ULONG snapshotInterval=DRAW_INTERVAL;');end=s.index('    for(unsigned g=0;g<service->groupCount;++g)if(service->groups[g].config.show',begin);(out/'refresh.inc').write_text(s[begin:end])
grid=root/'workbench/system/Wanderer/Classes/iconlist.c';g=grid.read_text(encoding='latin1');(out/'iconlist.c').write_bytes(grid.read_bytes());begin=g.index('    /* List view must not inherit cell slots');end=g.index('    ForeachNode(&data->icld_IconList, entry) {',begin);gridbody=g[begin:end];(out/'grid.inc').write_text(gridbody)
results={}
def run(name,file,expected):
 cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(out/file),'-o',str(out/name)]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=30);(out/(name+'.run.log')).write_text(r.stdout+r.stderr)
 if expected:assert not r.returncode and r.stdout==expected and not r.stderr,r
 else:assert r.returncode,'mutant unexpectedly passed'
 results[name]={'command':cmd,'exit':r.returncode}
for f in ('fixture.c','grid.c'):(out/f).write_bytes((here/f).read_bytes())
run('cache','fixture.c','tiles-lift-cache PASS\n');run('grid','grid.c','tiles-list-transition PASS\n')
mut=functions.replace('        publish_tile_cache(tile,now);','        if(!publish_tile_cache(tile,now))return FALSE;');assert mut!=functions;(out/'functions.inc').write_text(mut);(out/'functions-mutant.inc').write_text(mut);run('deferred-render-mutant','fixture.c',None);(out/'functions.inc').write_text(functions)
mut=gridbody.replace('    /* List view','    if (data->icld_DisplayFlags & ICONLIST_DISP_MODELIST)return FALSE;\n    /* List view');(out/'grid.inc').write_text(mut);(out/'grid-mutant.inc').write_text(mut);run('stale-grid-mutant','grid.c',None);(out/'grid.inc').write_text(gridbody)
retry=(out/'retry.inc').read_text();mut=retry.replace(' &&\n           !TileSourcePending(&service->sources,tile)','');assert mut!=retry
# Keep the mock referenced so -Werror validates the mutation behavior, not an unused-function warning.
mut='(void)TileSourcePending;(void)service;\n'+mut
(out/'retry.inc').write_text(mut);(out/'retry-mutant.inc').write_text(mut);run('pending-provider-mutant','fixture.c',None);(out/'retry.inc').write_text(retry)
refresh=(out/'refresh.inc').read_text();mut=refresh.replace('(now-service->snapshotTick)>=DRAW_INTERVAL','(now-service->snapshotTick)>=snapshotInterval');assert mut!=refresh
(out/'refresh.inc').write_text(mut);(out/'refresh-mutant.inc').write_text(mut);run('foreign-fast-copy-mutant','fixture.c',None);(out/'refresh.inc').write_text(refresh)
(out/'result.json').write_text(json.dumps({'result':'PASS','main_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'iconlist_sha256':hashlib.sha256(grid.read_bytes()).hexdigest(),'runs':results},indent=2)+'\n');print('tiles-lift-cache + list-transition PASS; four negative controls rejected')
