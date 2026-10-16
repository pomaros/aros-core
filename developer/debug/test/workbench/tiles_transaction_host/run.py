#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,pathlib,subprocess,json,hashlib
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args();out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=False);(out/'fs').mkdir();hashes={}
for f in [root/'workbench/system/Wanderer/tile_transaction.c',root/'workbench/system/Wanderer/tile_transaction.h',here/'test.c']:
 b=f.read_bytes();(out/f.name).write_bytes(b);hashes[str(f.relative_to(root))]=hashlib.sha256(b).hexdigest()
cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I'+str(out),str(out/'tile_transaction.c'),str(out/'test.c'),'-o',str(out/'test')]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/'build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
r=subprocess.run([str(out/'test'),str(out/'fs')],capture_output=True,text=True,timeout=30);(out/'run.log').write_text(r.stdout+r.stderr);assert not r.returncode and r.stdout=='tiles-transaction-host PASS\n' and not r.stderr,r
# Negative control: removing rollback must break the actual forward-failure tests.
mutant=(out/'tile_transaction.c').read_text().replace('while(plan->completed) {','while(0) {')
assert mutant!=(out/'tile_transaction.c').read_text()
(out/'no-rollback.c').write_text(mutant)
mutcmd=[str(out/'no-rollback.c') if x==str(out/'tile_transaction.c') else str(out/'no-rollback') if x==str(out/'test') else x for x in cmd]
r=subprocess.run(mutcmd,capture_output=True,text=True,timeout=60);(out/'mutant-build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
(out/'mutant-fs').mkdir()
r=subprocess.run([str(out/'no-rollback'),str(out/'mutant-fs')],capture_output=True,text=True,timeout=30);(out/'mutant-run.log').write_text(r.stdout+r.stderr);assert r.returncode,'removed rollback unexpectedly passed'
# Exercise the actual worker receipt/join functions, not a boolean model.
worker=root/'workbench/system/Wanderer/tile_drop.c'
b=worker.read_bytes();(out/'tile_drop.c').write_bytes(b);hashes[str(worker.relative_to(root))]=hashlib.sha256(b).hexdigest()
source=b.decode();functions=[]
# The actual queue declarations and drain/poll/finish implementation.
begin=source.index('#define TILE_DROP_PENDING');end=source.index('\nstatic int exists',begin)
functions.append(source[begin:end])
for marker in ('static int launch(const struct TileDropRequest *input)\n{',
               'int TileDropStart(const struct TileDropRequest *input)\n{'):
 assert source.count(marker)==1
 begin=source.index(marker);end=source.index('{',begin)+1;depth=1
 while depth:
  if source[end]=='{':depth+=1
  elif source[end]=='}':depth-=1
  end+=1
 functions.append(source[begin:end])
(out/'lifetime-functions.inc').write_text('\n'.join(functions)+'\n')
icons=root/'workbench/system/Wanderer/Classes/iconlist.c'
b=icons.read_bytes();hashes[str(icons.relative_to(root))]=hashlib.sha256(b).hexdigest()
(out/'iconlist.c').write_bytes(b);icontext=b.decode('latin1')
start=icontext.index('                    struct IconEntry *moved;')
end=icontext.index('\n                }',start)
snippet=icontext[start:end];assert snippet.count('IconList_QueueTileTransaction(')==1
(out/'selection.inc').write_text(snippet+'\n')
b=(here/'lifetime.c').read_bytes();(out/'lifetime.c').write_bytes(b);hashes[str((here/'lifetime.c').relative_to(root))]=hashlib.sha256(b).hexdigest()
life=['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(out/'lifetime.c'),'-o',str(out/'lifetime')]
r=subprocess.run(life,capture_output=True,text=True,timeout=60);(out/'lifetime-build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
r=subprocess.run([str(out/'lifetime')],capture_output=True,text=True,timeout=30);(out/'lifetime-run.log').write_text(r.stdout+r.stderr);assert not r.returncode and r.stdout=='tiles-worker-lifetime PASS\n' and not r.stderr,r
# Negative control: the unsafe busy-only quit must fail the early-clear case.
inc=(out/'lifetime-functions.inc').read_text();inc=inc.replace('void TileDropFinish(void)\n{','void TileDropFinish(void)\n{\n    if(!busy)return;')
(out/'lifetime-functions.inc').write_text(inc)
(out/'lifetime-functions-mutant.inc').write_text(inc)
mutlife=life[:-1]+[str(out/'lifetime-mutant')]
r=subprocess.run(mutlife,capture_output=True,text=True,timeout=60);assert not r.returncode,r.stderr
r=subprocess.run([str(out/'lifetime-mutant')],capture_output=True,text=True,timeout=30);(out/'lifetime-mutant.log').write_text(r.stdout+r.stderr);assert r.returncode,'busy-only quit unexpectedly passed'
(out/'lifetime-functions.inc').write_text('\n'.join(functions)+'\n')
# Original admission policy loses every subsequent synchronous selection.
inc='\n'.join(functions)+'\n'
inc=inc.replace('int TileDropStart(const struct TileDropRequest *input)\n{',
 'int TileDropStart(const struct TileDropRequest *input)\n{\n    if(busy||childID)return 0;')
(out/'lifetime-functions.inc').write_text(inc)
mutlife=life[:-1]+[str(out/'queue-mutant')]
r=subprocess.run(mutlife,capture_output=True,text=True,timeout=60);assert not r.returncode,r.stderr
r=subprocess.run([str(out/'queue-mutant')],capture_output=True,text=True,timeout=30);(out/'queue-mutant.log').write_text(r.stdout+r.stderr);assert r.returncode,'old busy rejection unexpectedly passed'
(out/'lifetime-functions.inc').write_text('\n'.join(functions)+'\n')
(out/'result.json').write_text(json.dumps({'result':'PASS','hashes':hashes,'command':cmd,'negative_control':['removed rollback rejected','busy-only quit rejected','old multi-selection busy rejection rejected'],'lifetime_command':life},indent=2)+'\n');print('tiles-transaction-host PASS; removed-rollback negative control rejected')
