/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL), Version 1.1.
 * Read-shared page across permission changes; no disk I/O. */
#include <exec/tasks.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <aros/kernel.h>
#include <aros/debug.h>

APTR KernelBase;
#define READERS 7
#define ROUNDS 64
#define WORDS 1024
#ifdef SHARED_CODE
#define TEST_NAME "SMP-SharedCode"
#define PAGE_COUNT 2
#else
#define TEST_NAME "SMP-SharedPage"
#define PAGE_COUNT 1
#endif
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while(0)
struct Shared { volatile uint32_t *page[PAGE_COUNT]; uint32_t epoch, stop; };
struct Reader {
    struct Shared *shared;
    ULONG cpu, tid, first, last, seen, done, failed;
    uint64_t scans;
};
static uint64_t counter(void) {
    /* No test-side ISB: instruction synchronization must come from the
     * kernel protection operation, not the reader's timeout sample. */
    uint64_t v; __asm__ volatile("mrs %0,cntvct_el0":"=r"(v)); return v;
}
#ifndef SHARED_CODE
static uint32_t pattern(unsigned i) { return UINT32_C(0x13579bdf) ^ (i * 73U); }
#else
static void generate(volatile uint32_t *page, unsigned epoch) {
    page[0]=UINT32_C(0x52800000)|(epoch<<5); /* mov w0,#epoch */
    page[1]=UINT32_C(0xd65f03c0); /* ret */
}
#endif
static LONG ReadPage(void) {
    struct Task *self=FindTask(NULL);
    struct Reader *r=self->tc_UserData;
    uint64_t hz; __asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
    uint64_t start=counter();
    r->first=KrnGetCPUNumber();
    __atomic_store_n(&r->tid,self->tc_UnionETask.tc_ETask->et_UniqueID,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&r->shared->stop,__ATOMIC_ACQUIRE)) {
        uint32_t epoch=__atomic_load_n(&r->shared->epoch,__ATOMIC_ACQUIRE);
#ifdef SHARED_CODE
        if(!epoch) { /* No executable page is published yet. */
            if(counter()-start>hz*30U) { r->failed=2;break; }
            continue;
        }
        unsigned result=((unsigned (*)(void))(uintptr_t)r->shared->page[epoch&1U])();
        if(result!=epoch) r->failed=1;
#else
        for(unsigned i=0;i<WORDS;i++)
            if(r->shared->page[0][i]!=pattern(i)) { r->failed=1; break; }
#endif
        if(r->failed) break;
        ++r->scans;
        __atomic_store_n(&r->seen,epoch,__ATOMIC_RELEASE);
        if(counter()-start>hz*30U) { r->failed=2; break; }
    }
    r->last=KrnGetCPUNumber();
    __atomic_store_n(&r->done,1,__ATOMIC_RELEASE);
    return r->failed ? RETURN_FAIL : RETURN_OK;
}
static int wait_epoch(struct Reader *readers, unsigned count, unsigned epoch) {
    for(unsigned attempt=0;attempt<200;attempt++) {
        unsigned ready=0;
        for(unsigned i=0;i<count;i++) {
            if(__atomic_load_n(&readers[i].done,__ATOMIC_ACQUIRE)) return 0;
            if(__atomic_load_n(&readers[i].seen,__ATOMIC_ACQUIRE)>=epoch) ++ready;
        }
        if(ready==count) return 1;
        Delay(1);
    }
    return 0;
}
int main(void) {
    struct Shared shared={0};
    struct Reader readers[READERS]={0};
    unsigned created=0,transitions=0,rounds=0;
    int ok=1;
    KernelBase=OpenResource("kernel.resource");
    if(!KernelBase || KrnGetCPUCount()!=8 || KrnGetCPUNumber()!=0) {
        PutStr("Run " TEST_NAME " on CPU0 with eight online CPUs\n"); return RETURN_FAIL;
    }
    intptr_t operation=KrnGetSystemAttr(KATTR_PageProtect);
    if(operation==0 || operation==-1) return RETURN_FAIL;
    KRN_PageProtect protect=(KRN_PageProtect)operation;
    for(unsigned i=0;i<PAGE_COUNT;i++) {
        shared.page[i]=KrnAllocPages(NULL,4096,MEMF_CLEAR);
        if(!shared.page[i]) { ok=0;goto cleanup; }
    }
#ifdef SHARED_CODE
    generate(shared.page[1],1);
    if(!protect((void *)shared.page[1],4096,MAP_Readable|MAP_Executable)) {
        ok=0;goto cleanup;
    }
#else
    for(unsigned i=0;i<WORDS;i++) shared.page[0][i]=pattern(i);
#endif
    REPORT("[" TEST_NAME "] start readers=7 rounds=64 bytes=4096 pages=%lu\n",(unsigned long)PAGE_COUNT);
    for(unsigned i=0;i<READERS;i++) {
        struct Reader *r=&readers[i];r->shared=&shared;r->cpu=i+1;
        APTR affinity=KrnAllocCPUMask();if(!affinity) break;
        KrnClearCPUMask(affinity);KrnGetCPUMask(r->cpu,affinity);
        if(!CreateNewProcTags(NP_Entry,(IPTR)ReadPage,NP_Name,(IPTR)TEST_NAME "Reader",
            NP_UserData,(IPTR)r,NP_StackSize,256*1024,NP_Affinity,(IPTR)affinity,
            NP_NotifyOnDeath,TRUE,TAG_DONE)) { KrnFreeCPUMask(affinity);break; }
        ++created;
    }
    if(created!=READERS) ok=0;
    __atomic_store_n(&shared.epoch,1,__ATOMIC_RELEASE);
    if(ok && !wait_epoch(readers,created,1)) ok=0;
    for(unsigned n=0;ok && n<ROUNDS;n++) {
#ifdef SHARED_CODE
        /* All readers acknowledged the current version after returning from
         * it. The other page has no remaining callers and may be rewritten. */
        volatile uint32_t *next=shared.page[(n+2)&1U];
        if(!protect((void *)next,4096,MAP_Readable|MAP_Writable)) { ok=0;break; }
        ++transitions;
        generate(next,n+2);
        if(!protect((void *)next,4096,MAP_Readable|MAP_Executable)) { ok=0;break; }
        ++transitions;
#else
        if(!protect((void *)shared.page[0],4096,MAP_Readable)) { ok=0;break; }
        ++transitions;
        if(!protect((void *)shared.page[0],4096,MAP_Readable|MAP_Writable)) { ok=0;break; }
        ++transitions;
#endif
        __atomic_store_n(&shared.epoch,n+2,__ATOMIC_RELEASE);
        if(!wait_epoch(readers,created,n+2)) { ok=0;break; }
        ++rounds;
        if((n&15)==15) REPORT("[" TEST_NAME "] progress rounds=%lu\n",(unsigned long)rounds);
    }
    __atomic_store_n(&shared.stop,1,__ATOMIC_RELEASE);
    /* Even on failure, keep page, code and contexts until every child exits. */
    for(unsigned i=0;i<created;i++) {
        struct Reader *r=&readers[i];
        while(!__atomic_load_n(&r->tid,__ATOMIC_ACQUIRE)) Delay(1);
        struct ETask *child=(struct ETask *)ChildWait(r->tid);
        if((IPTR)child<=CHILD_ACTIVE) {
            REPORT("[" TEST_NAME "] FAIL unreaped child; retaining context\n");
            for(;;) Wait(0);
        }
        ChildFree(child->et_UniqueID);
        if(!r->done || r->failed || !r->scans || r->first!=r->cpu || r->last!=r->cpu || r->seen!=65) ok=0;
        REPORT("[" TEST_NAME "] reader cpu=%lu first=%lu last=%lu seen=%lu scans=%llu error=%lu reaped=1\n",
            (unsigned long)r->cpu,(unsigned long)r->first,(unsigned long)r->last,
            (unsigned long)r->seen,(unsigned long long)r->scans,(unsigned long)r->failed);
    }
cleanup:
    for(unsigned i=0;i<PAGE_COUNT;i++) if(shared.page[i]) {
        if(protect((void *)shared.page[i],4096,MAP_Readable|MAP_Writable))
            KrnFreePages((void *)shared.page[i],4096);
        else ok=0; /* Retain allocation if restoration fails. */
    }
    REPORT("[" TEST_NAME "] result=%s readers=%lu rounds=%lu transitions=%lu\n",
        ok?"PASS":"FAIL",(unsigned long)created,(unsigned long)rounds,(unsigned long)transitions);
    return ok?RETURN_OK:RETURN_FAIL;
}
