#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Actual hostdisk BeginIO/unit dispatch + Unix barrier over real host file I/O."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--tsan',action='store_true');a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4]
dev=root/'arch/all-hosted/devs/hostdisk/hostdisk_device.c';unix=root/'arch/all-unix/devs/hostdisk/hostdisk_host.c'
d=dev.read_text();u=unix.read_text()
# Preserve actual functions/bodies; only names colliding with libc read/write
# are scoped by macros around the host-independent unit source.
code=u[u.index('static ULONG error('):u.index('ULONG Host_Open(')]
code+=u[u.index('ULONG Host_Open('):u.index('static ULONG InternalGetGeometry(')]
constants=d[d.index('static const UWORD NSDSupported[]'):d.index('AROS_LH1(void, beginio,')]
body=d[d.index('    AROS_LIBFUNC_INIT',d.index('AROS_LH1(void, beginio,')):d.index('    AROS_LIBFUNC_EXIT',d.index('AROS_LH1(void, beginio,'))]
unit=d[d.index('static LONG read('):d.index('void eject(')]
# Include the real read/write functions, not geometry helper (not exercised).
unit=unit[:unit.index('static void addchangeint(')]
start=d.index('                switch(iotd->iotd_Req.io_Command)',d.index('static void unitentry('))
end=d.index('            } /* while((iotd',start)
dispatch=d[start:end]
code+='\n'+constants+'\nstatic void actual_beginio(struct IOExtTD *iotd,struct HostDiskBase *hdskBase){\n'+body+'\n}\n'
code+='\n#define read unit_read\n#define write unit_write\n'+unit+'\n#undef read\n#undef write\n'
dispatch=dispatch.replace('err = read(unit, iotd);','err = unit_read(unit, iotd);').replace('err = write(unit, iotd);','err = unit_write(unit, iotd);')
code+='\nstatic void actual_dispatch(struct unit *unit,struct IOExtTD *iotd){LONG err=0;\n'+dispatch+'\n}\n'
geometry_sources=[]
for variant in ['darwin','linux']:
 source=root/('arch/all-'+variant+'/hostdisk/geometry.c');geometry_sources.append(source)
 text=source.read_text();text=text[text.index('ULONG Host_DeviceGeometry('):]
 if 'static int deviceProbe(' in text:text=text[:text.index('static int deviceProbe(')]
 code+='\n'+text.replace('Host_DeviceGeometry(',variant+'_geometry(')
changes={
 'positive':None,
 'published-before-final-flags':('iotd->iotd_Req.io_Flags &= ~IOF_QUICK;\n            PutMsg(((struct unit *)iotd->iotd_Req.io_Unit)->port, &iotd->iotd_Req.io_Message);', 'PutMsg(((struct unit *)iotd->iotd_Req.io_Unit)->port, &iotd->iotd_Req.io_Message);\n            iotd->iotd_Req.io_Flags &= ~IOF_QUICK;'),
 'update-acknowledged-immediately':('        case CMD_UPDATE:\n        case CMD_READ:', '        case CMD_READ:'),
 'barrier-acknowledged-without-sync':('err = unit->hdskBase->update ?\n                        unit->hdskBase->update(unit) : IOERR_NOCMD;', 'err = 0;'),
 'host-error-lost':('return error(err ? err : EIO);','return 0;'),
 'all-io-cached-errno':('int *value = hdskBase->iface->__error();','int *value = fixture_cached_errno;'),
 'cached-foreign-errno':('host_errno = hdskBase->iface->__error();','host_errno = fixture_cached_errno;'),
 'no-interrupt-retry':('while (ret == -1 && err == EINTR);','while (0);'),
 'unsupported-advertised':('NSDSupported + (hdskBase->update ? 0 : 1)','NSDSupported'),
 'wrong-sync-fd':('ret = hdskBase->iface->fsync(Unit->file);','ret = hdskBase->iface->fsync(-1);'),
 'zero-write-spins':('        if(subsize == 0)\n            return IOERR_BADLENGTH;',''),
}
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
for name,change in changes.items():
 lane=out/name;lane.mkdir();actual=code
 if change:
  before,after=change;assert actual.count(before)==1,(name,actual.count(before));actual=actual.replace(before,after)
  if name=='update-acknowledged-immediately':actual=actual.replace('        case CMD_CLEAR:','        case CMD_UPDATE:\n        case CMD_CLEAR:',1)
 (lane/'actual.c').write_text(actual)
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-g','-pthread','-fsanitize='+('thread' if a.tsan else 'address,undefined'),'-I'+str(lane),str(here/'host.c'),'-o',str(lane/'host')]
 b=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(lane/'compile.log').write_text(b.stdout+b.stderr);exitcode=None
 if b.returncode==0:
  try:r=subprocess.run([str(lane/'host'),str(lane/'disk.img')],capture_output=True,text=True,timeout=12);exitcode=r.returncode;log=r.stdout+r.stderr
  except subprocess.TimeoutExpired as e:exitcode='timeout';log=(e.stdout or b'').decode()+(e.stderr or b'').decode()
  (lane/'run.log').write_text(log)
 rows.append({'name':name,'compile_exit':b.returncode,'run_exit':exitcode});print(rows[-1],flush=True)
inputs=[dev,unix,root/'arch/all-hosted/devs/hostdisk/hostdisk_device.h',root/'arch/all-unix/devs/hostdisk/hostdisk_host.h',here/'host.c',Path(__file__).resolve()]+geometry_sources
(out/'result.json').write_text(json.dumps({'scope':'Actual BeginIO, unit dispatch, read/write and Unix Host_Update/Seek/Read/Write, host FIFO over a real temporary file and real fsync. No AROS guest, media power-loss, other OS backend or forced-stop proof.','result':'PASS' if rows[0]['compile_exit']==0 and rows[0]['run_exit']==0 and all(r['compile_exit']==0 and r['run_exit'] not in (0,None) for r in rows[1:]) else 'FAIL','inputs':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs},'checks':rows},indent=2)+'\n')
assert rows[0]['compile_exit']==0 and rows[0]['run_exit']==0
assert all(r['compile_exit']==0 and r['run_exit'] not in (0,None) for r in rows[1:])
