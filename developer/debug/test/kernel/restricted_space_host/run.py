#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual production space/channel adapter with synthetic PTE memory; HOST only."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--allow-dirty-diagnostic',action='store_true');a=p.parse_args()
 out=a.out.resolve();native=ROOT/'arch/aarch64-apple/kernel';shared=ROOT/'arch/aarch64-all/kernel';source=native/'restricted_service.c'
 if out.exists() or ROOT in out.parents:p.error('fresh output outside checkout required')
 dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT))
 if dirty and not a.allow_dirty_diagnostic:p.error('commit inputs or select diagnostic mode')
 inputs=[source,native/'restricted_service.h',ROOT/'arch/aarch64-apple/include/aros/apple/restricted.h',shared/'mapping.c',shared/'mapping.h',shared/'setprotection.c',shared/'protection.h',HERE/'test.c',Path(__file__).resolve()]
 inputs+=sorted((ROOT/'rom/kernel/mapping').glob('*.[ch]'))
 hashes={str(p.relative_to(ROOT)):sha(p) for p in inputs};out.mkdir(parents=True)
 r={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':dirty,'input_hashes':hashes,'records':[]}
 original=source.read_text()
 generic=ROOT/'rom/kernel/mapping/mapping.c';pager=ROOT/'rom/kernel/mapping/pager.c'
 cases=[('baseline','adapter',None,None,None),
  ('cut-identity','adapter','right->identity=r->identity;','right->identity=0;','cut-retained-identity'),
  ('cut-ledger-offset','adapter','page+=r->mapping.chunk_offset;','page+=0;','cut-retained-identity'),
  ('cow-copy','adapter','memcpy((void *)pa,(const void *)old,PAGE);','(void)old;','cow-bytes'),
  ('cow-writable','adapter','p->protection!=KMAP_READ || p->generation','(p->protection&~(KMAP_READ|KMAP_WRITE)) || p->generation','cow-writable-refused'),
  ('cow-count','adapter','*peer=r->shared_next;--b->views;','*peer=r->shared_next;','cow-ledger'),
  ('take-space-selection','adapter','s=selected ? selected->services : active;','s=active;','take-space-foreign'),
  ('shared-copy-executable','adapter','(write && (p->protection&KMAP_EXEC))','0','shared-copy-executable-refused'),
  ('shared-copy-closing','adapter','b->publication || b->closing ||\n        b->count','b->publication ||\n        b->count','shared-copy-closing-refused'),
  ('view-exec-publication','adapter','if ((access&KMAP_EXEC) && (b->native_view || b->native_pending)) return 0;','','view-refuses-exec-publication'),
  ('view-ending-publication','adapter','(b->native_view || b->native_pending)) return 0;','b->native_view) return 0;','view-ending-refuses-exec'),
  ('view-beside-exec','adapter','if (backing_of(r,page) && (KrnMappingPageAt(&r->mapping,page)->protection&KMAP_EXEC)) return 0;','(void)0;','view-refused-beside-exec'),
  ('view-one-per-object','adapter','b->native_view || b->native_pending || b->views==SIZE_MAX) return 0;','b->views==SIZE_MAX) return 0;','view-one-pending'),
  ('view-copy-alias','adapter','other->native_view<end) return 0;','other->native_view<end && 0) return 0;','view-copy-buffer-refused'),
  ('view-counted','adapter','b->native_pending=1;++b->views;return 1;','b->native_pending=1;return 1;','view-begin-counts'),
  ('view-closing','adapter','if (!b || b->closing || !shared_registered(b) || b->publication ||','if (!b || !shared_registered(b) || b->publication ||','view-closing-still-refused'),
  ('shared-copy-alias','adapter','if (begin<other->pages[i]+PAGE && other->pages[i]<end) return 0;','if (begin<other->pages[i]+PAGE && other->pages[i]<end && 0) return 0;','shared-copy-native-alias-refused'),
  ('reserved-fault-ledger','adapter','backing_of(r,index) || r->publication_pending','r->publication_pending','reserved-fault-ledger-refused'),
  ('reserved-fault-pending','adapter','backing_of(r,index) || r->publication_pending','backing_of(r,index)','reserved-fault-pending-refused'),
  ('reserved-fault-state','adapter','p->state!=KMAP_RESERVED || p->physical','p->physical','reserved-fault-state-refused'),
  ('reserved-fault-generation','adapter','p->physical || p->generation==UINT64_MAX','p->physical','reserved-fault-generation-exhausted'),
  ('reserved-fault-pte','adapter','if (AArch64MappingPhysical(&s->hardware,s->root,s->tcr,va,&actual) &&\n        (actual!=va || !AArch64PrivilegedPagePhysical(&s->hardware,s->root,s->tcr,va,&actual))) return 0;','(void)actual;(void)va;','reserved-fault-pte-refused'),
  ('shared-exec-write-conflict','adapter','if ((access&KMAP_EXEC) && (p->protection&KMAP_WRITE)) return 0;','','shared-publish-write-conflict'),
  ('shared-write-exec-conflict','adapter','if ((access&KMAP_WRITE) && (p->protection&KMAP_EXEC)) return 0;','','shared-publish-commit-writer-refused'),
  ('shared-native-writer','adapter','((access&KMAP_EXEC) || other_execute)','(access&KMAP_EXEC)','shared-publish-keeps-native-readonly'),
  ('shared-pending-owner','adapter','(b->publication && b->publication!=r)','0','shared-publish-pending-other-transaction'),
  ('shared-pending-decommit','adapter','!released || r->publication_pending ||\n        (r->shared && r->shared->publication)','!released || r->publication_pending','shared-publish-pending-decommit'),
  ('shared-pending-detach','adapter','if (r->shared->publication==r) r->shared->publication=NULL;','','shared-publish-detach-pending'),
  ('fixed-channel-cap','adapter','space->channels==(unsigned)-1','space->channels>=16','channel-growth-init'),
  ('channel-stops-domain','adapter','s->closing=1;','s->closing=1; KrnMappingDomainStop(&s->space->domain);','channel-close-keeps-domain'),
  ('stale-channel','adapter','    while (s && s->channel!=handle) s=s->index_next;\n    return s;','    while (s && s->channel!=handle) s=s->index_next;\n    if (!s) for (s=active;s && s->next;) s=s->next;\n    return s;','stale-channel-refused'),
  ('early-space-unmap','adapter','if (s->channels) return 0;','if (0 && s->channels) return 0;','space-keeps-live-backing'),
  ('duplicate-space','adapter','s->root || physical_in_use(backing)','s->root','duplicate-space-backing'),
  ('private-alias','adapter','!backing || *backing || physical_in_use(pa)','!backing || *backing','private-alias-refused'),
  ('publication-generation','adapter','if (!r->publication_pending) ++p->generation;','','publication-generation-advanced'),
  ('publication-skip-revoke','adapter','if (!KrnMappingProtectPage(&r->mapping,page,0,&r->space->mapops)) return 0;','','publication-revoked-before-native'),
  ('publication-pinned','adapter','r->mapping.closing || r->mapping.in_flight || !r->mapping.virtual_reservation','r->mapping.closing || !r->mapping.virtual_reservation','publication-pinned-refused'),
  ('publication-early-pending','adapter','r->publication_pending=1;r->publication_page=page;','r->publication_pending=0;r->publication_page=page;','publication-revoked-before-native'),
  ('publication-direct-write','adapter','!(KrnMappingPageAt(&r->mapping,page)->protection&KMAP_EXEC) &&','1 &&','publication-exec-direct-write-refused'),
  ('unowned-install','adapter','if (!owns_page(s,va,pa)) return 0;','','unowned-region-install'),
  ('pinned-backing','adapter','if (!KrnMappingDecommitPage(&r->mapping,page,&r->space->mapops,released)) return 0;',
   'if (!KrnMappingDecommitPage(&r->mapping,page,&r->space->mapops,released)) { if (backing_at(r,page)) *backing_at(r,page)=0;return 0; }','region-decommit-pinned'),
  ('attached-region','adapter','if (s->regions) return 0;','','space-keeps-data-with-regions'),
  ('eager-readonly','mapping','m->start+index*m->page_size,physical,protection)',
   'm->start+index*m->page_size,physical,protection&~KMAP_WRITE)','eager-write-permission'),
  ('guard-readonly','mapping','(p->state==KMAP_DIRTY || anonymous) ? protection',
   'p->state==KMAP_DIRTY ? protection','guard-to-write'),
  ('reused-reservation','adapter','r->identity=++reservation_generation;','r->identity=1;','reused-reservation-distinct-identity'),
  ('identity-pte-mismatch','adapter','if (!ok || actual!=p->physical) return 0;','if (!ok) return 0;','identity-pte-mismatch'),
  ('shared-early-close','adapter','b->closing=1;if (b->views) return 0;','b->closing=1;','shared-close-retained'),
  ('shared-unretained','adapter','r->shared=b;r->shared_first=first;++b->views;','r->shared=b;r->shared_first=first;','shared-view-count'),
  ('shared-owned-decommit','adapter','if (r->shared) *released=0;\n    else if (*released)','if (*released)','shared-decommit-retains-object'),
  ('index-no-shift','adapter','if (((j-home)&INDEX_MASK)>=((j-i)&INDEX_MASK)) { backing_index[i]=backing_index[j];i=j; }','(void)home;','index-owned-refused'),
  ('index-no-remove','adapter','else if (*released) index_remove(*released);','else if (*released) (void)index_remove;','region-recommit'),
  ('index-no-detach-remove','adapter','if (!r->shared) index_remove(released);','(void)released;','reused-reservation'),
  ('chunk-attach-dirty','adapter','for (size_t i=0;i<r->mapping.chunk_pages;++i) if (backing[i]) return 0;','','chunk-attach-dirty-refused'),
  ('chunk-reserved-generation','adapter','*generation=p ? p->generation : m->reserved_generation;','*generation=p ? p->generation : 1;','chunk-reserved-identity'),
  ('shared-owned-detach','adapter','if (backing) *backing=0;\n        }\n        --r->shared->views','if (backing) *backing=r->shared->pages[i];\n        }\n        --r->shared->views','shared-detach-no-ownership'),
  ('shared-closing-attach','adapter','!shared_registered(b) || b->closing ||','!shared_registered(b) ||','shared-close-stops-attach'),
  ('shared-untracked-pa','adapter','index_insert(pages[i]);','(void)pages[i];','shared-duplicate-pa'),
  ('shared-index-rollback','adapter','while (i) index_remove(pages[--i]);','(void)i;','shared-registration-rollback'),
  ('shared-index-early-close','adapter','b->closing=1;if (b->views) return 0;','b->closing=1;for (size_t i=0;i<b->count;++i) index_remove(b->pages[i]);if (b->views) return 0;','shared-closing-index-retained'),
  ('shared-index-no-release','adapter','for (size_t i=0;i<b->count;++i) index_remove(b->pages[i]);','(void)b;','shared-index-reuse-after-close'),
  ('recommit-zero','pager','bytes[i]=0;','bytes[i]=0xa5;','region-recommit-zero'),
  ('jit-native-readonly','adapter','unsigned native=access==KMAP_JIT_CLASS ? KMAP_READ|KMAP_WRITE :','unsigned native=access==KMAP_JIT_CLASS ? KMAP_READ :','jit-native-writable'),
  ('jit-shared','adapter','!KrnMappingProtectionValid(access,!r->shared)','!KrnMappingProtectionValid(access,1)','jit-shared-publish-refused'),
  ('jit-identity-exec','adapter','access==KMAP_EXEC && p->protection==KMAP_JIT_CLASS ?\n        AARCH64_PROTECT_JIT : access;','access;','jit-execute-identity'),
  ('jit-direct-commit','adapter','unsigned initial=access==KMAP_JIT_CLASS ? KMAP_READ|KMAP_WRITE : access;','unsigned initial=access;','jit-sync-alias-resident'),
  ('jit-install-class','adapter','unsigned access=protection==KMAP_JIT_CLASS ?','unsigned access=0 ?','jit-commit')]
 try:
  for name,target,before,after,oracle in cases:
   used=source;used_pager=pager
   if before:
    text={'adapter':original,'mapping':generic.read_text(),'pager':pager.read_text()}[target]
    if text.count(before)!=1:raise RuntimeError('mutation must match once: '+name)
    mutation=out/(name+'.c');mutation.write_text(text.replace(before,after))
    if target=='adapter':used=mutation
    elif target=='pager':used_pager=mutation
    else:
     used=out/(name+'-adapter.c')
     used.write_text(original.replace('#include "../../../rom/kernel/mapping/mapping.c"','#include "'+str(mutation)+'"'))
   binary=out/name
   cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(native),'-I'+str(ROOT/'rom/kernel/mapping'),'-I'+str(ROOT/'arch/aarch64-apple/include'),str(HERE/'test.c'),str(used),str(shared/'mapping.c'),str(shared/'setprotection.c'),str(used_pager),'-o',str(binary)]
   build=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/(name+'-build.log')).write_text(build.stdout+build.stderr)
   if build.returncode:raise RuntimeError(name+' build failed')
   run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'abort_on_error=1:detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
   (out/(name+'.log')).write_text(run.stdout+run.stderr)
   expected='FAIL '+oracle+'\n' if oracle else 'PASS restricted space and channel ownership\n'
   row={'name':name,'command':cmd,'build_exit':build.returncode,'exit':run.returncode,'stdout':run.stdout,'stderr':run.stderr,'binary_sha256':sha(binary)};r['records'].append(row)
   if run.returncode!=bool(oracle) or run.stdout!=expected or run.stderr:raise RuntimeError(name+' semantic mismatch')
  if hashes!={str(p.relative_to(ROOT)):sha(p) for p in inputs}:raise RuntimeError('inputs changed')
  r['result']='DIAGNOSTIC_PASS' if dirty else 'PASS'
 except Exception as e:r['error']=str(e)
 (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'result':r['result'],'error':r.get('error')}));return int(r['result']=='FAIL')
if __name__=='__main__':raise SystemExit(main())
