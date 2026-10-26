#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL), version 1.1.
"""Check four production bodies and reject unguarded-read mutations on HOST."""
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[3]
SOURCE=CORE/'rom/hidds/gfx/gfx_bitmapmemblit.c'
def identity(p):
 b=Path(p).read_bytes();return dict(path=str(Path(p).resolve()),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def bodies(text):
 result=[]
 for bits in (8,16,24,32):
  start=text.index('VOID BM__Hidd_BitMap__PutMemTemplate'+str(bits)+'(');brace=text.index('{',start);depth=1;end=brace+1
  while depth:
   depth+=(text[end]=='{')-(text[end]=='}');end+=1
  result.append(text[start:end])
 return '\n\n'.join(result)+'\n'
def run(out):
 out=Path(out).resolve();out.mkdir(exist_ok=False)
 current=SOURCE.read_text();fixed=bodies(current)
 original=fixed.replace('if (!mask && x + 1 < msg->width)', 'if (!mask)')
 # The boundary tests execute exact source bodies; the extraction is no port.
 assert fixed.count('if (!mask && x + 1 < msg->width)')==24
 assert original.count('if (!mask)')==24
 records=[]
 for name,body in [('fixed',fixed),('unguarded-control',original)]:
  folder=out/name;folder.mkdir();(folder/'production.inc').write_text(body)
  source=folder/'fixture.c';source.write_bytes((HERE/'fixture.c').read_bytes())
  for sanitizer in (False,True) if name=='fixed' else (False,):
   binary=folder/('fixture-asan'if sanitizer else'fixture')
   command=['/usr/bin/clang','-std=gnu11','-O1'if sanitizer else'-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter']
   if sanitizer:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
   command +=[str(source),'-o',str(binary)]
   built=subprocess.run(command,capture_output=True,text=True,timeout=30);(folder/(binary.name+'-compile.log')).write_text(built.stdout+built.stderr);assert built.returncode==0,built.stderr
   if name=='fixed':
    argv=[str(binary),'heap'if sanitizer else'guard'];r=subprocess.run(argv,capture_output=True,text=True,timeout=45,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
    (folder/(binary.name+'-run.log')).write_text(r.stdout+r.stderr);assert r.returncode==0 and 'cases=9072'in r.stdout,(r.returncode,r.stdout,r.stderr)
    records.append(dict(kind='FIXED_ACTUAL_BODY',command=command,argv=argv,returncode=r.returncode,stdout=r.stdout,production_sha256=hashlib.sha256(body.encode()).hexdigest(),binary=identity(binary)))
   else:
    for bits in (8,16,24,32):
     argv=[str(binary),'fault',str(bits)];r=subprocess.run(argv,capture_output=True,text=True,timeout=10);(folder/('fault-'+str(bits)+'.log')).write_text(r.stdout+r.stderr)
     assert r.returncode==90 and r.stderr=='GUARD_PAGE_READ_FAULT\n',(bits,r.returncode,r.stderr)
     records.append(dict(kind='UNGUARDED_MUTATION_GUARD_FAULT',bits=bits,argv=argv,returncode=r.returncode,stderr=r.stderr,production_sha256=hashlib.sha256(body.encode()).hexdigest()))
 result=dict(result='PASS_HOST_BOUNDARY_AND_PIXEL_ORACLE',source=identity(SOURCE),fixture=identity(HERE/'fixture.c'),negative_control="remove final-pixel read guards from current production bodies",compiler=identity('/usr/bin/clang'),records=records,native_qualification=False)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path);a=p.parse_args()
 if a.out:run(a.out)
 else:
  with tempfile.TemporaryDirectory(prefix='bitmap-bounds-')as t:run(Path(t)/'proof')
