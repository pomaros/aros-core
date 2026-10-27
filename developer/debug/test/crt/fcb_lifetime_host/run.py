#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Actual POSIX file operation lifetime paths at mocked DOS/table boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--tsan', action='store_true')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[4]
posixc = root / 'compiler/crt/posixc'
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
inputs = []


def read(name):
    path = posixc / name
    inputs.append(path)
    return path.read_text()


def block(text, signature):
    start = text.index(signature)
    begin = text.index('{', start)
    depth = 0
    for end in range(begin, len(text)):
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
            if depth == 0:
                return text[start:end + 1]
    raise ValueError(signature)


def syscall(name, signature):
    text = read(name + '.c')
    start = text.index('\n{\n', text.index('    INTERNALS'))
    return signature + '\n' + text[start:text.rfind('}') + 1]


header = read('__fdesc.h')
source = read('__fdesc.c')
reclock = read('__reclock.c')
parts = {
    'FCB_TYPES': block(header, 'typedef struct _fcb\n') + ' fcb;',
    'FDESC_TYPES': block(header, 'typedef struct _fdesc\n') + ' fdesc;',
    'FCB_LOCK': block(header, 'static inline void __fcb_lock('),
    'FCB_UNLOCK': block(header, 'static inline void __fcb_unlock('),
    'FCB_RETAIN': block(header, 'static inline BOOL __fcb_retain('),
    'HOLD_FILE': block(source, 'fcb *__holdfcb('),
    'RELEASE_FILE': block(source, 'void __releasefcb('),
    'RELEASE_RECORD': block(reclock, 'void __posixc_reclock_release_file('),
    'READ': syscall('read', 'static ssize_t ar_read(int fd,void *buf,size_t count)'),
    'WRITE': syscall('write', 'static ssize_t ar_write(int fd,const void *buf,size_t count)'),
    'FSTAT': syscall('fstat', 'static int ar_fstat(int fd,struct stat *sb)'),
    'FSTAT64': syscall('fstat64', 'static int ar_fstat64(int fd,struct stat64 *sb)'),
    'FSYNC': syscall('fsync', 'static int ar_fsync(int fd)'),
    'CLOSE': syscall('close', 'static int ar_close(int fd)'),
    'DUP': block(source, 'int __dupfd('),
    'DUP2': syscall('dup2', 'static int ar_dup2(int oldfd,int newfd)'),
    'CLOSEDIR': syscall('closedir', 'static int ar_closedir(struct directory *dir)'),
}
text = (here / 'host.c').read_text()
for marker, implementation in parts.items():
    placeholder = '/* ' + marker + ' */'
    assert text.count(placeholder) == 1, marker
    implementation = re.sub(r'\bclose\b', 'ar_close', implementation)
    text = text.replace(placeholder, implementation)
controls = {
    'unretained-operation': ('if (__fcb_retain(descriptor->fcb))', 'if (TRUE)',
                              'Assertion failed: (!atomic_load(&handle_closes[11])&&!atomic_load(&block_frees)&&!atomic_load(&record_puts))'),
    'missing-operation-release': ('    __releasefcb(file);', '    /* missing operation release */',
                                   'Assertion failed: (atomic_load(&handle_closes[11])==1&&atomic_load(&block_frees)==1&&atomic_load(&record_puts)==1)'),
    'missing-record-release': ('    __posixc_reclock_release_file(file);', '    /* lost record ownership */',
                                'Assertion failed: (!f->reclock)'),
    'missing-dup2-rollback': ('        __releasefcb(newfdesc->fcb);', '        /* leaked duplication reference */',
                              'Assertion failed: (ar_dup2(5,7)==-1&&file->opencount==1)'),
    'reference-wrap': ('if (!count || count == (unsigned int)-1)', 'if (!count)',
                       'Assertion failed: (ar_read(5,&byte,1)==-1&&errno==EOVERFLOW)'),
}
controls.update({
    'metadata-unretained': (parts['FSTAT'], parts['FSTAT'].replace(
        '__holdfcb(fd, &admission_error)',
        '(admission_error=0,__getfdesc(fd)?__getfdesc(fd)->fcb:NULL)'),
        'metadata operation owns the admitted file'),
    'metadata-error-overwritten': ('    errno = saved_errno;', '    (void)saved_errno;',
                                   'Assertion failed: (a.error==EIO)'),
    'metadata-lock-held': (parts['FSTAT'], parts['FSTAT'].replace(
        '    __fcb_unlock(file);', '    /* retained metadata lock */'),
        'Assertion failed: (isfile && !metadata_file_depth)'),
    'sync-release-lost': (parts['FSYNC'], parts['FSYNC'].replace(
        '    __releasefcb(file);', '    /* lost sync reference */'),
        'Assertion failed: (atomic_load(&handle_closes[11])==1 && atomic_load(&block_frees)==1)'),
})
cases = [('production', text, None)]
for name, (before, after, expected) in controls.items():
    assert before in text, name
    cases.append((name, text.replace(before, after), expected))
records = []
for name, actual, expected in cases:
    path = output / (name + '.c')
    path.write_text(actual)
    binary = output / name
    command = ['clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g', '-pthread',
               '-fsanitize=' + ('thread' if args.tsan else 'address,undefined'), str(path), '-o', str(binary)]
    with (output / (name + '-compile.log')).open('w') as log:
        process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    row = {'name': name, 'command': command, 'compile_exit': process.returncode,
           'run_exit': None, 'expected_cause': expected, 'pass': False}
    if process.returncode == 0:
        logpath = output / (name + '.log')
        with logpath.open('w') as log:
            try:
                process = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, timeout=20)
                row['run_exit'] = process.returncode
            except subprocess.TimeoutExpired:
                row['run_exit'] = 124
        row['pass'] = ((row['run_exit'] not in (0, 124) and expected in logpath.read_text())
                       if expected else row['run_exit'] == 0)
    records.append(row)
result = {
    'result': 'PASS' if all(row['pass'] for row in records) else 'FAIL',
    'records': records,
    'inputs': {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
               for path in [*inputs, here / 'host.c', Path(__file__)]},
    'scope': ('Actual fcb retain/hold/release, record metadata final release, read, write, '
              'close, dup, dup2 and closedir bodies. Modeled recursive descriptor table '
              'and DOS/file registry boundary. Concurrent close/reuse before native I/O, '
              'error preservation, overflow and duplicate rollback. No whole fd/cwd '
              'namespace, other local file operations, guest or filesystem acceptance.'),
}
(output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(result['result'])
for row in records:
    if not row['pass']:
        print(row['name'], row['compile_exit'], row['run_exit'])
raise SystemExit(result['result'] != 'PASS')
