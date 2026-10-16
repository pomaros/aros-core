/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL), Version 1.1.
 * Concurrent per-task socket library lifecycle. No network traffic or disk I/O.
 * Stack-owned worker state and executable survive until every child is reaped. */
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <aros/debug.h>
#include <stdint.h>
APTR KernelBase;
#define WORKERS 4
#define ROUNDS 32
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while (0)
struct Group { ULONG stop; };
struct Worker {
    struct Group *group;
    struct Task *parent,*task;
    ULONG parent_signal,go_signal,cpu,tid,ready,done,error;
    ULONG first,last,samples,wrong,rounds;
    IPTR first_base;
    uint64_t begin,end;
};
static uint64_t counter(void) {
    uint64_t value; __asm__ volatile("mrs %0,cntvct_el0":"=r"(value));return value;
}
static void sample(struct Worker *w) {
    ULONG cpu=KrnGetCPUNumber();
    if(!w->samples)w->first=cpu;
    w->last=cpu;w->samples++;if(cpu!=w->cpu)w->wrong++;
}
static LONG Work(void) {
    struct Task *self=FindTask(NULL);struct Worker *w=self->tc_UserData;
    struct Library *base=NULL;BYTE bit=AllocSignal(-1);w->task=self;
    if(bit>=0){w->go_signal=1UL<<bit;SetSignal(0,w->go_signal);}
    __atomic_store_n(&w->tid,self->tc_UnionETask.tc_ETask->et_UniqueID,__ATOMIC_RELEASE);
    Signal(w->parent,w->parent_signal);
    if(bit<0){w->error=1;goto ready_failure;}
    Wait(w->go_signal);w->begin=counter();sample(w);
    base=OpenLibrary("bsdsocket.library",0);sample(w);
    w->first_base=(IPTR)base;if(!base)w->error=2;
    __atomic_store_n(&w->ready,1,__ATOMIC_RELEASE);Signal(w->parent,w->parent_signal);
    /* Keep the first reference alive until the parent compares all bases. */
    Wait(w->go_signal);
    for(unsigned i=0;!w->error&&!__atomic_load_n(&w->group->stop,__ATOMIC_ACQUIRE)&&i<ROUNDS;i++) {
        struct Library *again=OpenLibrary("bsdsocket.library",0);sample(w);
        if(!again){w->error=3;break;}
        if(again!=base)w->error=4;
        CloseLibrary(again);sample(w);
        CloseLibrary(base);base=NULL;sample(w);
        if(w->error)break;
        w->rounds++;
        if(i+1<ROUNDS){base=OpenLibrary("bsdsocket.library",0);sample(w);if(!base)w->error=5;}
    }
    if(base)CloseLibrary(base);
    sample(w);w->end=counter();FreeSignal(bit);
    goto done;
ready_failure:
    __atomic_store_n(&w->ready,1,__ATOMIC_RELEASE);Signal(w->parent,w->parent_signal);
done:
    __atomic_store_n(&w->done,1,__ATOMIC_RELEASE);
    return w->error||w->wrong?RETURN_FAIL:RETURN_OK;
}
int main(void) {
    static const ULONG cpus[WORKERS]={1,3,4,7};
    struct Group group={0};struct Worker workers[WORKERS]={0};
    unsigned created=0;BOOL ok=TRUE,distinct=TRUE;
    uint64_t begin=0,end=UINT64_MAX;BYTE bit;
    KernelBase=OpenResource("kernel.resource");
    if(!KernelBase||KrnGetCPUCount()!=8||KrnGetCPUNumber()!=0||!(FindTask(NULL)->tc_Flags&TF_ETASK))return RETURN_FAIL;
    bit=AllocSignal(-1);if(bit<0)return RETURN_FAIL;
    REPORT("[SMP-Bases] start workers=4 rounds=32\n");
    for(unsigned i=0;i<WORKERS;i++) {
        struct Worker *w=&workers[i];w->group=&group;w->cpu=cpus[i];
        w->parent=FindTask(NULL);w->parent_signal=1UL<<bit;
        APTR affinity=KrnAllocCPUMask();if(!affinity)break;
        KrnClearCPUMask(affinity);KrnGetCPUMask(w->cpu,affinity);
        if(!CreateNewProcTags(NP_Entry,(IPTR)Work,NP_Name,(IPTR)"SMP-SocketBase",
            NP_UserData,(IPTR)w,NP_StackSize,256*1024,NP_Affinity,(IPTR)affinity,
            NP_Cli,TRUE,NP_NotifyOnDeath,TRUE,TAG_DONE)) {KrnFreeCPUMask(affinity);break;}
        created++;
    }
    for(unsigned i=0;i<created;i++)
        while(!__atomic_load_n(&workers[i].tid,__ATOMIC_ACQUIRE))Wait(1UL<<bit);
    for(unsigned i=0;i<created;i++)if(workers[i].go_signal)Signal(workers[i].task,workers[i].go_signal);
    for(unsigned i=0;i<created;i++)
        while(!__atomic_load_n(&workers[i].ready,__ATOMIC_ACQUIRE))Wait(1UL<<bit);
    for(unsigned i=0;i<created;i++) {
        if(!workers[i].first_base||workers[i].error)distinct=FALSE;
        for(unsigned j=0;j<i;j++)if(workers[j].first_base==workers[i].first_base)distinct=FALSE;
    }
    if(created!=WORKERS||!distinct){ok=FALSE;__atomic_store_n(&group.stop,1,__ATOMIC_RELEASE);}
    REPORT("[SMP-Bases] barrier created=%lu distinct=%lu\n",(unsigned long)created,(unsigned long)distinct);
    for(unsigned i=0;i<created;i++)if(workers[i].go_signal)Signal(workers[i].task,workers[i].go_signal);
    for(unsigned i=0;i<created;i++) {
        struct Worker *w=&workers[i];struct ETask *child=(struct ETask *)ChildWait(w->tid);
        if((IPTR)child<=CHILD_ACTIVE){REPORT("[SMP-Bases] FAIL unreaped; retaining context\n");for(;;)Wait(0);}
        ChildFree(child->et_UniqueID);
        if(!__atomic_load_n(&w->done,__ATOMIC_ACQUIRE)||w->error||w->wrong||w->rounds!=ROUNDS||w->samples!=130||w->first!=w->cpu||w->last!=w->cpu)ok=FALSE;
        if(w->begin>begin)begin=w->begin;if(w->end<end)end=w->end;
        REPORT("[SMP-Bases] worker cpu=%lu first=%lu last=%lu base=%llu rounds=%lu samples=%lu wrong=%lu error=%lu begin=%llu end=%llu reaped=1\n",
            (unsigned long)w->cpu,(unsigned long)w->first,(unsigned long)w->last,(unsigned long long)w->first_base,
            (unsigned long)w->rounds,(unsigned long)w->samples,(unsigned long)w->wrong,(unsigned long)w->error,
            (unsigned long long)w->begin,(unsigned long long)w->end);
    }
    FreeSignal(bit);if(end<=begin)ok=FALSE;
    REPORT("[SMP-Bases] result=%s created=%lu overlap=%llu\n",ok?"PASS":"FAIL",(unsigned long)created,(unsigned long long)(end>begin?end-begin:0));
    return ok?RETURN_OK:RETURN_FAIL;
}
