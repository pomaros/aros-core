#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output',required=True);args=p.parse_args()
out=pathlib.Path(args.output);out.mkdir(parents=True,exist_ok=False)
source=(here.parents[4]/'workbench/system/Tiles/main.c').read_text()
def extract(name):
 start=source.index(('static BOOL ' if name=='unsubscribe_screen' else 'static void ')+name+'('); body=source.index('{',start);depth=1;end=body+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]+'\n'
functions=''.join(extract(n) for n in ['discard_app_messages','release_tile','reload_catalog','release_screen'])
unit=(here/'fixture.c').read_text().replace('/* EXTRACT */',functions)
(out/'main.c').write_text(source);results={}
for name in ['production','omit_discard','unlock_before_cleanup','pending','reuse_pending','notify','forget_subscription']:
 code=unit
 if name in ['pending','reuse_pending']:
  code=(here/'pending.c').read_text().replace('/* EXTRACT */',extract('release_tile')+extract('load_tile'))
 if name in ['notify','forget_subscription']:
  code=(here/'notify.c').read_text().replace('/* EXTRACT */',extract('screen_messages')+extract('unsubscribe_screen'))
 if name=='forget_subscription':
  old='if(!EndScreenNotify(service->screenNotify))return FALSE;';assert code.count(old)==1;code=code.replace(old,'if(!EndScreenNotify(service->screenNotify)){service->screenNotify=NULL;return FALSE;}')
 if name=='reuse_pending':
  old='!TileSourcePending(&service->sources,&service->tiles[i])';assert code.count(old)==1;code=code.replace(old,'1')
 if name=='omit_discard':code=code.replace('discard_app_messages(service);','/* omitted */')
 if name=='unlock_before_cleanup':code=code.replace('drop_tiles(service);close_panels(service);','UnlockPubScreen(NULL,service->screen);drop_tiles(service);close_panels(service);')
 path=out/(name+'.c');path.write_text(code)
 cmd=['clang','-std=c99','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-parameter','-Werror','-fsanitize=address,undefined','-g',str(path),'-o',str(out/name)]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 r=subprocess.run([str(out/name)],capture_output=True,text=True,timeout=20);(out/(name+'.run.log')).write_text(r.stdout+r.stderr)
 if name in ['production','pending','notify']:assert r.returncode==0 and r.stdout==('tiles-'+{'production':'reload','pending':'pending','notify':'notify'}[name]+'-host PASS\n') and not r.stderr,r
 else:assert r.returncode!=0 and 'Assertion failed:' in r.stderr,r
 results[name]={'exit':r.returncode,'command':cmd}
(out/'result.json').write_text(json.dumps({'result':'PASS','main_sha256':hashlib.sha256(source.encode()).hexdigest(),'runs':results},indent=2)+'\n')
print('tiles-reload-host PASS: actual lifecycle functions, four failing mutation controls')
