#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,pathlib,subprocess,hashlib,json
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4];src=root/'workbench/system/Tiles'
p=argparse.ArgumentParser();p.add_argument('--output',required=True);args=p.parse_args()
out=pathlib.Path(args.output);out.mkdir(parents=True,exist_ok=False)
source=(src/'main.c').read_text();start='static void load_path(';end='static void scan_catalog('
assert source.count(start)==source.count(end)==1
function=source[source.index(start):source.index(end)]
fixture=(here/'fixture.c').read_text();assert fixture.count('/* EXTRACT_LOAD_PATH */')==1
unit=fixture.replace('/* EXTRACT_LOAD_PATH */',function)
(out/'main.c').write_text(source)
for f in ['tiles_group.c','tiles_group.h']:(out/f).write_bytes((src/f).read_bytes())
results={}
for name in ['production','missing_preflight','ignore_enumeration_failure']:
 code=unit
 if name=='missing_preflight':
  old='++candidates>TILE_LIMIT-active';assert code.count(old)==1;code=code.replace(old,'++candidates>4000+active*0')
 if name=='ignore_enumeration_failure':
  old='IoErr()!=ERROR_NO_MORE_ENTRIES || !Examine(lock,fib)';assert code.count(old)==1;code=code.replace(old,'!Examine(lock,fib)')
 path=out/(name+'.c');path.write_text(code)
 cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I'+str(out),str(path),str(out/'tiles_group.c'),'-o',str(out/name)]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=20);(out/(name+'.run.log')).write_text(r.stdout+r.stderr)
 if name=='production':assert r.returncode==0 and r.stdout.endswith('tiles-group-load-host PASS\n') and not r.stderr,r
 else:assert r.returncode!=0 and 'Assertion failed:' in r.stderr,r
 results[name]={'exit':r.returncode,'command':cmd,'sha256':hashlib.sha256(code.encode()).hexdigest()}
(out/'result.json').write_text(json.dumps({'result':'PASS','main_sha256':hashlib.sha256(source.encode()).hexdigest(),'runs':results},indent=2)+'\n')
print('tiles-group-load-host PASS: production and two assertion-failing mutation controls')
