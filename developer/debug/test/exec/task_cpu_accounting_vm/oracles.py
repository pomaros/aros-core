# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Recompute public CPU accounting contracts from raw, complete guest records."""
import re
BEGIN='[CPU21] guest command begin'
END='[CPU21] guest command PASS'
BOOT='[B2Q] AROS startup contract PASS'
BAD=re.compile(r'\b(?:FAIL|ERROR|PANIC|FATAL|GURU|CRASH)\b|Synchronous Exception|\[B2Q\] EXCEPTION|segmentation fault|assertion.*failed',re.I)

FIRMWARE_DIAGNOSTICS={
    'Tpm2GetCapabilityPcrs fail!',
    'Error: Image at 0005FDB6000 start failed: 00000001',
    'Error: Image at 0005FCB2000 start failed: Unsupported',
    'Error: Image at 0005FC37000 start failed: Not Found',
    'Error: Image at 0005FB5D000 start failed: Aborted',
}

def serial_failure(text):
    started=False
    for line in text.splitlines():
        if line.startswith('AROSBOOTAA64:') or re.search(r'\[(?:B2[A-Z]?|VM|CPU21|AROS)[^]]*\]',line): started=True
        if not started and line in FIRMWARE_DIAGNOSTICS: continue
        if BAD.search(line): return True
    return False

def expected_rejection(serial):
    expected='[CPU21] FAIL busy/idle accounting contract'
    allowed=(expected,'[CPU21] guest command FAIL','[B2Q] FAIL: stage-two startup')
    if serial.count(expected)!=1 or '[CPU21] PASS' in serial: return False
    lines=serial.splitlines()
    if any(lines.count(line)!=1 for line in allowed): return False
    positions=[lines.index(line) for line in allowed]
    if positions!=sorted(positions): return False
    remainder='\n'.join(line for line in serial.splitlines() if line not in allowed)
    return not serial_failure(remainder)

def metrics(serial):
    def one(pattern):
        matches=re.findall(pattern,serial,re.M)
        if len(matches)!=1: raise ValueError('missing/duplicate record '+pattern)
        return matches[0]
    windows=tuple(map(int,one(r'^\[CPU21\] windows base_cpu=(\d+) base_usage=(\d+) busy_cpu=(\d+) busy_usage=(\d+) idle_cpu=(\d+) idle_usage=(\d+) busy_us=(\d+) idle_us=(\d+) iterations=(\d+)\r?$')))
    base,base_usage,busy,busy_usage,idle,idle_usage,busy_us,idle_us,iterations=windows
    if not (base_usage==0 and busy>=base+250000 and idle>=busy and 0x40000000<=busy_usage<=0xffffffff and 0<=idle_usage<=0x0ccccccc and idle_usage<busy_usage and busy_us>=1000000 and idle_us>=1000000 and iterations>0 and idle-busy<=50000 and busy-base<=busy_us+10000):
        raise ValueError('busy/idle accounting contract')
    expected=min(0xffffffff, (busy-base)*0xffffffff//busy_us)
    if abs(busy_usage-expected)>0xffffffff//20:
        raise ValueError('usage disagrees with measured CPUTime/wall window by more than five percentage points')
    before,after,elapsed=map(int,one(r'^\[CPU21\] current before_cpu=(\d+) after_cpu=(\d+) elapsed_us=(\d+)\r?$'))
    if not (elapsed>=50000 and after>=before+40000 and after-before<=elapsed+10000): raise ValueError('uncommitted current slice')
    one(r'^\[CPU21\] churn created=32 exited=32 live_queries=32\r?$')
    one(r'^\[CPU21\] sleep unqualified\r?$')
    ordered=(BEGIN,'[CPU21] begin usage_scale=4294967295 window_us=250000 cpus=1','[CPU21] windows','[CPU21] current','[CPU21] churn','[CPU21] sleep unqualified','[CPU21] PASS',END,BOOT)
    if any(serial.count(t)!=1 for t in ordered): raise ValueError('missing/duplicate proof')
    positions=[serial.index(t) for t in ordered]
    if positions!=sorted(positions): raise ValueError('proof order')
    return {'baseline_cpu_us':base,'busy_cpu_us':busy,'idle_cpu_us':idle,
            'busy_usage_q32':busy_usage,'expected_busy_usage_q32':expected,'idle_usage_q32':idle_usage,
            'busy_wall_us':busy_us,'idle_wall_us':idle_us,'iterations':iterations,
            'current_slice_cpu_us':after-before,'current_slice_wall_us':elapsed,
            'exited_children':32,'sleep_qualified':False,'smp_qualified':False}

def inspect(serial,host,trace,elapsed,exitcode,orderly,timedout):
    errors=[]; measured={}
    if serial_failure(serial) or BAD.search(host): errors.append('reserved failure token')
    if trace is None: errors.append('missing QEMU trace')
    elif trace.strip(): errors.append('nonempty QEMU guest_errors/unimp trace')
    try: measured=metrics(serial)
    except ValueError as e: errors.append(str(e))
    if timedout: errors.append('deadline expired')
    if not orderly or exitcode!=0: errors.append('unexpected QEMU termination')
    return {'result':'FAIL' if errors else 'PASS','errors':errors,'metrics':measured,
            'workload_host_seconds':elapsed,'qemu_exit_code':exitcode,
            'qmp_orderly_quit':orderly,'timed_out':timedout}
