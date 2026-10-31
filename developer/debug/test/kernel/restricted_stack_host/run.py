#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1.
"""Actual stack mapping helpers with simulated page tables; HOST only."""
import argparse, hashlib, json, subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[4]
def extract(text,name):
    start=text.index('static ',text.index(name)-30)
    end=text.index('{',text.index(name))+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path)
    out=p.parse_args().out.resolve()
    if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
    out.mkdir(parents=True);source=ROOT/'arch/aarch64-apple/kernel/restricted.c'
    text=source.read_text()
    # Select the unmap definition, not its forward declaration.
    text=text.replace('static int r_unmap_thread(struct Restricted *);','')
    actual=''.join(extract(text,n) for n in ['r_tables_need(struct','r_range_tables(uint64_t','r_prepare(struct','r_unmap_thread(struct'])
    (out/'actual.inc').write_text(actual)
    command=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(out),str(HERE/'test.c'),'-o',str(out/'test')]
    result={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),'inputs':{str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [source,HERE/'test.c',HERE/'run.py']},'command':command}
    b=subprocess.run(command,capture_output=True,text=True);(out/'build.log').write_text(b.stdout+b.stderr)
    if not b.returncode:
        r=subprocess.run([str(out/'test')],capture_output=True,text=True,timeout=10)
        (out/'run.log').write_text(r.stdout+r.stderr);result['exit']=r.returncode
        if not r.returncode and not r.stderr:result['result']='DIAGNOSTIC_PASS' if result['dirty_inputs'] else 'HOST_PASS'
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result']);return result['result']=='FAIL'
if __name__=='__main__':raise SystemExit(main())
