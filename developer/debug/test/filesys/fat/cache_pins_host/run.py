#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Actual FAT transfer bodies at modeled cache and disk boundaries."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[5];source=root/'rom/filesys/fat/ops.c';text=source.read_text()
def function(name):
 start=text.index(name);return text[start:text.index('\n}\n',start)+3]
body='\n'.join(function(name) for name in ['static void ReleaseFileCacheBlock(', 'LONG OpRead(', 'LONG OpWrite(']);host=(here/'host.c').read_text();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
controls={'production':body,'read-release-lost':body.replace('    ReleaseFileCacheBlock(&lock->ioh);','    /* release omitted */',1)}
write=body.index('LONG OpWrite(');controls['write-release-lost']=body[:write]+body[write:].replace('    ReleaseFileCacheBlock(&lock->ioh);','    /* release omitted */',1)
for name,code in controls.items():
 path=out/(name+'.c');path.write_text(host.replace('/* ACTUAL_OPERATIONS */',code));command=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g','-fsanitize=address,undefined',str(path),'-o',str(out/name)]
 compiled=subprocess.run(command,capture_output=True,text=True,timeout=30);(out/(name+'-compile.log')).write_text(compiled.stdout+compiled.stderr);row={'name':name,'compile_exit':compiled.returncode,'pass':False}
 if not compiled.returncode:
  run=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=10);(out/(name+'.log')).write_text(run.stdout+run.stderr);row['run_exit']=run.returncode;expected='read releases borrow' if name.startswith('read-') else 'write releases borrow';row['pass']=run.returncode==0 if name=='production' else bool(run.returncode and expected in run.stderr)
 rows.append(row)
r={'result':'PASS' if all(x['pass'] for x in rows) else 'FAIL','records':rows,'scope':'Actual OpRead/OpWrite/release helper; modeled4-range cache and disk I/O. Not guest FAT acceptance.','inputs':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [source,here/'host.c',Path(__file__)]}};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(r['result']);raise SystemExit(r['result']!='PASS')
