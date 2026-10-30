#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Bounded real-MMU diagnostic; no production scheduler/W2 acceptance claim."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
KERNEL = ROOT / 'arch/aarch64-all/kernel'
EXPECTED = 'PASS domains=2 switches=2000 faults=2 denied=6 bridge=2 rejected_buffers=4 kernel_faults=4 guard_faults=2 nx_faults=2'


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


class Monitor:
    def __init__(self, path, process):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(3)
        deadline = time.monotonic() + 3
        while True:
            try:
                self.sock.connect(str(path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('QMP unavailable before execution')
                time.sleep(.02)
        self.stream = self.sock.makefile('rwb', buffering=0)
        self.greeting = json.loads(self.stream.readline())
        self.call('qmp_capabilities')

    def call(self, command, arguments=None):
        msg = {'execute': command}
        if arguments is not None:
            msg['arguments'] = arguments
        self.stream.write((json.dumps(msg)+'\n').encode())
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError('QMP closed')
            reply = json.loads(line)
            if 'error' in reply:
                raise RuntimeError(str(reply))
            if 'return' in reply:
                return reply['return']

    def close(self):
        self.stream.close()
        self.sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--buffer-proof', action='store_true', help='link the actual owned-buffer/service boundary')
    parser.add_argument('--cc', required=True)
    parser.add_argument('--qemu', default='qemu-system-aarch64')
    parser.add_argument('--allow-dirty-diagnostic', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('output must be a fresh directory outside the source checkout')
    cc = shutil.which(args.cc)
    qemu = shutil.which(args.qemu)
    if not cc or not qemu:
        parser.error('compiler and QEMU must exist')
    portable = ROOT / 'rom/kernel/mapping'
    fixture = HERE.parent / 'user_buffer_qemu'
    extra_scope = ['rom/kernel/mapping', str(fixture.relative_to(ROOT))] if args.buffer_proof else []
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--',
                 str(HERE.relative_to(ROOT)), str(KERNEL.relative_to(ROOT)), *extra_scope], cwd=ROOT))
    if dirty and not args.allow_dirty_diagnostic:
        parser.error('dirty inputs require --allow-dirty-diagnostic')
    inputs = [HERE/x for x in ('entry.S', 'probe.c', 'link.ld', 'run.py')]
    inputs += [KERNEL/x for x in ('mapping.c', 'mapping.h', 'setprotection.c', 'protection.h')]
    if args.buffer_proof:
        inputs += [fixture/'fixture.c', fixture/'freestanding/string.h']
        inputs += [portable/x for x in ('mapping.c','mapping.h','domain.c','domain.h','service.c','service.h','user_buffer.c','user_buffer.h','user_service.c','user_service.h')]
    before = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    out.mkdir(parents=True)
    result = {'result': 'FAIL', 'scope': 'single-CPU freestanding QEMU MMU diagnostic; not production AROS or full W2',
              'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'tool_versions': {name: subprocess.check_output([tool, '--version'], text=True).splitlines()[0] for name, tool in [('cc', cc), ('qemu', qemu)]},
              'dirty_inputs': dirty, 'input_hashes': before, 'records': []}
    variants = [('baseline', 0, None), ('shared-backing', 1, 'FAIL guest-data'),
                ('admit-supervisor', 2, 'FAIL guest-svc'), ('skip-protect', 3, 'FAIL completion-counts'),
                ('expose-kernel', 4, 'FAIL guest-kernel-access')]
    controls = {}
    if args.buffer_proof:
        controls = {
            'reset-record-generation': ('user_service.c', 'r->token = generation;', 'r->token = generation - generation;', 'FAIL buffer-cross-queue-generation'),
            'stale-worker': ('user_service.c', 'r->token != token || (r->state != KS_RUNNING', '!token || (r->state != KS_RUNNING', 'FAIL buffer-cross-queue-worker'),
            'foreign-domain': ('user_buffer.c', 's->principal == principal', 'principal != 0', 'FAIL buffer-foreign-domain'),
            'stale-domain': ('user_buffer.c', 's->generation == (uint32_t)(handle >> 32)', 's->generation != 0', 'FAIL buffer-stale-domain'),
            'foreign-page': ('user_buffer.c', 'm->owner != s', '0', 'FAIL buffer-foreign-page'),
            'readonly-ledger': ('user_buffer.c', '(p->protection & access) == access', '1', 'FAIL buffer-readonly-ledger'),
            'cancel-early-release': ('user_service.c', 'if (r->state == KS_RUNNING || r->state == KS_CANCELLED)', 'if (r->state == KS_CANCELLED)', 'FAIL buffer-unmap-retained'),
        }
        variants = [('baseline', 0, None)] + [(n, 0, v[3]) for n,v in controls.items()]
        result['scope'] = 'single-CPU freestanding real-MMU owned-buffer/service boundary; not production AROS restricted scheduling'
    try:
        for name, mutant, expected_failure in variants:
            elf = out/(name+'.elf')
            cmd = [cc, '--target=aarch64-none-elf', '-std=c11', '-O1', '-g', '-ffreestanding',
                   '-fno-builtin', '-fno-stack-protector', '-mgeneral-regs-only', '-Wall', '-Wextra', '-Werror',
                   '-nostdlib', '-fuse-ld=lld', '-Wl,--build-id=none', '-Wl,-T,'+str(HERE/'link.ld'),
                   '-I'+str(KERNEL), '-DMUTANT='+str(mutant), str(HERE/'entry.S'), str(HERE/'probe.c'),
                   str(KERNEL/'mapping.c'), str(KERNEL/'setprotection.c'), '-o', str(elf)]
            if args.buffer_proof:
                cmd += ['-I'+str(portable), '-I'+str(fixture/'freestanding'), str(fixture/'fixture.c')]
                for filename in ('mapping.c','domain.c','service.c','user_buffer.c','user_service.c'):
                    source = portable/filename
                    if name in controls and controls[name][0] == filename:
                        _, old, new, _ = controls[name]
                        original = source.read_text()
                        if original.count(old) != 1:
                            raise RuntimeError(name+': mutation no longer uniquely matches')
                        source = out/(name+'-'+filename)
                        source.write_text(original.replace(old,new))
                    cmd.append(str(source))
            build = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            (out/(name+'-build.log')).write_text(build.stdout+build.stderr)
            record = {'name': name, 'build_command': cmd, 'build_exit': build.returncode}
            result['records'].append(record)
            if build.returncode:
                raise RuntimeError(name+': build failure')
            record['elf_sha256'] = digest(elf)
            with tempfile.TemporaryDirectory(prefix='aros-domain-') as tmp:
                sock = Path(tmp)/'qmp'
                command = [qemu, '-machine', 'virt,virtualization=off', '-cpu', 'cortex-a72',
                           '-accel', 'tcg', '-smp', '1', '-m', '128M', '-display', 'none',
                           '-monitor', 'none', '-serial', 'stdio', '-no-reboot', '-S',
                           '-qmp', 'unix:'+str(sock)+',server=on,wait=off',
                           '-semihosting-config', 'enable=on,target=native', '-kernel', str(elf)]
                record['qemu_command'] = command
                proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                monitor = None
                try:
                    monitor = Monitor(sock, proc)
                    status = monitor.call('query-status')
                    regs = monitor.call('human-monitor-command', {'command-line':'info registers'})
                    (out/(name+'-preflight.txt')).write_text(json.dumps(status)+'\n'+regs)
                    if status['running'] or 'PC=0000000040080000' not in regs:
                        raise RuntimeError('independent stopped-register preflight failed')
                    monitor.call('cont')
                    try:
                        stdout, stderr = proc.communicate(timeout=10)
                    except subprocess.TimeoutExpired:
                        monitor.call('stop')
                        record['timeout_registers'] = monitor.call('human-monitor-command', {'command-line':'info registers'})
                        monitor.call('quit')
                        stdout, stderr = proc.communicate(timeout=3)
                        record['timeout'] = True
                    (out/(name+'.stdout')).write_text(stdout)
                    (out/(name+'.stderr')).write_text(stderr)
                    record.update(exit=proc.returncode, output=stdout, stderr=stderr)
                    if expected_failure is None:
                        expected_lines = ['READY restricted-domain-probe']
                        if args.buffer_proof:
                            expected_lines.append('PASS buffers domains=2 copied=2 refused=4 cancelled=4 retired=2 cross_queue=1')
                        expected_lines.append(EXPECTED)
                        passed = proc.returncode == 0 and stdout.splitlines() == expected_lines and not stderr
                    else:
                        passed = proc.returncode == 1 and stdout.splitlines() == ['READY restricted-domain-probe', expected_failure] and not stderr
                    record['result'] = 'PASS' if passed and not record.get('timeout') else 'FAIL'
                    if record['result'] != 'PASS':
                        raise RuntimeError(name+': unexpected runtime result')
                finally:
                    if proc.poll() is None:
                        proc.kill()
                        proc.communicate(timeout=3)
                    if monitor:
                        monitor.close()
        if before != {str(p.relative_to(ROOT)): digest(p) for p in inputs}:
            raise RuntimeError('inputs changed during run')
        result['result'] = 'DIAGNOSTIC_PASS' if dirty else 'PASS'
        result['limits'] = ['no production scheduler', 'no asynchronous native worker', 'no SMP',
                            'no alias-wide production W^X', 'caches disabled', 'no complete fault upcall API']
    except Exception as error:
        result['error'] = str(error)
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({'result': result['result'], 'error': result.get('error'), 'out': str(out)}))
    return int(result['result'] == 'FAIL')


if __name__ == '__main__':
    raise SystemExit(main())
