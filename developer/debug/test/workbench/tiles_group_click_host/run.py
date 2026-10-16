#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);out=pathlib.Path(p.parse_args().output).resolve();out.mkdir(parents=True,exist_ok=False)
source=root/'workbench/system/Wanderer/Classes/iconlist.c';raw=source.read_bytes();s=raw.decode('latin1');(out/'iconlist.c').write_bytes(raw)
marker='static BOOL IconList_TileRegionConsumesDoubleClick(';assert s.count(marker)==1
begin=s.index(marker);end=s.index('{',begin)+1;depth=1
while depth:
 if s[end]=='{':depth+=1
 elif s[end]=='}':depth-=1
 end+=1
helper=s[begin:end];(out/'helper.inc').write_text(helper)
marker='                        if (doubleclicked && data->icld_SelectionLastClicked &&';assert s.count(marker)==1
begin=s.index(marker);end=s.index('return MUI_EventHandlerRC_Eat;',begin)+len('return MUI_EventHandlerRC_Eat;')
(out/'dispatch.inc').write_text(s[begin:end])
marker='                        struct IconEntry *tileEntry = data->icld_SelectionLastClicked;';assert s.count(marker)==1
begin=s.index(marker);end=s.index('                        if (data->icld_LassoActive == TRUE)',begin)
(out/'release.inc').write_text(s[begin:end])
fixture=(here/'fixture.c').read_bytes();(out/'fixture.c').write_bytes(fixture)
cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(out/'fixture.c'),'-o',str(out/'test')]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(out/'test')],capture_output=True,text=True,timeout=30);(out/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0 and r.stdout=='tiles-group-doubleclick PASS\n' and not r.stderr,r
mut=helper.replace('if (DoMethod(obj,MUIM_IconList_Tile,(IPTR)entry,(IPTR)&anchor))','if (DoMethod(obj,MUIM_IconList_Tile,(IPTR)entry,(IPTR)&anchor) && 0)');assert mut!=helper
(out/'helper-mutant.inc').write_text(mut);(out/'helper.inc').write_text(mut)
r=subprocess.run(cmd[:-1]+[str(out/'mutant')],capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr
r=subprocess.run([str(out/'mutant')],capture_output=True,text=True,timeout=30);(out/'mutant.log').write_text(r.stdout+r.stderr);assert r.returncode,'old group suppression unexpectedly passed'
(out/'helper.inc').write_text(helper)
(out/'result.json').write_text(json.dumps({'result':'PASS','source_sha256':hashlib.sha256(raw).hexdigest(),'fixture_sha256':hashlib.sha256(fixture).hexdigest(),'command':cmd,'negative_control':'old aggregate suppression rejected'},indent=2)+'\n')
print('tiles-group-doubleclick PASS; old behavior mutant rejected')
