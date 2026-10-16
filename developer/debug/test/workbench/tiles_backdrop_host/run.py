#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);out=pathlib.Path(p.parse_args().output).resolve();out.mkdir(parents=True,exist_ok=False)
a=root/'workbench/system/Wanderer/appobjects.c';s=a.read_text(encoding='latin1');marker='BOOL AppIcon_TileDefinitionRegistered(';assert s.count(marker)==1
begin=s.index(marker);end=s.index('\n}',begin)+2;adapter=s[begin:end];(out/'adapter.inc').write_text(adapter)
b=root/'workbench/system/Wanderer/iconwindow_volumelist.c';s=b.read_text(encoding='latin1');marker='                        {\n                            char *tmpbdrp_file = NULL;';assert s.count(marker)==1
begin=s.index(marker);end=s.index('                        bdrp_namepart = FilePart(bdrp_fullfile);',begin);body=s[begin:end];(out/'parse.inc').write_text(body)
(out/'fixture.c').write_bytes((here/'fixture.c').read_bytes());results={}
for name in ('production','no-suppression'):
 if name!='production':
  mutant=body.replace('if (AppIcon_TileDefinitionRegistered(bdrp_fullfile))','if (AppIcon_TileDefinitionRegistered(bdrp_fullfile) && 0)');assert mutant!=body;(out/'parse.inc').write_text(mutant);(out/'parse-mutant.inc').write_text(mutant)
 cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(out/'fixture.c'),'-o',str(out/name)]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=30);(out/(name+'.run.log')).write_text(r.stdout+r.stderr)
 if name=='production':assert not r.returncode and r.stdout=='tiles-backdrop-transition PASS\n' and not r.stderr,r
 else:assert r.returncode,'old duplicate behavior unexpectedly passed'
 results[name]={'command':cmd,'exit':r.returncode}
(out/'parse.inc').write_text(body)
for path in (a,b):(out/path.name).write_bytes(path.read_bytes())
(out/'result.json').write_text(json.dumps({'result':'PASS','hashes':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (a,b)},'runs':results},indent=2)+'\n');print('tiles-backdrop-transition PASS; duplicate behavior mutant rejected')
