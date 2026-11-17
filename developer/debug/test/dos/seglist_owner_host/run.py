#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER. AROS Public License 1.1.
"""Exercise actual CreateSegList and UnLoadSeg release dispatch with protected pages."""
import argparse
import hashlib
import json
import re
import signal
import subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args(); here=Path(__file__).resolve().parent; root=here.parents[4]
out=a.output.resolve(); out.mkdir(parents=True,exist_ok=False)
create=(root/'compiler/arossupport/createseglist.c').read_text()
create=re.sub(r'^#include[^\n]*','',create,flags=re.M)
release=(root/'rom/dos/unloadseg.c').read_text()
release=release[release.index('static AROS_UFH3'):release.index('/*****************************************************************************')]
memory=(root/'compiler/include/exec/memory.h').read_text(encoding='latin-1')
flags='\n'.join(re.findall(r'^#define MEM[BF]_EXECUTABLE[^\n]*',memory,re.M))
assert flags.count('#define')==2
fixture=(here/'host.c').read_text().replace('/* @MEMFLAGS@ */',flags)
old='KrnAllocPages(NULL, segsize, MEMF_EXECUTABLE)'
assert create.count(old)==1
variants={'production':create,'untyped-pages':create.replace(old,'KrnAllocPages(NULL, segsize, MEMF_ANY)')}
results=[]
for variant,body in variants.items():
    source=fixture.replace('/* @CREATE@ */',body).replace('/* @FREE@ */',release)
    path=out/(variant+'.c'); path.write_text(source)
    for wx in (True,False):
        if not wx and variant!='production': continue
        name=variant+('-wx' if wx else '-pool'); binary=out/name
        command=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=undefined']
        if wx: command+=['-D__AROSPLATFORM_WXSEG__']
        command += [str(path),'-o',str(binary)]
        build=subprocess.run(command,capture_output=True,text=True)
        (out/(name+'-build.log')).write_text(build.stdout+build.stderr)
        assert build.returncode==0,(name,build.stderr)
        for scenario in ('initial','borrowed','bootstrap','failure'):
            run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=10)
            (out/(name+'-'+scenario+'.log')).write_text(run.stdout+run.stderr)
            negative=variant=='untyped-pages' and scenario=='borrowed'
            if negative:
                # UBSan may handle the signal and exit; require both our owner
                # witness and its invalid-write signal report in that case.
                fault=run.returncode in (-signal.SIGBUS,-signal.SIGSEGV)
                fault |= run.returncode!=0 and 'UndefinedBehaviorSanitizer:DEADLYSIGNAL' in run.stderr
                assert fault and 'WRONG_OWNER: pool free on protected page' in run.stderr,(name,run.returncode,run.stderr)
            else:
                assert run.returncode==0 and run.stdout.startswith('PASS '),(name,scenario,run.stdout,run.stderr)
            results.append({'variant':name,'scenario':scenario,'returncode':run.returncode,'expected_fault':negative})
result={'result':'PASS','scope':'HOST allocation/protection model with production creation and release dispatch; not guest execution',
        'cases':results,'production_sha256':{'create':hashlib.sha256(create.encode()).hexdigest(),'release':hashlib.sha256(release.encode()).hexdigest()}}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print('seglist owner HOST PASS: initial/borrowed/bootstrap/failure; old allocation faults only after arena growth')
