#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
"""Exercise freshly extracted production ENV/click functions and the real parser."""
import argparse, hashlib, json, pathlib, re, subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--cc',default='clang')
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
root=pathlib.Path(__file__).resolve().parents[5]
source=root/'workbench/system/Tiles/main.c';text=source.read_text()
# Blank strings, characters and comments without changing offsets, then balance
# braces. Never silently use the wrong definition or an incomplete extraction.
pattern=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',re.S)
masked=pattern.sub(lambda m:' '*len(m[0]),text)
def extract(name):
    matches=list(re.finditer(r'^static [^\n]+\b'+name+r'\([^;]*?\)\s*\{',masked,re.M))
    assert len(matches)==1,(name,len(matches))
    begin=matches[0].start();pos=masked.index('{',matches[0].start());depth=1;end=pos+1
    while depth and end<len(masked):
        depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
    assert depth==0,name
    return text[begin:end]
functions='\n\n'.join(extract(n) for n in ('source_result','set_value','app_messages'))
fixture=pathlib.Path(__file__).with_name('fixture.c').read_text()
assert fixture.count('/* PRODUCTION */')==1
unit=fixture.replace('/* PRODUCTION */',functions)
(a.output/'source-main.c').write_text(text)
parser=root/'workbench/system/Tiles/tiles_config.c'
header=parser.with_suffix('.h')
def run(label,code,negative=False):
    cfile=a.output/(label+'.c');cfile.write_text(code)
    command=[a.cc,'-std=c11','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(parser.parent),str(cfile),str(parser),'-o',str(a.output/label)]
    (a.output/(label+'-command.json')).write_text(json.dumps(command,indent=2)+'\n')
    compile=subprocess.run(command,capture_output=True,text=True,timeout=60)
    (a.output/(label+'-compile.log')).write_text(compile.stdout+compile.stderr)
    if compile.returncode: raise RuntimeError(compile.stderr)
    result=subprocess.run([str(a.output/label)],capture_output=True,text=True,timeout=30)
    (a.output/(label+'.log')).write_text(result.stdout+result.stderr)
    if negative:
        assert result.returncode!=0 and 'calls==2' in result.stderr,'mutation must fail queued-toggle assertion'
    else:
        assert result.returncode==0 and not result.stderr and result.stdout.startswith('PASS actual '),result
    return result.returncode
positive=run('production',unit)
old='(tile->envTogglePending ? tile->envToggleValue : tile->value.boolean)'
assert unit.count(old)==1
negative=run('old-click-regression',unit.replace(old,'tile->value.boolean'),True)
files=[source,parser,header,pathlib.Path(__file__).with_name('fixture.c'),pathlib.Path(__file__)]
report={'result':'PASS','production_exit':positive,'regression_control_exit':negative,
        'scope':'Actual extracted service functions + real parser; host queue/ENV mocks, no guest claim',
        'source_sha256':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}}
(a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
