#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Exercise production presentation-wake admission with modeled Exec I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', required=True, type=Path)
out = parser.parse_args().out.resolve()
out.mkdir(exist_ok=False)
source = (root / 'rom/intuition/windowcomposition.c').read_text()
actual = source[source.index('struct WindowCompositionWake\n{'):
                source.index('struct WindowComposition\n{')]
prefix = r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define PA_IGNORE 0
#define IND_WRITEEVENT 11
#define IECLASS_NULL 0
struct SignalSemaphore { pthread_mutex_t mutex; };
struct MsgPort { int mp_SigBit, mp_Flags; void *mp_SigTask; };
struct IORequest { int unused; };
struct InputEvent { int ie_Class; };
struct IOStdReq { void *io_Device, *io_Unit; int io_Command;
                 size_t io_Length; void *io_Data; };
struct WindowCompositionWake;
struct IntuitionBase { struct WindowCompositionWake *CompositionWake;
                      struct SignalSemaphore IntuiActionLock; struct IOStdReq *InputIO; };
#define GetPrivIBase(b) (b)
static int fail_at, alloc_attempts, objects, signals;
static atomic_int in_flight, replied, sends, stop;
static _Thread_local int lock_depth;
static struct IOStdReq *known_request;
static void InitSemaphore(struct SignalSemaphore *s) { assert(!pthread_mutex_init(&s->mutex, NULL)); }
static void ObtainSemaphore(struct SignalSemaphore *s) { assert(!pthread_mutex_lock(&s->mutex)); ++lock_depth; }
static void ReleaseSemaphore(struct SignalSemaphore *s) { assert(lock_depth); --lock_depth; assert(!pthread_mutex_unlock(&s->mutex)); }
static void *allocate(size_t size) {
    if (++alloc_attempts == fail_at) return NULL;
    void *p = calloc(1, size); assert(p); ++objects; return p;
}
static void *AllocVec(size_t size, int flags) { (void)flags; return allocate(size); }
static void FreeVec(void *p);
static struct MsgPort *CreateMsgPort(void) {
    struct MsgPort *p = allocate(sizeof(*p));
    if (p) { p->mp_SigBit = 7; p->mp_SigTask = (void *)1; ++signals; }
    return p;
}
static void DeleteMsgPort(struct MsgPort *p) {
    if (p->mp_SigBit >= 0) --signals;
    --objects; free(p);
}
static void *CreateIORequest(struct MsgPort *p, size_t size) { assert(p); return allocate(size); }
static void DeleteIORequest(struct IORequest *p) { --objects; free(p); }
static void FreeSignal(int bit) { assert(bit == 7); --signals; }
static void *GetMsg(struct MsgPort *port) {
    assert(port && lock_depth);
    return atomic_exchange(&replied, 0) ? known_request : NULL;
}
static void SendIO(struct IORequest *io) {
    struct IOStdReq *r = (void *)io;
    assert(!lock_depth);
    assert(r == known_request && r->io_Command == IND_WRITEEVENT);
    assert(r->io_Length == sizeof(struct InputEvent));
    assert(((struct InputEvent *)r->io_Data)->ie_Class == IECLASS_NULL);
    assert(!atomic_load(&replied));
    assert(!atomic_exchange(&in_flight, 1));
    atomic_fetch_add(&sends, 1);
}
'''
suffix = r'''
static void FreeVec(void *p) {
    struct WindowCompositionWake *w = p;
    assert(!pthread_mutex_destroy(&w->lock.mutex)); --objects; free(p);
}
static void finish_io(void) {
    assert(atomic_exchange(&in_flight, 0));
    assert(!atomic_exchange(&replied, 1));
}
static void cleanup(struct IntuitionBase *base) {
    struct WindowCompositionWake *w = base->CompositionWake;
    if (!w) return;
    assert(!atomic_load(&in_flight));
    if (w->pending) { ObtainSemaphore(&w->lock); assert(GetMsg(w->port) == w->request); ReleaseSemaphore(&w->lock); }
    DeleteIORequest((void *)w->request); DeleteMsgPort(w->port); FreeVec(w);
    base->CompositionWake = NULL;
}
static void *producer(void *arg) {
    for (unsigned i = 0; i < 1000; ++i) request_composition_wake(arg);
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    while (!atomic_load(&stop) || atomic_load(&in_flight)) {
        if (atomic_load(&in_flight)) finish_io();
        else sched_yield();
    }
    return NULL;
}
int main(void) {
    alarm(20);
    struct IOStdReq input = {.io_Device=(void *)1, .io_Unit=(void *)2};
    struct IntuitionBase base = {.InputIO=&input}; InitSemaphore(&base.IntuiActionLock);
    request_composition_wake(&base); assert(!sends);
    for (fail_at = 1; fail_at <= 3; ++fail_at) {
        alloc_attempts = 0;
        assert(!WindowCompositionInitWake(&base));
        assert(!objects && !signals && !base.CompositionWake && !lock_depth);
    }
    fail_at = 0; alloc_attempts = 0;
    assert(WindowCompositionInitWake(&base));
    int allocations = alloc_attempts;
    assert(WindowCompositionInitWake(&base) && alloc_attempts == allocations);
    known_request = base.CompositionWake->request;
    assert(known_request->io_Device == input.io_Device && known_request->io_Unit == input.io_Unit);
    assert(!signals && !base.CompositionWake->port->mp_SigTask);
    for (unsigned i = 0; i < 64; ++i) request_composition_wake(&base);
    assert(sends == 1 && in_flight);
    finish_io(); request_composition_wake(&base); assert(sends == 2 && in_flight);
    finish_io(); /* The completed request is reused by the first stress caller. */
    pthread_t workers[8], device;
    assert(!pthread_create(&device, NULL, consumer, NULL));
    for (unsigned i = 0; i < 8; ++i) assert(!pthread_create(&workers[i], NULL, producer, &base));
    for (unsigned i = 0; i < 8; ++i) assert(!pthread_join(workers[i], NULL));
    atomic_store(&stop, 1); assert(!pthread_join(device, NULL));
    assert(alloc_attempts == allocations && !lock_depth);
    cleanup(&base); assert(!objects && !signals);
    assert(!pthread_mutex_destroy(&base.IntuiActionLock.mutex));
    printf("COMPOSITION_WAKE HOST_PASS failure_unwind3 coalesced64 callers8000 sends=%d no_callback_allocations\n", sends);
    return 0;
}
'''
variants = {
    'actual': actual,
    'reuse-pending': actual.replace('if (!wake->pending)', 'if (TRUE)'),
    'submit-locked': actual.replace('ReleaseSemaphore(&wake->lock);\n    if (submit) SendIO((struct IORequest *)wake->request);',
                                    'if (submit) SendIO((struct IORequest *)wake->request);\n    ReleaseSemaphore(&wake->lock);'),
}
results = {}
for name, code in variants.items():
    assert name == 'actual' or code != actual
    path = out / name
    path.mkdir()
    (path / 'test.c').write_text(prefix + code + suffix)
    subprocess.run(['clang', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-pthread', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    run = subprocess.run([str(path / 'test')], capture_output=True, text=True, timeout=25)
    (path / 'output.txt').write_text(run.stdout + run.stderr)
    assert (run.returncode == 0) == (name == 'actual'), (name, run.stderr)
    if name != 'actual':
        assert ('in_flight' if name == 'reuse-pending' else '!lock_depth') in run.stderr
    results[name] = run.returncode
(out / 'result.json').write_text(json.dumps({
    'state': 'HOST_PASS', 'scope': 'actual wake admission; modeled Exec I/O, not native presentation',
    'source_sha256': hashlib.sha256(source.encode()).hexdigest(), 'returncodes': results}, indent=2) + '\n')
print((out / 'actual/output.txt').read_text(), end='')
