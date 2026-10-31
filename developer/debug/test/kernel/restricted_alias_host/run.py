#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1.
"""Actual native protection bridge and alias iteration, serialized HOST model."""
import argparse, hashlib, json, subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[4]
def extract(text,prefix):
    start=text.index(prefix);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path)
    out=p.parse_args().out.resolve()
    if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
    out.mkdir(parents=True);kernel=ROOT/'arch/aarch64-apple/kernel';source=kernel/'restricted.c';adapter=kernel/'setprotection.c'
    actual=extract(source.read_text(),'static uint64_t r_alias_no_allocate(')+extract(source.read_text(),'int AROSAppleRestrictedProtectAliases(')
    bridge=extract(adapter.read_text(),'static uintptr_t protect_one_page(')
    bridge=bridge.replace('__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));','ttbr0=123;').replace('__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));','tcr=16;')
    actual+=bridge
    variants=[('actual',actual),('skip-propagation',actual.replace('return AROSAppleRestrictedProtectAliases(protection_operation.address,tcr,\n        protection_operation.permissions);','return 1;')),('retired-root',actual.replace('d->state==R_REAPING || !d->root','!d->root')),('ignore-failure',actual.replace('{ ok=0;break; }','{ break; }')),('user-alias-flags',actual.replace('permissions & (AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_EXEC)','permissions'))]
    expected={
        'skip-propagation':'protect_one_page() && native_permissions==flags[i] && calls==2',
        'retired-root':'protect_one_page() && native_permissions==flags[i] && calls==2',
        'ignore-failure':'!protect_one_page() && calls==2 && native_permissions==29 && !coordinator',
        'user-alias-flags':'roots[0]==1 && roots[1]==2 && policies[0]==expected[i] && policies[1]==expected[i]',
    }
    result={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),'inputs':{str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [source,adapter,HERE/'test.c',HERE/'run.py']},'variants':[]}
    for name,code in variants:
        folder=out/name;folder.mkdir();(folder/'actual.inc').write_text(code)
        cmd=['clang','-std=c11','-Wall','-Wextra','-Wno-unused-function','-Werror','-fsanitize=address,undefined','-I'+str(folder),'-I'+str(ROOT/'arch/aarch64-all/kernel'),str(HERE/'test.c'),'-o',str(folder/'test')]
        b=subprocess.run(cmd,capture_output=True,text=True);(folder/'build.log').write_text(b.stdout+b.stderr)
        if b.returncode:result['error']=name+' compile';break
        r=subprocess.run([str(folder/'test')],capture_output=True,text=True,timeout=10);(folder/'run.log').write_text(r.stdout+r.stderr)
        result['variants'].append({'name':name,'exit':r.returncode})
        if (name=='actual' and (r.returncode or r.stderr)) or (name!='actual' and (r.returncode!=-6 or ('Assertion failed: ('+expected[name]+'), function main,') not in r.stderr)):
            result['error']=name+' outcome';break
    else:result['result']='DIAGNOSTIC_PASS' if result['dirty_inputs'] else 'HOST_PASS'
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'result':result['result'],'error':result.get('error')}));return result['result']=='FAIL'
if __name__=='__main__':raise SystemExit(main())
