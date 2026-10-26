#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Exercise the actual registry constructor and list guard under contention."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[5]
    source = root / 'compiler/crt/posixc/__threadhook.c'
    guard = root / 'compiler/crt/stdc/__exec_list.h'
    body = source.read_text()
    start = body.index('static struct CRTThreadHooks *__get_threadhooks(')
    opening = body.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (body[end] == '{') - (body[end] == '}')
        end += 1
    function = body[start:end]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    inc = out / 'include'
    for name in ['aros/config.h', 'exec/execbase.h', 'proto/exec.h', 'resources/execlock.h',
                 'proto/execlock.h', 'aros/types/spinlock_s.h']:
        target = inc / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('/* host ABI declarations supplied by fixture */\n')
    fixture = r'''
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL;
typedef unsigned long ULONG;
typedef long LONG;
typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define LOCKF_DISABLE 1
#define SPINLOCK_MODE_READ 1
#define SPINLOCK_MODE_WRITE 2
#define CRT_THREADHOOKS_SEMNAME "crt.thread-hooks.v1"
struct Node { struct Node *next; char *ln_Name; };
struct List { struct Node *head; };
struct Library { int dummy; };
struct SignalSemaphore { struct Node ss_Link; int initialized; };
struct CRTThreadHooks { struct SignalSemaphore th_Sem; struct List th_Hooks; };
struct ExecBase { struct List SemaphoreList, LibList; };
static struct ExecBase execbase;
static struct ExecBase *SysBase = &execbase;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int held;
static int smp, fail_lock, fail_write_lock, opened;
static unsigned long allocations, frees;
static pthread_barrier_t allocate_barrier;
static struct Library resource;
static void *OpenResource(CONST_STRPTR name) {
 assert(!held); assert(!strcmp(name,"execlock.resource"));
 return smp ? &resource : NULL;
}
static int ObtainSystemLock(struct List *list, ULONG mode, ULONG flags) {
 assert(list==&SysBase->SemaphoreList || list==&SysBase->LibList); assert(mode==1 || mode==2);
 assert(flags==LOCKF_DISABLE); assert(!held);
 if(fail_lock || (fail_write_lock && mode==SPINLOCK_MODE_WRITE)) return FALSE;
 assert(!pthread_mutex_lock(&lock)); held=1; return TRUE;
}
static void ReleaseSystemLock(struct List *list, ULONG flags) {
 assert((list==&SysBase->SemaphoreList || list==&SysBase->LibList) && flags==LOCKF_DISABLE && held);
 held=0; assert(!pthread_mutex_unlock(&lock));
}
static void Disable(void) { assert(!held); assert(!pthread_mutex_lock(&lock)); held=1; }
static void Enable(void) { assert(held); held=0; assert(!pthread_mutex_unlock(&lock)); }
static struct Node *FindName(struct List *list, CONST_STRPTR name) {
 assert(held);
 for(struct Node *n=list->head;n;n=n->next) if(!strcmp(n->ln_Name,name))return n;
 return NULL;
}
static void Enqueue(struct List *list, struct Node *node) {
 assert(held);
 struct CRTThreadHooks *registry=(void *)node;
 assert(registry->th_Sem.initialized);
 assert(registry->th_Hooks.head==NULL);
 assert(!FindName(list,node->ln_Name));
 node->next=list->head; list->head=node;
}
static void *AllocMem(size_t size, int flags) {
 assert(!held && flags==(MEMF_PUBLIC|MEMF_CLEAR));
 void *p=calloc(1,size); assert(p);
 __atomic_add_fetch(&allocations,1,__ATOMIC_RELAXED);
 int rc=pthread_barrier_wait(&allocate_barrier); assert(!rc || rc==PTHREAD_BARRIER_SERIAL_THREAD);
 return p;
}
static void FreeMem(void *p,size_t size) {
 assert(!held && size==sizeof(struct CRTThreadHooks));
 __atomic_add_fetch(&frees,1,__ATOMIC_RELAXED); free(p);
}
static void InitSemaphore(struct SignalSemaphore *sem) { assert(!held); sem->initialized=1; }
#define NEWLIST(list) ((list)->head=NULL)
static struct Library *OpenLibrary(CONST_STRPTR name,int version) {
 assert(!held && !strcmp(name,"locale.library") && version==0);++opened;return &resource;
}
struct PosixCIntBase { int unused; };
struct CRTThreadHook { struct { void *mln_Succ; } th_Node; void (*th_Enter)(void); void (*th_Leave)(void); };
static struct CRTThreadHook __posixc_threadhook;
static LONG __posixc_threadbase_slot;
static int slot_failure, semaphore_held, additions;
static void posixc_thread_enter(void) { }
static void posixc_thread_leave(void) { }
static LONG AllocTaskStorageSlot(void) { assert(semaphore_held);return slot_failure ? 0 : 7; }
static void ObtainSemaphore(struct SignalSemaphore *sem) { assert(!held&&!semaphore_held&&sem->initialized);semaphore_held=1; }
static void ReleaseSemaphore(struct SignalSemaphore *sem) { assert(!held&&semaphore_held&&sem->initialized);semaphore_held=0; }
static void AddTail(struct List *list, struct Node *node) { assert(semaphore_held&&list);node->next=(void*)1;++additions; }
#include "__exec_list.h"
'''
    optional = (root/'compiler/crt/stdc/__optionallibs.c').read_text()
    begin = optional.index('static struct Library *__libfindandopen(')
    opening = optional.index('{', begin)
    depth, end = 1, opening+1
    while depth:
        depth += (optional[end]=='{')-(optional[end]=='}')
        end += 1
    fixture += optional[begin:end]+'\n'+function
    begin = body.index('static int __posixc_threadhook_init(')
    opening = body.index('{', begin)
    depth, end = 1, opening+1
    while depth:
        depth += (body[end]=='{')-(body[end]=='}')
        end += 1
    fixture += '\n'+body[begin:end]
    fixture += r'''
static void *worker(void *unused) {
 (void)unused;
 struct CRTThreadHooks *reg=__get_threadhooks(TRUE);
 if(fail_write_lock) { assert(!reg);return NULL; }
 assert(reg);
 for(int i=0;i<10000;i++) assert(__get_threadhooks(FALSE)==reg);
 return reg;
}
int main(int argc,char **argv) {
 assert(argc==3); smp=atoi(argv[1]);
#if defined(__AROSEXEC_SMP__)
 if(!smp) {
  struct __crt_list_guard guard;
  assert(!__crt_list_enter(&guard,&SysBase->SemaphoreList,SPINLOCK_MODE_WRITE));
  assert(!__get_threadhooks(TRUE));assert(!__posixc_threadhook_init(NULL));assert(!allocations&&!frees);
  assert(!__libfindandopen("locale.library",0)&&!opened);
  puts("PASS missing SMP resource refuses unguarded access");return 0;
 }
#endif
 struct Node library={NULL,"locale.library"};
 assert(!__libfindandopen("locale.library",0)&&!opened);
 SysBase->LibList.head=&library;
 assert(__libfindandopen("locale.library",0)==&resource && opened==1);
 if(smp) { fail_lock=1;assert(!__libfindandopen("locale.library",0));assert(opened==1);fail_lock=0; }
 fail_write_lock=atoi(argv[2]);
 assert(__get_threadhooks(FALSE)==NULL);
 if(smp) { fail_lock=1; assert(__get_threadhooks(TRUE)==NULL); fail_lock=0; }
 assert(!pthread_barrier_init(&allocate_barrier,NULL,8));
 pthread_t threads[8]; void *winner=NULL;
 for(int i=0;i<8;i++) assert(!pthread_create(&threads[i],NULL,worker,NULL));
 for(int i=0;i<8;i++) {void *value; assert(!pthread_join(threads[i],&value)); if(!winner)winner=value; assert(value==winner);}
 if(fail_write_lock) {
  assert(!winner && allocations==8 && frees==8 && !SysBase->SemaphoreList.head);
  puts("PASS failed publication lock frees every unpublished candidate");return 0;
 }
 assert(allocations==8 && frees==7);
 assert(SysBase->SemaphoreList.head==(void *)winner);
 assert(!SysBase->SemaphoreList.head->next);
 slot_failure=1;assert(!__posixc_threadhook_init(NULL));
 assert(!semaphore_held&&!additions&&!__posixc_threadhook.th_Node.mln_Succ);
 slot_failure=0;assert(__posixc_threadhook_init(NULL));assert(additions==1&&__posixc_threadbase_slot==7);
 assert(__posixc_threadhook_init(NULL));assert(additions==1&&!semaphore_held);
 free(winner);
 assert(!pthread_barrier_destroy(&allocate_barrier));
 puts("PASS single initialized registry, losing allocations freed, 80000 lookups");
}
'''
    # macOS has no pthread_barrier_t: use a bounded reusable condition barrier.
    fixture = fixture.replace('static pthread_barrier_t allocate_barrier;', '''
struct host_barrier { pthread_mutex_t mutex; pthread_cond_t cond; unsigned n; };
static struct host_barrier allocate_barrier = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
static void host_barrier_wait(struct host_barrier *b) {
 assert(!pthread_mutex_lock(&b->mutex));
 if(++b->n==8) assert(!pthread_cond_broadcast(&b->cond));
 while(b->n<8) assert(!pthread_cond_wait(&b->cond,&b->mutex));
 assert(!pthread_mutex_unlock(&b->mutex));
}
''').replace('int rc=pthread_barrier_wait(&allocate_barrier); assert(!rc || rc==PTHREAD_BARRIER_SERIAL_THREAD);', 'host_barrier_wait(&allocate_barrier);').replace(' assert(!pthread_barrier_init(&allocate_barrier,NULL,8));', '').replace(' assert(!pthread_barrier_destroy(&allocate_barrier));', '')
    result = {'sources': {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (source, guard, root/'compiler/crt/stdc/__optionallibs.c', Path(__file__).resolve())},
        'runs': [], 'mutants': []}
    marker = 'reg = (struct CRTThreadHooks *)FindName(&SysBase->SemaphoreList,'
    where = fixture.rindex(marker)
    stop = fixture.index(';', where)+1
    guard_text = guard.read_text()
    missing = '#if defined(__AROSEXEC_SMP__)\n    return FALSE;\n#else\n    Disable();\n    return TRUE;\n#endif'
    assert guard_text.count(missing)==1
    variants = {'current': (fixture,guard_text),
        'duplicate-registry': (fixture[:where]+'reg = NULL;'+fixture[stop:],guard_text),
        'missing-smp-lock': (fixture,guard_text.replace(missing,'    Disable();\n    return TRUE;'))}

    with tempfile.TemporaryDirectory(prefix='exec-registry-') as temporary:
        for name, (unit, guard_unit) in variants.items():
            variant = out/name
            variant.mkdir()
            (variant/'fixture.c').write_text(unit)
            (variant/'__exec_list.h').write_text(guard_unit)
            for configuration in (('smp',) if name=='missing-smp-lock' else ('up', 'smp')):
                binary = Path(temporary)/(name+'-'+configuration)
                cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-pthread',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-I'+str(inc), '-I'+str(guard.parent), str(variant/'fixture.c'), '-o', str(binary)]
                if configuration == 'smp': cmd.insert(1, '-D__AROSEXEC_SMP__=1')
                build = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
                (variant/(configuration+'-build.log')).write_text(build.stdout+build.stderr)
                build.check_returncode()
                cases = [('0','0'),('1','0'),('1','1')] if name=='current' else [('0','0')] if name=='missing-smp-lock' else [('1','0')]
                for resource, write_failure in cases:
                    run = subprocess.run([str(binary),resource,write_failure], capture_output=True,text=True,timeout=30)
                    (variant/(configuration+'-'+resource+'-'+write_failure+'.log')).write_text(run.stdout+run.stderr)
                    failure = '!__crt_list_enter(&guard,&SysBase->SemaphoreList,SPINLOCK_MODE_WRITE)' if name=='missing-smp-lock' else '!FindName(list,node->ln_Name)'
                    expected = run.returncode == 0 if name=='current' else run.returncode != 0 and failure in run.stderr
                    row = {'variant': name, 'configuration': configuration,'smp_resource':resource=='1',
                           'publication_lock_failure':write_failure=='1','exit':run.returncode,'expected':expected}
                    result['runs' if name=='current' else 'mutants'].append(row)
    result['result'] = 'PASS' if all(x['expected'] for x in result['runs']+result['mutants']) else 'FAIL'
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
    raise SystemExit(result['result']!='PASS')


if __name__ == '__main__':
    main()
