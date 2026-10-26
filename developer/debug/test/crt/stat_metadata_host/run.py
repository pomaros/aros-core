#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Execute actual metadata bodies with modeled DOS providers and narrow off_t."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
here=Path(__file__).resolve().parent
root=here.parents[4];source=root/'compiler/crt/posixc/__stat.c'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--mutations',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
text=source.read_text()
def function(text,name):
 match=re.search(r'(?m)^(?:static )?(?:int|void) '+name+r'\([^;]+?\)\n\{',text)
 if not match:raise RuntimeError(name)
 pos=text.index('{',match.start())+1;depth=1
 while depth:
  depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
 return text[match.start():pos]
def unit(text):
 names=['__fill_statbuffer','__fill_stat64buffer']
 if re.search(r'(?m)^static int __stat_from_path_common\(',text):names+=['__fill_stat_pathbuffer','__stat_from_path_common']
 names+=['__stat_from_path','__stat64_from_path']
 body='\n\n'.join(function(text,n) for n in names)
 return (here/'test.c').read_text().replace('/* PRODUCTION */',body)
variants={'current':(text,('size','path-size','root','fallback','allocation','provider','join','missing'))}
if a.mutations:
 edits={
  'narrow-size':('sb->st_size    = (__off64_t)fib->fib_Size;', 'sb->st_size    = (off_t)fib->fib_Size;', 'size'),
  'missing-success':('__fill_stat_pathbuffer(sb, abspath, fib, fallback_to_defaults, lock, wide);\n        res = 0;', '__fill_stat_pathbuffer(sb, abspath, fib, fallback_to_defaults, lock, wide);', 'root'),
  'wrong-wide-fill':('return __stat_from_path_common(path, sb, TRUE);', 'return __stat_from_path_common(path, sb, FALSE);', 'path-size'),
  'leaked-input':('out:\n    FreeVec(mypath);', 'out:', 'allocation'),
 }
 for name,(before,after,scenario) in edits.items():
  assert text.count(before)==1,name
  variants[name]=(text.replace(before,after),(scenario,))
rows=[]
with tempfile.TemporaryDirectory(prefix='stat-metadata-') as temp:
 for name,(code,scenarios) in variants.items():
  out=a.output/name;out.mkdir()
  path=out/'fixture.c';path.write_text(unit(code))
  binary=Path(temp)/name
  cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(path),'-o',str(binary)]
  built=subprocess.run(cmd,capture_output=True,text=True,timeout=20);(out/'build.log').write_text(built.stdout+built.stderr);built.check_returncode()
  for scenario in scenarios:
   ran=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=10);(out/(scenario+'.log')).write_text(ran.stdout+ran.stderr)
   expected=ran.returncode==0 if name=='current' else ran.returncode!=0 and 'Assertion failed:' in ran.stderr
   rows.append({'variant':name,'scenario':scenario,'exit':ran.returncode,'expected':expected})
result={'result':'PASS' if all(r['expected'] for r in rows) else 'FAIL','runs':rows,
 'scope':'Actual fill/path bodies with modeled DOS/allocations/hash/converters and 32-bit ordinary offset; no real filesystem/assign/identity/guest qualification',
 'sources':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in (source,here/'test.c',Path(__file__).resolve())}}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(result['result']!='PASS')
