#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Extract actual slot transitions; HOST serialized interleavings, not SMP proof."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
SOURCE=ROOT/'arch/aarch64-apple/kernel/restricted.c'

def body(text,name):
    start=text.index('static ',text.index(name)-40)
    end=text.index('{',text.index(name));depth=1;end+=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();out=a.out.resolve()
    if out.exists() or ROOT in out.parents:p.error('use a fresh output outside source')
    out.mkdir(parents=True)
    source=SOURCE.read_text()
    actual=''.join(body(source,name) for name in ['r_domain_slot(uintptr_t','r_domain_lookup(uint64_t','r_shared_operation(struct','r_domain_retire(struct','r_domain_lifecycle(struct','r_slot(uintptr_t','r_lifecycle(struct','r_close(struct','r_claim_reap(struct','r_stack_sp(const','r_stack_bounds(struct'])
    variants=[('actual',actual)]
    for name,old,new in [
        ('foreign-shared-close','b->owner_context!=context','0'),
        ('stale-shared-handle','(*link)->handle!=p->handle','0'),
        ('foreign-shared-release','b->releaser_context==context','1'),
        ('wrong-active-stack','r->fault_active ? r->fault_address : r->stack_address','r->stack_address'),
        ('guard-stack-bound','sp>base &&','sp>=base &&'),
        ('wrong-native-bounds','fault ? r->fault_address : r->stack_address','r->stack_address'),
        ('shared-tls-address','AROS_RESTRICTED_TLS+i*AROS_RESTRICTED_THREAD_STRIDE','AROS_RESTRICTED_TLS'),
        ('shared-domain-release','r->domain->state!=R_REAPING || r->domain->threads!=1','r->domain->state!=R_REAPING'),
        ('shared-domain-close','(!r->domain->managed && r->domain->threads!=1)','0'),
        ('undrained-space','!AROSRestrictedSpaceClose(&r->domain->space)','0'),
        ('thread-unmap-ack','!r_unmap_thread(r)','0'),
        ('stale-domain-handle','d->space.handle==handle','1'),
        ('foreign-domain-close','d->state!=R_LIVE || d->owner_context!=context','d->state!=R_LIVE'),
        ('early-domain-retire','if (d->threads) return 0;','if (0) return 0;'),
        ('undrained-region','a->region.space && !AROSRestrictedRegionDetach(&a->region)','0'),
        ('domain-stopped-by-thread','if (!r->domain->managed) AROSRestrictedSpaceStop','if (1) AROSRestrictedSpaceStop'),
        ('live-peer-snapshot','r->snapshot_data=!r->domain->managed || (r->domain->closing && r->domain->threads==1);','r->snapshot_data=1;'),
        ('open-domain-snapshot','r->domain->closing && r->domain->threads==1','r->domain->threads==1'),
        ('duplicate-reap','owned->state=R_REAPING;',''),
        ('wrong-publisher','r->owner!=task','0'),
        ('wrong-context','r->owner_context!=context','0'),
        ('wrong-releaser','r->releaser_context!=context','0'),
        ('early-reuse','if (r->state!=R_FREE) continue;','if (r->state!=R_FREE && r->state!=R_REAPING) continue;'),
        ('undrained-release','r_close(owned)','1'),
    ]:
        assert actual.count(old)==1,old
        variants.append((name,actual.replace(old,new)))
    result={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(
        ['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'inputs':{},'variants':[]}
    for path in [SOURCE,ROOT/'arch/aarch64-apple/include/aros/apple/restricted.h',HERE/'test.c',Path(__file__).resolve()]:
        result['inputs'][str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
    try:
        for name,code in variants:
            folder=out/name;folder.mkdir();(folder/'actual.inc').write_text(code)
            binary=folder/'test'
            command=['clang','-std=c11','-g','-O1','-fsanitize=address,undefined',
                '-I'+str(folder),'-I'+str(ROOT/'arch/aarch64-apple/include'),str(HERE/'test.c'),'-o',str(binary)]
            build=subprocess.run(command,capture_output=True,text=True,timeout=30)
            (folder/'build.log').write_text(build.stdout+build.stderr)
            if build.returncode:raise RuntimeError(name+' compilation failed')
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
            (folder/'run.log').write_text(run.stdout+run.stderr)
            result['variants'].append({'name':name,'exit':run.returncode,'command':command})
            if (run.returncode==0)!=(name=='actual'):raise RuntimeError(name+' unexpected result')
        result['result']='HOST_PASS'
    except Exception as error:result['error']=str(error)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'result':result['result'],'error':result.get('error')}))
    return int(result['result']!='HOST_PASS')

if __name__=='__main__':raise SystemExit(main())
