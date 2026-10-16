#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Retain a strict UP TCG indicator command/panel smoke proof."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time
p=argparse.ArgumentParser()
p.add_argument('--esp',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
shutil.copy2(Path(__file__),out/'run.py')
shutil.copytree(a.esp,out/'esp');esp=out/'esp'
manifest=json.loads((esp/'provenance.json').read_text())
actual={str(f.relative_to(esp)) for f in esp.rglob('*') if f.is_file() and f.name!='provenance.json'}
assert actual==set(manifest['files'])
for name,digest in manifest['files'].items(): assert hashlib.sha256((esp/name).read_bytes()).hexdigest()==digest
bad=re.compile(r'\b(?:ERROR|FAIL|PANIC|CRASH|FATAL|GURU)\b|Synchronous Exception|segmentation fault',re.I)
def read(name):
    f=out/name
    return f.read_text(errors='replace') if f.exists() else ''
errors=[];injected=set();escaped=False;finished=None;normal=False;events=[]
with tempfile.TemporaryDirectory(prefix='ind-qmp-') as temporary:
    qmp=Path(temporary)/'q.sock'
    cmd=['qemu-system-aarch64','-accel','tcg','-M','virt,acpi=on,gic-version=3,msi=gicv2m',
         '-cpu','cortex-a72','-m','512','-smp','1','-bios','/opt/homebrew/share/qemu/edk2-aarch64-code.fd',
         '-drive',f'format=raw,file=fat:rw:{esp}','-device','ramfb','-display','none','-no-reboot',
         '-serial',f'file:{out}/firmware.log','-chardev',f'file,id=vmconsole,path={out}/serial.log',
         '-device','virtio-serial-pci,disable-legacy=on,id=vmconsole',
         '-device','virtconsole,chardev=vmconsole,bus=vmconsole.0,name=aros.console',
         '-device','virtio-gpu-pci,id=gpu,xres=1024,yres=768','-device','qemu-xhci,id=xhci',
         '-device','usb-kbd,bus=xhci.0','-device','usb-tablet,bus=xhci.0',
         '-qmp',f'unix:{qmp},server=on,wait=off','-d','guest_errors,unimp','-D',str(out/'guest-errors.log')]
    (out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    started=time.monotonic()
    with (out/'host.log').open('w') as host,(out/'qmp.jsonl').open('w') as log:
        proc=subprocess.Popen(cmd,stdout=host,stderr=subprocess.STDOUT)
        try:
            while not qmp.exists() and proc.poll() is None and time.monotonic()-started<5:time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(3);sock.connect(str(qmp))
                with sock.makefile('rwb') as stream:
                    def receive():
                        raw=stream.readline()
                        if not raw:raise RuntimeError('QMP EOF')
                        reply=json.loads(raw);log.write(json.dumps({'receive':reply})+'\n');log.flush();return reply
                    receive();sequence=0
                    def call(name,arguments=None):
                        global sequence
                        sequence+=1;request={'execute':name,'id':sequence}
                        if arguments:request['arguments']=arguments
                        log.write(json.dumps({'send':request})+'\n');log.flush()
                        stream.write(json.dumps(request).encode()+b'\n');stream.flush()
                        while True:
                            reply=receive()
                            if reply.get('id')==sequence:
                                if 'error' in reply:raise RuntimeError(str(reply))
                                return reply['return']
                    def capture(name):
                        target=out/(name+'.ppm')
                        call('screendump',{'filename':str(target),'device':'gpu','head':0,'format':'ppm'})
                        events.append({'event':name,'seconds':time.monotonic()-started,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
                    call('qmp_capabilities')
                    while time.monotonic()-started<70 and proc.poll() is None:
                        serial=read('serial.log')
                        if bad.search(serial) or bad.search(read('host.log')):raise RuntimeError('failure token')
                        if not escaped and '[INDPROBE] escape-ready' in serial:
                            capture('panel-open')
                            call('send-key',{'keys':[{'type':'qcode','data':'esc'}],'hold-time':100})
                            escaped=True;events.append({'event':'escape-injected'})
                        def click(x,y,hold=.1):
                            call('input-send-event',{'device':'gpu','head':0,'events':[
                                {'type':'abs','data':{'axis':'x','value':int(x*32767/1023)}},
                                {'type':'abs','data':{'axis':'y','value':int(y*32767/767)}}]})
                            time.sleep(.1)
                            for down in (True,False):
                                call('input-send-event',{'device':'gpu','head':0,'events':[{'type':'btn','data':{'down':down,'button':'left'}}]})
                                time.sleep(hold if down else .1)
                        for phase in ['hotkey','outside','glyph','glyph-second','programmatic']:
                            if phase in injected or '[INDPROBE] '+phase+'-ready' not in serial: continue
                            if phase=='hotkey':call('send-key',{'keys':[{'type':'qcode','data':'meta_l'},{'type':'qcode','data':'b'}],'hold-time':100})
                            elif phase=='outside':click(900,650)
                            else:
                                found=re.findall(r'\[TILE2\] displayed id=1 frame=\d+ tick=\d+ x=(-?\d+) y=(-?\d+) w=64 h=64 visible=1',serial)
                                if not found:raise RuntimeError('glyph geometry not recorded')
                                x,y=map(int,found[-1]);click(x+24,y+24,1.1 if phase=='glyph-second' else .1)
                            injected.add(phase);events.append({'event':phase+'-injected'})
                        for phase in ['disabled-glyph','enabled-glyph']:
                            if '[INDPROBE] '+phase+'-ready' in serial and phase not in injected:
                                found=re.findall(r'anchor=(-?\d+),(-?\d+)',serial)
                                if not found:raise RuntimeError('glyph anchor missing')
                                x,y=map(int,found[-1]);click(x+24,y-40)
                                injected.add(phase);events.append({'event':phase+'-injected'})
                        if '[INDPROBE] sound-mute-ready' in serial and 'sound-mute' not in injected:
                            capture('sound-panel')
                            found=re.findall(r'mute=(-?\d+),(-?\d+),(\d+),(\d+)',serial)
                            if not found:raise RuntimeError('mute geometry not recorded')
                            x,y,w,h=map(int,found[-1]);click(x+w//2,y+h//2)
                            injected.add('sound-mute');events.append({'event':'sound-mute-injected'})
                        if '[INDPROBE] wifi-unavailable-ready' in serial and 'wifi-unavailable' not in injected:
                            capture('wifi-unavailable');injected.add('wifi-unavailable')
                        if '[INDPROBE] wifi-panel-ready'  in serial and 'wifi-panel' not in injected:
                            capture('wifi-panel');injected.add('wifi-panel')
                        if '[INDPROBE] PASS' in serial:
                            if finished is None:finished=time.monotonic();capture('after-probe')
                            if time.monotonic()-finished>=3:break
                        time.sleep(.05)
                    if not finished:raise RuntimeError('fixture did not finish')
                    if not escaped or len(injected-{'wifi-panel','wifi-unavailable','sound-mute','disabled-glyph','enabled-glyph'})!=5:raise RuntimeError('missing real input phase')
                    state=call('query-status')
                    if not state.get('running'):raise RuntimeError('guest stopped')
                    call('stop');(out/'registers.txt').write_text(str(call('human-monitor-command',{'command-line':'info registers'})))
                    call('quit');proc.wait(timeout=8);normal=proc.returncode==0
        except Exception as exc:errors.append(str(exc))
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=5)
                except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=5)
if not normal:errors.append('QMP normal exit missing')
if not (out/'guest-errors.log').exists() or (out/'guest-errors.log').stat().st_size:errors.append('guest trace missing or nonempty')
if bad.search(read('serial.log')) or bad.search(read('host.log')):errors.append('final failure token scan')
if '[INDPROBE] PASS' not in read('serial.log'):errors.append('fixture PASS missing')
verdict={'result':'FAIL' if errors else 'PASS','errors':errors,'events':events,'qemu_exit':proc.returncode,
         'scope':'UP TCG command/panel smoke only; no full WM15, actual providers or hardware acceptance',
         'sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in out.iterdir() if f.is_file()}}
(out/'result.json').write_text(json.dumps(verdict,indent=2)+'\n');print(json.dumps(verdict))
raise SystemExit(bool(errors))
