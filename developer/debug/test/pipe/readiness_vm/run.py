#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Strict bounded PX3 pipe readiness VM run; preserve logs, trace and QMP exit evidence."""
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

BEGIN = '[SMP0] guest command begin (two 10-second windows)'
END = '[SMP0] guest command PASS'
BOOT = '[B2Q] AROS startup contract PASS'
BAD = re.compile(r'\b(?:FAIL|PANIC|FATAL|GURU)\b|Synchronous Exception|\[B2Q\] EXCEPTION|segmentation fault|assertion.*failed', re.I)

def serial_failure(text):
    # Stock EDK2 probes a TPM even when none is attached. Permit only this
    # exact firmware line, only before any AROS proof; retain raw log bytes.
    guest_started = False
    for line in text.splitlines():
        if any(marker in line for marker in ('[B2', '[VM]', '[SMP0]', '[PX3]')): guest_started = True
        if not guest_started and line == 'Tpm2GetCapabilityPcrs fail!': continue
        if BAD.search(line): return True
    return False


PROOF = '[PX3] PASS pipe: nonblock-empty=would-block notify-on-data=1 notify-if-already-readable=1 partial-read-returns=1 interactive=1'
CONTROL = '[PX3] (control) unregistered: signals=0'
START = '[PX3] begin real DOS pipe readiness'

def inspect(serial, host, trace, elapsed, exitcode, orderly, timedout):
    errors = []
    if serial_failure(serial) or BAD.search(host): errors.append('reserved failure token')
    if trace.strip(): errors.append('QEMU guest_errors/unimp trace is nonempty')
    tokens = (BEGIN, START, CONTROL, PROOF, END, BOOT)
    if any(serial.count(token) != 1 for token in tokens):
        errors.append('missing or duplicate proof')
    elif [serial.index(t) for t in tokens] != sorted(serial.index(t) for t in tokens):
        errors.append('proof order')
    if timedout: errors.append('deadline expired')
    if not orderly or exitcode != 0: errors.append('unexpected QEMU termination')
    return {'result':'FAIL' if errors else 'PASS','errors':errors,
            'workload_seconds':elapsed, 'qemu_exit_code':exitcode,
            'qmp_orderly_quit':orderly,'timed_out':timedout}


