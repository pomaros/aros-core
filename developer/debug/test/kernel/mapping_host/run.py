#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Bounded HOST tests of actual portable engine. No platform or guest claim."""
import argparse,hashlib,json,os,subprocess,sys,time,resource,signal
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',default='clang');p.add_argument('--allow-dirty-diagnostic',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[5];here=Path(__file__).resolve().parent
model=root/'rom/kernel/mapping/mapping.c';header=model.with_suffix('.h')
out=a.output.resolve()
if out==root or root in out.parents:raise ValueError('output must be outside production source')
source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
dirty=bool(subprocess.check_output(['git','status','--porcelain','--','rom/kernel/mapping','developer/debug/test/kernel/mapping_host'],cwd=root,text=True))
if dirty and not a.allow_dirty_diagnostic:raise ValueError('dirty source requires diagnostic mode')
inputs=[model,header,model.parent/'domain.c',model.parent/'domain.h',here/'domain_test.c',here/'test.c',here/'range_test.c',here/'writeback_batch_test.c',Path(__file__)]
input_hashes_before={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}
out.mkdir(parents=True,exist_ok=False)
for f in inputs:(out/f.name).write_bytes(f.read_bytes())
text=model.read_text();(out/'mapping.h').write_bytes(header.read_bytes())
flags=[a.cc,'-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(model.parent)]
commands=[];start=time.monotonic()
def limits():
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
 resource.setrlimit(resource.RLIMIT_CPU,(15,15))
 resource.setrlimit(resource.RLIMIT_FSIZE,(16<<20,16<<20))
def run(argv,name):
 remain=180-(time.monotonic()-start)
 if remain<=0:raise RuntimeError('180second suite bound')
 r=subprocess.run(list(map(str,argv)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=min(20,remain),preexec_fn=limits,env={**os.environ,'ASAN_OPTIONS':'abort_on_error=1:detect_leaks='+('0'if sys.platform=='darwin'else'1'),'UBSAN_OPTIONS':'halt_on_error=1'})
 (out/(name+'.log')).write_bytes(r.stdout);commands.append(dict(name=name,argv=list(map(str,argv)),returncode=r.returncode));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');return r
EXPECTED_ASSERTIONS={'batch-no-rollback': ('pages[m][p].state==state', 'assert_all'), 'stale-generation': ('!KrnMappingEndWriteback(&wb, 0)', 'object_tests'), 'writeback-unprotected': ('memory.protection==1', 'object_tests'), 'drop-dirty-on-close': ('!KrnMappingReleasePage(&m, 0, &ops)', 'object_tests'), 'cut-live-page': ('KrnMappingCut(&arena,&object,BASE+2*PAGE,2*PAGE,&right,rpages,8)==KMAP_CUT_INVALID', 'main'), 'populate-preflight': ('!KrnMappingPopulate(&m,m.start,m.length,1,&resident,&pending,&error)', 'populate_tests'), 'cut-offset': ('right.start==BASE+4*PAGE && right.length==2*PAGE && right.offset==7*PAGE', 'main'), 'cut-neighbor': ('object.next==&right && right.next==&neighbor', 'main'), 'double-drain': ('m.resident==1 && !m.in_flight && memory.protection==1', 'object_tests'), 'cut-capacity': ('KrnMappingCut(&arena,&lazy,start+2*PAGE,2*PAGE,&extra,extra_pages,3)==KMAP_CUT_INVALID', 'main'), 'batch-partial-end': ('!KrnMappingEndWritebackBatch(batch,6,0)', 'main'), 'publish-after-close': ('!KrnMappingComplete(&first,0x200000,0,&ops)', 'close_completion_tests'), 'batch-duplicate': ('!KrnMappingBeginWritebackBatch(batch,6,&ops) && !installs', 'main'), 'cut-generation': ('!KrnMappingComplete(&stale,0xa00000,0,&ops)', 'main'), 'batch-not-shared': ('!KrnMappingBeginWritebackBatch(batch,6,&ops) && !installs', 'main')}
def expected_assertion(name, completed):
 expression,function=EXPECTED_ASSERTIONS[name]
 text=completed.stdout.decode(errors='replace')
 prefix='Assertion failed: ('+expression+'), function '+function+', file '
 return completed.returncode==-signal.SIGABRT and len(text.splitlines())==1 and text.startswith(prefix)
controls={}
def execute(name,test,before=None,after=None):
 source=model
 if before is not None:
  assert text.count(before)==1,(name,'mutant no longer matches')
  source=out/(name+'.c');source.write_text(text.replace(before,after))
 binary=out/name;r=run(flags+[test,source,'-o',binary],name+'-build');assert r.returncode==0,(name,r.stdout.decode())
 r=run([binary],name)
 if before is None:assert r.returncode==0,(name,r.stdout.decode())
 else:
  assert expected_assertion(name,r),(name,r.returncode,r.stdout.decode())
  controls[name]=True
try:
 execute('object',here/'test.c')
 execute('range',here/'range_test.c')
 execute('batch',here/'writeback_batch_test.c')
 domain=out/'domain'
 r=run(flags+[here/'domain_test.c',model.parent/'domain.c','-o',domain],'domain-build')
 assert r.returncode==0,r.stdout.decode()
 r=run([domain],'domain')
 assert r.returncode==0,r.stdout.decode()
 for name,before,after,test in [
  ('writeback-unprotected','p->physical, p->protection & ~KMAP_WRITE))\n        return 0;','p->physical, p->protection))\n        return 0;',here/'test.c'),
  ('stale-generation','p->state == state && p->generation == r->generation','p->state == state',here/'test.c'),
  ('drop-dirty-on-close','(m->shared && p->state == KMAP_DIRTY)','(0)',here/'test.c'),
  ('publish-after-close','if (m->closing) {','if (0 && m->closing) {',here/'test.c'),
  ('double-drain','if (!p)\n        return 0;\n    struct KrnMappingObject *m = r->mapping;','if (!p) { if (r && r->mapping) --r->mapping->in_flight; return 0; }\n    struct KrnMappingObject *m = r->mapping;',here/'test.c'),
  ('cut-offset','right->offset=m->offset+after*m->page_size;','right->offset=m->offset;',here/'range_test.c'),
  ('cut-neighbor','right->resident=resident_pages(right_pages,remaining);','right->resident=resident_pages(right_pages,remaining); right->next=NULL;',here/'range_test.c'),
  ('cut-live-page','if (m->pages[i].physical || m->pages[i].state==KMAP_LOADING ||','if (m->pages[i].state==KMAP_LOADING ||',here/'range_test.c'),
  ('cut-capacity','right_capacity<remaining','(right_capacity<remaining && right_capacity==SIZE_MAX)',here/'range_test.c'),
  ('cut-generation','m->pages[i]=m->pages[after+i];\n            m->pages[i].generation=generation;','m->pages[i]=m->pages[after+i];',here/'range_test.c'),
  ('populate-preflight','if (queue && (p->state==KMAP_ERROR ||','if (0 && queue && (p->state==KMAP_ERROR ||',here/'test.c'),
  ('batch-duplicate','if (i && address<=previous)','if (0 && i && address<=previous)',here/'writeback_batch_test.c'),
  ('batch-not-shared','!p || !r->mapping->shared || !p->physical','!p || !p->physical',here/'writeback_batch_test.c'),
  ('batch-no-rollback','prior->mapping->pages[prior->page].state=KMAP_DIRTY;','(void)prior;',here/'writeback_batch_test.c'),
  ('batch-partial-end','if (!writeback_batch_valid(requests,count,KMAP_WRITEBACK)) return 0;','if (0) return 0;',here/'writeback_batch_test.c'),
 ]:execute(name,test,before,after)
 inputs=[model,header,model.parent/'domain.c',model.parent/'domain.h',here/'domain_test.c',here/'test.c',here/'range_test.c',here/'writeback_batch_test.c',Path(__file__)]
 result={'result':'DIAGNOSTIC_PASS' if dirty else 'PASS','source_commit':source_commit,'dirty_source':dirty,'scope':'HOST portable mapping object engine with PTE callbacks, no actual platform/ABI/scheduler/pager/guest acceptance','controls_detected':controls,'commands':commands,'elapsed_seconds':time.monotonic()-start,'inputs':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in inputs}}
 assert {str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in inputs}==input_hashes_before,'source changed during replay'
except (OSError,ValueError,RuntimeError,AssertionError,subprocess.SubprocessError) as error:
 result={'result':'FAIL','source_commit':source_commit,'dirty_source':dirty,'error':str(error),'commands':commands,'controls_detected':controls,'elapsed_seconds':time.monotonic()-start}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'result':result['result'],'controls':len(controls),'seconds':result['elapsed_seconds']}))
raise SystemExit(0 if result['result'] in ('PASS','DIAGNOSTIC_PASS') else 1)
