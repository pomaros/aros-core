#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Execute actual exec lifecycle code at modeled DOS and library boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
here=Path(__file__).resolve().parent
root=here.parents[4]
source_path=root/'compiler/crt/posixc/__exec.c'
source=source_path.read_text()
text=(here/'host.c').read_text()
for marker,start,end in [
    ('EXEC_DO','static void __exec_do_regular(struct PosixCIntBase *PosixCBase)\n{','\n\nchar *const *__exec_valist2array'),
    ('EXEC_CLEANUP','static void __exec_cleanup(struct PosixCIntBase *PosixCBase)\n{',None),
]:
    begin=source.index(start)
    part=source[begin:source.index(end,begin)] if end else source[begin:]
    assert text.count('/* '+marker+' */')==1
    text=text.replace('/* '+marker+' */',part)
controls={
    'native-cwd':('CurrentDir(PosixCBase->exec_cwd)','CurrentDir(4)','run with captured logical directories'),
    'lost-cwd-restore':('UnLock(CurrentDir(oldcwd));','UnLock(oldcwd);','restore launcher directories'),
    'lost-file-release':('__releasefcb(PosixCBase->exec_files[i]);','(void)__releasefcb;','release every execution file once'),
    'lost-errno':('    errno = saved_errno;','    (void)saved_errno;','cleanup preserves the operation error'),
}
output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
records=[]
for name,patch,cause in [('production',None,None)]+[(n,(a,b),c) for n,(a,b,c) in controls.items()]:
    actual=text
    if patch:
        assert actual.count(patch[0])==1,(name,actual.count(patch[0]))
        actual=actual.replace(*patch)
    path=output/(name+'.c');path.write_text(actual);binary=output/name
    cmd=['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g','-fsanitize=address,undefined',str(path),'-o',str(binary)]
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
    (output/(name+'-compile.log')).write_text(p.stdout+p.stderr)
    row={'name':name,'command':cmd,'compile_exit':p.returncode,'expected_cause':cause,'pass':False}
    if not p.returncode:
        p=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
        (output/(name+'.log')).write_text(p.stdout+p.stderr)
        row.update(run_exit=p.returncode,**{'pass':p.returncode==0 if not cause else p.returncode!=0 and cause in p.stderr})
    records.append(row)
result={'result':'PASS' if all(x['pass'] for x in records) else 'FAIL','records':records,
        'inputs':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source_path,here/'host.c',Path(__file__)]},
        'scope':'Actual exec do/cleanup; mocked capture, DOS handles/directories, RunCommand and CRT bases. No exec preparation, full CRT admission, native guest or OS scheduler claim.'}
(output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['result']);raise SystemExit(result['result']!='PASS')
