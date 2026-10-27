#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. APL-1.1.
"""Fault-inject the actual transport functions; never claim a DMA/guest proof."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4];source=root/'rom/hidds/virtiogfx/transport.c'
s=source.read_text();code=s[s.index('struct GPUHeader '):s.index('BOOL VirtioGPUGetSurface')]
code=code.replace('__asm__ volatile("dsb sy" ::: "memory");','atomic_thread_fence(memory_order_seq_cst);')
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
mutants={
 'positive':None,
 'irq-required':('return result>0;','return result>0 && gpu->interrupts!=before;'),
 'wrong-fence-accepted':('!(flags&1) || fence!=gpu->fence','!(flags&1)'),
 'reuse-pending':('if(!result){gpu->repaint=TRUE;ReleaseSemaphore(&gpu->lock);return FALSE;}','if(!result){gpu->pending=FALSE;}'),
 'lose-damage':('gpu->repaint=!okay;','gpu->repaint=FALSE;'),
 'no-full-repaint':('if(gpu->repaint){x=y=0;width=1024;height=768;}','(void)gpu->repaint;'),
 'timer-before-submit':None,
 'signal-reused-before-clear':('SetSignal(0,mask);FreeSignal(bit);','FreeSignal(bit);'),
}
rows=[]
for name,change in mutants.items():
 d=out/name;d.mkdir();body=code
 if change:
  old,new=change;assert body.count(old)==1;body=body.replace(old,new)
 if name=='timer-before-submit':
  arm='SendIO((struct IORequest *)gpu->timer);'
  assert body.count(arm)==1;body=body.replace(arm,'')
  body=body.replace('    device_t dev=&gpu->device;', '    '+arm+'\n    device_t dev=&gpu->device;',1)
 (d/'actual.c').write_text(body)
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I'+str(d),str(here/'host.c'),'-o',str(d/'host')]
 b=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(d/'compile.log').write_text(b.stdout+b.stderr)
 r=None
 if not b.returncode:
  r=subprocess.run([str(d/'host')],capture_output=True,text=True,timeout=10);(d/'run.log').write_text(r.stdout+r.stderr)
 rows.append({'name':name,'compile_exit':b.returncode,'run_exit':None if r is None else r.returncode})
result={'scope':'Actual command/reap/present bodies with fault-injected Exec and coherent-device publication. No real IRQ/cache/DMA/native runtime proof.','inputs':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in (source,here/'host.c',Path(__file__).resolve())},'checks':rows}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(rows)
assert rows[0]['compile_exit']==0 and rows[0]['run_exit']==0
assert all(r['compile_exit']==0 and r['run_exit'] not in (0,None) for r in rows[1:])
