#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Production orphan admission/retirement, serialized HOST leases; not Native/SMP proof."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
SOURCE=ROOT/'arch/aarch64-apple/kernel/restricted.c'
def function(text,name):
    m=re.search(r'static\s+(?:int|void)\s+'+name+r'\s*\([^;{}]*\)\s*\{',text)
    assert m,name
    begin=text.index('{',m.start());end=begin+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[m.start():end]+'\n'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    args=p.parse_args();out=args.out.resolve()
    if out.exists() or ROOT in out.parents:p.error('use a fresh output outside source')
    out.mkdir(parents=True);source=SOURCE.read_text()
    defines='\n'.join(line for line in source.splitlines() if any(line.startswith('#define '+name+' ') for name in ('R_LIFETIME','R_DOMAIN_CLAIM','R_COPY','R_REAP_REFRESH')))
    begin=source.index('    if (!AROSAppleTaskExecution(context)->root &&')
    guard=source[begin:source.index('    r_coordinate();',begin)]
    begin=source.index('    if (ec==0x15 && svc==R_REAP_REFRESH) {')
    dispatch=source[begin:source.index('    if (ec==0x15 && (svc==R_WAKE',begin)]
    code=defines+'\n'+''.join(function(source,n) for n in ('r_orphan_work_pending','r_refresh_reap_hint','r_mark_retired_resources'))
    code+='static int native_admits(void *context,unsigned ec,unsigned svc)\n{\n'+guard+'return 1;\n}\n'
    code+='static int dispatch_refresh(struct frame *frame,void *context,unsigned ec,unsigned svc)\n{\nif (!native_admits(context,ec,svc)) return 0;\n'+dispatch+'return 0;\n}\n'
    variants=[('actual',code)]
    for name,old,new in [
        ('lost-thread-orphan','r->abandoned && (r->state==R_LIVE || r->state==R_REAPING)','0'),
        ('early-thread-release','r->abandoned && (r->state==R_LIVE || r->state==R_REAPING)','r->abandoned && r->state==R_LIVE'),
        ('lost-domain-orphan','!d->owner_context && d->managed &&','0 &&'),
        ('early-domain-release','(d->state==R_LIVE || d->state==R_REAPING)','d->state==R_LIVE'),
        ('unmanaged-domain-reap','!d->owner_context && d->managed &&','!d->owner_context &&'),
        ('lost-shared-orphan','if (!b->owner_context) return 1;','if (0) return 1;'),
        ('early-shared-view-release','if (!b->owner_context) return 1;','if (!b->owner_context && !b->backing.views) return 1;'),
        ('early-native-release','if (!b->owner_context) return 1;','if (!b->owner_context && !b->releaser_context) return 1;'),
        ('missing-retirement-hint','if (orphaned) __atomic_store_n','if (0) __atomic_store_n'),
        ('foreign-thread-retire','r->owner_context!=retired','0'),
        ('foreign-domain-retire','d->owner_context==retired','1'),
        ('foreign-shared-retire','b->owner_context==retired','1'),
        ('null-owner-retire','if (!retired) return;',''),
        ('missing-native-refresh-admission','svc>R_REAP_REFRESH','svc>R_COPY'),
        ('native-range-too-wide','svc>R_REAP_REFRESH','svc>R_REAP_REFRESH+1'),
    ]:
        assert code.count(old)==1,(name,old)
        variants.append((name,code.replace(old,new)))
    result={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True)),'inputs':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in (SOURCE,HERE/'test.c',Path(__file__).resolve())},'variants':[]}
    try:
        for name,body in variants:
            folder=out/name;folder.mkdir();(folder/'actual.inc').write_text(body);exe=folder/'test'
            cmd=['clang','-std=c11','-g','-O1','-fsanitize=address,undefined','-I'+str(folder),'-I'+str(ROOT/'arch/aarch64-apple/include'),str(HERE/'test.c'),'-o',str(exe)]
            build=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(folder/'build.log').write_text(build.stdout+build.stderr)
            if build.returncode:raise RuntimeError(name+' compile failed')
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10);(folder/'run.log').write_text(run.stdout+run.stderr);result['variants'].append({'name':name,'exit':run.returncode,'command':cmd})
            if (run.returncode==0)!=(name=='actual'):raise RuntimeError(name+' unexpected result')
        result['result']='HOST_PASS'
    except Exception as error:result['error']=str(error)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'result':result['result'],'error':result.get('error')}));return int(result['result']!='HOST_PASS')
if __name__=='__main__':raise SystemExit(main())
