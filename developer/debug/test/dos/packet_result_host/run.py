#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Exercise actual DOS packet transport using host allocator and handler ABI."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4];source=root/'rom/dos/dopkt.c'
s=source.read_text();s=s[s.index('static SIPTR packet_error('):]
wrapper=root/'rom/dos/dopktresult.c'
w=wrapper.read_text();w=w[w.index('    AROS_LIBFUNC_INIT'):w.index('    AROS_LIBFUNC_EXIT')]
s += '\nstatic SIPTR actual_result(struct MsgPort *port,LONG action,const SIPTR *args,SIPTR *result2) {\n' + w + '\n}\n'
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
for name in ['positive','task-asserted-process','lost-allocation-error','task-written-as-process']:
 lane=out/name;lane.mkdir();code=s
 if name=='task-asserted-process':
  before='ASSERT_VALID_TASK((struct Task *)me);';assert code.count(before)==1;code=code.replace(before,'ASSERT_VALID_PROCESS(me);')
 if name=='lost-allocation-error':
  before='return packet_error(me, res2, ERROR_NO_FREE_STORE);';assert code.count(before)==2;code=code.replace(before,'return DOSFALSE;')
 if name=='task-written-as-process':
  before='if (__is_process(me))\n        me->pr_Result2 = dp->dp_Res2;';assert code.count(before)==1;code=code.replace(before,'if (1)\n        me->pr_Result2 = dp->dp_Res2;')
 (lane/'actual.c').write_text(code)
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I'+str(lane),str(here/'host.c'),'-o',str(lane/'host')]
 b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(lane/'compile.log').write_text(b.stdout+b.stderr);r=None
 if b.returncode==0:
  r=subprocess.run([str(lane/'host')],capture_output=True,text=True,timeout=15);(lane/'run.log').write_text(r.stdout+r.stderr)
 rows.append({'name':name,'command':cmd,'compile_exit':b.returncode,'run_exit':None if r is None else r.returncode})
record={'scope':'Actual dopacket helper and body; mocked host Exec allocation/IPC, no guest or forced-removal acceptance.', 'inputs':{str(v.relative_to(root)):hashlib.sha256(v.read_bytes()).hexdigest() for v in [source,wrapper,here/'host.c',Path(__file__)]},'checks':rows}
(out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print([(x['name'],x['compile_exit'],x['run_exit']) for x in rows])
assert rows[0]['compile_exit']==0 and rows[0]['run_exit']==0
assert all(x['compile_exit']==0 and x['run_exit'] not in (0,None) for x in rows[1:])
