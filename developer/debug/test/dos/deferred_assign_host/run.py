#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4];source=root/'rom/dos/getdeviceproc.c';text=source.read_text()
actual=text[text.index('/* Deferred assign resolution owns'):text.index('static struct DevProc *deviceproc_internal(',text.index('/* Deferred assign resolution owns'))]
generation=(root/'rom/dos/dupassignlock.c').read_text();actual=generation[generation.index('void assign_generation_changed('):generation.index('BPTR dup_assign_member(')]+actual
variants={'positive':actual,
 'wait-under-read':actual.replace('        UnLockDosList(LDF_ALL | LDF_READ);\n\n        resolved =','        /* Missing guard release. */\n\n        resolved ='),
 'borrow-target':actual.replace('        CopyMem(dl->dol_misc.dol_assign.dol_AssignName, target, target_length + 1);','        target = dl->dol_misc.dol_assign.dol_AssignName;'),
 'accept-stale-generation':actual.replace('before.low == after.low && before.high == after.high','((void)before, (void)after, TRUE)'),
 'reuse-node-before-cleanup':actual.replace('        current = FindDosEntry(dl, copy, LDF_ALL);\n        UnLockDosList(LDF_ENTRY | LDF_READ);','        current = *entry;\n        UnLockDosList(LDF_ENTRY | LDF_READ);')}
a.output.mkdir(parents=True,exist_ok=False);rows=[]
with tempfile.TemporaryDirectory(prefix='dos-deferred-') as tmp:
 for name,code in variants.items():
  d=Path(tmp)/name;d.mkdir();(d/'actual.c').write_text(code)
  cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(d),str(here/'host.c'),'-o',str(d/'host')]
  b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(a.output/(name+'-compile.log')).write_text(b.stdout+b.stderr);assert b.returncode==0,b.stderr
  r=subprocess.run([str(d/'host')],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'});(a.output/(name+'.log')).write_text(r.stdout+r.stderr);assert (r.returncode==0)==(name=='positive'),(name,r.stderr)
  rows.append(dict(name=name,exit=r.returncode))
(a.output/'result.json').write_text(json.dumps(dict(scope='HOST actual deferred helpers and generation engine; modeled namespace, handler and security calls; no full GetDeviceProc or SMP guest claim',cases=rows,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()),indent=2)+'\n')
print('PASS deferred resolution and four rejected mutations')
