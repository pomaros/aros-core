#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# Licensed under the AROS Public License (APL), version 1.1.
"""Run the production once body with a shared gate and racing host tasks."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[4]
text=(root/'rom/exec/initsemaphoreonce.c').read_text()
body=text.split('AROS_LIBFUNC_INIT',1)[1].split('AROS_LIBFUNC_EXIT',1)[0]
prefix=r"""
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
typedef uint32_t ULONG; typedef uintptr_t IPTR; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define SSINITF_TRY 1
struct SignalSemaphore {atomic_int calls; atomic_int ready;};
struct Private {struct SignalSemaphore SemaphoreInitSem;} base;
static void *SysBase;
#define PrivExecBase(p) (&base)
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static void ObtainSemaphore(struct SignalSemaphore *s) {assert(s==&base.SemaphoreInitSem);assert(!pthread_mutex_lock(&gate));}
static BOOL AttemptSemaphore(struct SignalSemaphore *s) {assert(s==&base.SemaphoreInitSem);return !pthread_mutex_trylock(&gate);}
static void ReleaseSemaphore(struct SignalSemaphore *s) {assert(s==&base.SemaphoreInitSem);assert(!pthread_mutex_unlock(&gate));}
static void InitSemaphore(struct SignalSemaphore *s) {atomic_fetch_add(&s->calls,1);usleep(1000);atomic_store(&s->ready,1);}
static BOOL actual(struct SignalSemaphore *sigSem, ULONG *state, ULONG flags) {
"""
suffix=r"""
}
static struct SignalSemaphore shared;
static ULONG published;
static atomic_int go;
static void *worker(void *unused) {
    (void)unused;
    while (!atomic_load(&go)) sched_yield();
    for (int i=0;i<100;i++) {
        assert(actual(&shared,&published,0));
        assert(atomic_load(&shared.ready)==1);
    }
    return NULL;
}
int main(void) {
    (void)SysBase;
    ULONG state=0;struct SignalSemaphore sem={0};
    assert(!actual(NULL,&state,0));assert(!actual(&sem,NULL,0));
    assert(!actual(&sem,&state,2));state=2;assert(!actual(&sem,&state,0));state=0;
    unsigned char storage[12];
    ULONG *unaligned=(ULONG *)(((uintptr_t)storage+3u)&~(uintptr_t)3u);
    assert(!actual(&sem,(ULONG *)((char *)unaligned+1),0));
    assert(!pthread_mutex_lock(&gate));
    assert(!actual(&sem,&state,SSINITF_TRY));
    assert(state==0&&atomic_load(&sem.calls)==0);
    state=1;assert(actual(&sem,&state,SSINITF_TRY));
    assert(!pthread_mutex_unlock(&gate));
    pthread_t tasks[16];
    for(int i=0;i<16;i++)assert(!pthread_create(&tasks[i],NULL,worker,NULL));
    atomic_store(&go,1);
    for(int i=0;i<16;i++)assert(!pthread_join(tasks[i],NULL));
    assert(published==1&&atomic_load(&shared.calls)==1);
    puts("InitSemaphoreOnce actual body PASS 16 racing clients, 1600 calls, one init; invalid state and nonblocking busy gate refused");
}
"""
with tempfile.TemporaryDirectory(prefix='semaphore-once-') as d:
    p=Path(d);(p/'fixture.c').write_text(prefix+body+suffix)
    subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread',str(p/'fixture.c'),'-o',str(p/'fixture')],check=True)
    subprocess.run([str(p/'fixture')],check=True,timeout=15)
