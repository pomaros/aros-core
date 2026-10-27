#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Actual DOS transport/assign bodies, host pthread FIFO handler and DOS rwlock."""
import argparse, hashlib, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('--output',type=Path,required=True); p.add_argument('--tsan',action='store_true'); a=p.parse_args()
here=Path(__file__).resolve().parent; root=here.parents[4]
paths=[root/'rom/dos'/x for x in ['dopkt.c','dupassignlock.c','assignlock.c','assignpath.c','assignlate.c','remassignlist.c','assignaddtolist.c','assignadd.c','adddosentry.c','remdosentry.c']]
code=paths[0].read_text(); code=code[code.index('static SIPTR packet_error('):]
d=paths[1].read_text(); code+='\n'+d[d.index('struct assign_copy_guard'):d.index('/*****************************************************************************')]
signatures=['BPTR DupAssignLock(CONST_STRPTR name,struct MsgPort **handler,LONG *kind,SIPTR *result2)', 'LONG AssignLock(CONST_STRPTR name,BPTR lock)', 'BOOL AssignPath(CONST_STRPTR name,CONST_STRPTR path)', 'BOOL AssignLate(CONST_STRPTR name,CONST_STRPTR path)', 'LONG RemAssignList(CONST_STRPTR name,BPTR lock)', 'BOOL AssignAddToList(CONST_STRPTR name,BPTR lock,ULONG position)', 'BOOL AssignAdd(CONST_STRPTR name,BPTR lock)', 'LONG AddDosEntry(struct DosList *dlist)', 'LONG RemDosEntry(struct DosList *dlist)']
for source,sig in zip(paths[1:],signatures):
 s=source.read_text(); body=s[s.index('    AROS_LIBFUNC_INIT'):s.index('    AROS_LIBFUNC_EXIT')]; code+='\n'+sig+' {\n'+body+'\n}\n'
mutants={
 'positive':None,
 'release-before-enqueue':('internal_SendPkt(dp, port, replyPort);','if (after_send) { after_send(callback_data); after_send = NULL; }\n    internal_SendPkt(dp, port, replyPort);'),
 'wait-with-read':('if (after_send) after_send(callback_data);','if (0 && after_send) after_send(callback_data);'),
 'wrong-handler':('port = lock->fl_Task','port = entry->dol_Task'),
 'no-generation-bump':('    if (++IDosBase(DOSBase)->assignGenerationLow == 0)\n        ++IDosBase(DOSBase)->assignGenerationHigh;', '    (void)DOSBase;'),
 'same-under-write':('        same = SameLock(snapshot, lock) == LOCK_SAME;', '        LockDosList(LDF_ALL | LDF_WRITE);\n        same = SameLock(snapshot, lock) == LOCK_SAME;\n        UnLockDosList(LDF_ALL | LDF_WRITE);'),
 'deferred-lock-under-read':('            resolved = Lock(path, SHARED_LOCK);', '            LockDosList(LDF_ALL | LDF_READ);\n            resolved = Lock(path, SHARED_LOCK);'),
 'lost-copy-error':('if (!result && !error) error = ERROR_BAD_NUMBER;','if (!result && !error) error = 0;'),
}
# Restore the old placement in each real mutator; this must deadlock a handler
# which takes DOS WRITE before serving the already-enqueued COPY_DIR.
for fn in ['AssignLock','AssignPath','AssignLate']: mutants['free-under-write-'+fn]=fn
out=a.output.resolve(); out.mkdir(parents=True,exist_ok=False); rows=[]
for name,change in mutants.items():
 lane=out/name; lane.mkdir(); actual=code
 if isinstance(change,tuple):
  assert actual.count(change[0])==1,(name,actual.count(change[0])); actual=actual.replace(*change)
 elif change:
  start=actual.index(('LONG ' if change=='AssignLock' else 'BOOL ')+change+'('); ends=[v for v in [actual.find('\nBOOL ',start+1),actual.find('\nLONG ',start+1)] if v>=0]; end=min(ends) if ends else len(actual)
  segment=actual[start:end]; marker='    UnLockDosList(LDF_ALL | LDF_WRITE);'; assert segment.count(marker)==1
  segment=segment.replace(marker,'',1); point=segment.rindex('    return '); segment=segment[:point]+marker+'\n'+segment[point:]; actual=actual[:start]+segment+actual[end:]
 (lane/'actual.c').write_text(actual)
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-pthread','-fsanitize='+('thread' if a.tsan else 'address,undefined'),'-I'+str(lane),str(here/'host.c'),'-o',str(lane/'host')]
 b=subprocess.run(cmd,capture_output=True,text=True,timeout=30); (lane/'compile.log').write_text(b.stdout+b.stderr); exitcode=None
 if b.returncode==0:
  try:
   r=subprocess.run([str(lane/'host')],capture_output=True,text=True,timeout=8); exitcode=r.returncode; log=r.stdout+r.stderr
  except subprocess.TimeoutExpired as e: exitcode='timeout'; log=(e.stdout or b'').decode()+(e.stderr or b'').decode()
  (lane/'run.log').write_text(log)
 rows.append({'name':name,'compile_exit':b.returncode,'run_exit':exitcode}); print(rows[-1],flush=True)
record={'scope':'Actual transport, DupAssignLock and AssignLock/Path/Late/RemAssignList/AssignAddToList/AssignAdd bodies. Host Exec/FIFO and DOS-list implementation; no guest or forced-removal proof.','inputs':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths+[here/'host.c',Path(__file__)]},'checks':rows}
(out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
assert rows[0]['compile_exit']==0 and rows[0]['run_exit']==0
assert all(r['compile_exit']==0 and r['run_exit'] not in [None,0] for r in rows[1:])
