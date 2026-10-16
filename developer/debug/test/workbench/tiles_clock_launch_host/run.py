#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=False)
source=(here.parents[4]/'workbench/system/Tiles/main.c').read_text()
start=source.index('static BOOL start_clock_source(void)');opening=source.index('{',start);end=opening+1;depth=1
while depth:
 depth+=(source[end]=='{')-(source[end]=='}');end+=1
helper=source[start:end];unit=(here/'fixture.c').read_text().replace('/* EXTRACT */',helper)
(out/'main.c').write_text(source);results={}
for name in ['production','omit_input','omit_output','close_after_transfer']:
 code=unit
 if name=='omit_input':assert code.count('SYS_Input,input,')==1;code=code.replace('SYS_Input,input,','')
 if name=='omit_output':assert code.count('SYS_Output,output,')==1;code=code.replace('SYS_Output,output,','')
 if name=='close_after_transfer':
  old='    return TRUE;';assert code.count(old)==1;code=code.replace(old,'    Close(input);Close(output);return TRUE;')
 path=out/(name+'.c');path.write_text(code)
 cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(path),'-o',str(out/name)]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 r=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=15);(out/(name+'.run.log')).write_text(r.stdout+r.stderr)
 if name=='production':assert r.returncode==0 and r.stdout=='tiles-clock-launch-host PASS\n' and not r.stderr,r
 else:assert r.returncode!=0 and 'Assertion failed:' in r.stderr,r
 results[name]={'exit':r.returncode,'command':cmd,'unit_sha256':hashlib.sha256(code.encode()).hexdigest()}
(out/'result.json').write_text(json.dumps({'result':'PASS','source_sha256':hashlib.sha256(source.encode()).hexdigest(),'helper_sha256':hashlib.sha256(helper.encode()).hexdigest(),'runs':results},indent=2)+'\n')
print('tiles-clock-launch-host PASS: production and three failing mutation controls')