def selftest():
    good = '\n'.join((BEGIN, START, CONTROL, PROOF, END, BOOT))+'\n'
    assert inspect(good,'','',1,0,True,False)['result']=='PASS'
    assert inspect('Tpm2GetCapabilityPcrs fail!\n'+good,'','',1,0,True,False)['result']=='PASS'
    for values in [('[B2A] early guest proof\nTpm2GetCapabilityPcrs fail!\n'+good,'','',1,0,True,False),
                   (good+'\nPANIC\n','','',1,0,True,False),
                   (good+'\nTpm2GetCapabilityPcrs fail!\n','','',1,0,True,False),
                   (good,'fatal: crash','',1,0,True,False),
                   (good,'','unimplemented register',1,0,True,False),
                   (good,'','',1,-11,False,False),
                   (good,'','',1,0,True,True),
                   (good+END,'','',1,0,True,False),
                   (good.replace(CONTROL,''),'','',1,0,True,False),
                   (good.replace(START,CONTROL).replace(PROOF,START),'','',1,0,True,False)]:
        assert inspect(*values)['result']=='FAIL', values
    print('PASS verifier fixtures; no guest evidence')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--esp',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--timeout',type=float,default=90)
    parser.add_argument('--settle',type=float,default=3)
    parser.add_argument('--backend', choices=('hvf','tcg'), default='hvf')
    parser.add_argument('--selftest',action='store_true')
    parser.add_argument('--dump-memory', action='append', default=[], metavar='ADDRESS:SIZE', help='save bounded diagnostic physical memory after stopping guest')
    args=parser.parse_args()
    if args.selftest: selftest();return
    if not args.esp or not args.output: parser.error('--esp and --output required')
    if args.output.exists(): parser.error('output must be new; use a numbered attempt')
    if args.timeout<=20 or args.settle<1: parser.error('allow >20s timeout and >=1s post-proof observation')
    out=args.output.resolve();out.mkdir(parents=True)
    shutil.copytree(args.esp,out/'esp')
    serial=out/'serial.log';host=out/'host.log';trace=out/'qemu.trace'
    firmware=Path('/opt/homebrew/share/qemu/edk2-aarch64-code.fd')
    # Keep Unix socket short even when the artifact directory is deeply nested.
    with tempfile.TemporaryDirectory(prefix='px3-qmp-') as temp:
        qmp=Path(temp)/'qmp.sock'
        command=['qemu-system-aarch64','-accel',args.backend,'-M','virt,acpi=off,gic-version=3',
                 '-cpu','host' if args.backend=='hvf' else 'max','-m','512','-smp','1','-bios',str(firmware),
                 '-drive',f'format=raw,file=fat:rw:{out / "esp"}', '-device','ramfb',
                 '-display','none','-no-reboot','-serial',f'file:{serial}',
                 '-d','guest_errors,unimp','-D',str(trace),
                 '-qmp',f'unix:{qmp},server=on,wait=off']
        (out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
        started=time.monotonic();began=ended=proved=None;timedout=False;orderly=False;failure=None
        def read(path): return path.read_text(errors='replace') if path.exists() else ''
        with host.open('w') as log:
            proc=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
            try:
                while True:
                    now=time.monotonic();text=read(serial)
                    if BEGIN in text and began is None: began=now
                    if END in text and ended is None: ended=now
                    if serial_failure(text) or BAD.search(read(host)) or read(trace).strip():
                        failure='failure token or QEMU trace';break
                    if proc.poll() is not None: failure='early QEMU exit';break
                    if now-started >= args.timeout: timedout=True;break
                    if END in text and BOOT in text:
                        if proved is None: proved=now
                        if now-proved>=args.settle: break
                    time.sleep(0.05)
                if proc.poll() is None:
                    with socket.socket(socket.AF_UNIX) as sock, (out/'qmp.jsonl').open('w') as evidence:
                        sock.settimeout(3);sock.connect(str(qmp))
                        with sock.makefile('rwb') as stream:
                            def receive():
                                raw=stream.readline()
                                if not raw: raise RuntimeError('unexpected QMP EOF')
                                value=json.loads(raw);evidence.write(json.dumps({'receive':value})+'\n');evidence.flush();return value
                            receive()
                            def call(name, arguments=None):
                                request={'execute':name,'id':name}
                                if arguments:request['arguments']=arguments
                                evidence.write(json.dumps({'send':request})+'\n');evidence.flush()
                                stream.write(json.dumps(request).encode()+b'\n');stream.flush()
                                while True:
                                    response=receive()
                                    if response.get('id')==name:
                                        if 'error' in response:raise RuntimeError(str(response))
                                        return response['return']
                            call('qmp_capabilities')
                            state=call('query-status')
                            if state.get('status')!='running':failure='unexpected QEMU state '+str(state)
                            call('stop')
                            registers=call('human-monitor-command',{'command-line':'info registers'})
                            (out/'registers.txt').write_text(str(registers))
                            for index, spec in enumerate(args.dump_memory):
                                address, size = (int(n, 0) for n in spec.split(':'))
                                if address < 0 or not 0 < size <= 16*1024*1024: raise ValueError('invalid bounded memory dump')
                                call('pmemsave', {'val':address, 'size':size, 'filename':str(out/f'memory-{index}.bin')})
                            call('quit')
                            orderly=not failure and not timedout and proved is not None
                    proc.wait(timeout=5)
            except Exception as error:
                failure=f'{type(error).__name__}: {error}'
            finally:
                if proc.poll() is None:
                    orderly=False;proc.terminate()
                    try:proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=5)
        # Inspect only after process exit, including anything emitted after PASS.
        verdict=inspect(read(serial),read(host),read(trace),
                        ended-began if ended is not None and began is not None else None,
                        proc.returncode,orderly,timedout)
        if failure:verdict['errors'].append(failure);verdict['result']='FAIL'
        verdict.update({'actual_aros_guest':True,'backend':args.backend,'cpus':1,
                        'post_proof_observation_seconds':args.settle,
                        'bundle_sha256':hashlib.sha256((out/'esp/AROS/GEN0.BND').read_bytes()).hexdigest(),
                        'firmware_sha256':hashlib.sha256(firmware.read_bytes()).hexdigest(),
                        'seconds':round(time.monotonic()-started,3)})
        (out/'verdict.json').write_text(json.dumps(verdict,indent=2)+'\n')
        print(json.dumps(verdict,indent=2))
        raise SystemExit(0 if verdict['result']=='PASS' else 1)

if __name__=='__main__':main()
