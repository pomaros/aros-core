/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/kernel.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/lists.h>
#include <aros/debug.h>
#include <aros/apple/restricted.h>
APTR KernelBase;
extern const unsigned char RestrictedCode[], RestrictedCodeEnd[];
extern const unsigned char RestrictedBadStack[], RestrictedBadStackEnd[];
extern const unsigned char RestrictedQueued[], RestrictedQueuedEnd[];
extern const unsigned char RestrictedWorkerDeath[], RestrictedWorkerDeathEnd[];
static const struct AROSRestrictedAPI *api;
static struct AROSRestrictedResult results[2], retired_result;
static struct Task *retirement_parent;
static ULONG retirement_signal;
static unsigned retirement_taken;
static unsigned worker_replies, worker_ack;
static unsigned stop, done[2], beats, after_fault;
static struct Task *identities[4];
#define CHECK(x) do { if (!(x)) { bug("[ISOVM] FAIL line=%d\n",__LINE__); abort(); } } while (0)
static void *heartbeat(void *unused)
{
    (void)unused;
    identities[2] = FindTask(NULL);
    while (!__atomic_load_n(&stop,__ATOMIC_ACQUIRE)) {
        ++beats;
        if (__atomic_load_n(&done[0],__ATOMIC_ACQUIRE)) ++after_fault;
        KrnSwitch();
    }
    return NULL;
}
static void *worker(void *unused)
{
    (void)unused;identities[3]=FindTask(NULL);
    while (!__atomic_load_n(&stop,__ATOMIC_ACQUIRE)) {
        struct AROSRestrictedWork work;
        if (api->take(&work)) {
            CHECK(work.length==8);
            uint64_t operation;memcpy(&operation,work.bytes,8);
            unsigned held=operation==0xe2 || operation==0xe3;
            if (held) {
                unsigned state;
                while ((state=api->state(&work))==AROS_RESTRICTED_RUNNING) KrnSwitch();
                CHECK(state==AROS_RESTRICTED_CANCELLED);
            }
            for (unsigned i=0;i<work.length;++i) work.bytes[i]^=0x5a;
            CHECK(api->complete(&work));
            CHECK(!api->complete(&work)); /* Stale completion is rejected. */
            CHECK(api->state(&work)==0);
            if (held) ++worker_ack;else ++worker_replies;
        }
        KrnSwitch();
    }
    return NULL;
}
static void *client(void *argument)
{
    unsigned id = (unsigned)(uintptr_t)argument;
    identities[id] = FindTask(NULL);
    CHECK(api->run(RestrictedCode,(size_t)(RestrictedCodeEnd-RestrictedCode),
        (id+1)*4096,&results[id]));
    __atomic_store_n(&done[id],1,__ATOMIC_RELEASE);
    return NULL;
}
static void retired_worker(void)
{
    struct AROSRestrictedWork work;
    while (!api->take(&work)) KrnSwitch();
    CHECK(work.length==8);
    SetTaskPri(FindTask(NULL),retirement_parent->tc_Node.ln_Pri+1);
    __atomic_store_n(&retirement_taken,1,__ATOMIC_RELEASE);
    Signal(retirement_parent,retirement_signal);
    for (;;) Wait(0);
}
static void *retirement_client(void *unused)
{
    (void)unused;
    CHECK(api->run(RestrictedWorkerDeath,(size_t)(RestrictedWorkerDeathEnd-RestrictedWorkerDeath),16384,&retired_result));
    return NULL;
}
static void prove_worker_retirement(void)
{
    retirement_parent=FindTask(NULL);
    BYTE bit=AllocSignal(-1);CHECK(bit>=0);retirement_signal=1UL<<bit;
    struct Task *task=AllocMem(sizeof(*task),MEMF_PUBLIC|MEMF_CLEAR);
    void *stack=AllocMem(256*1024,MEMF_PUBLIC|MEMF_CLEAR);
    CHECK(task && stack);
    task->tc_Node.ln_Type=NT_TASK;task->tc_Node.ln_Name="restricted removable worker";
    task->tc_Node.ln_Pri=retirement_parent->tc_Node.ln_Pri;
    task->tc_SPLower=stack;task->tc_SPUpper=(unsigned char *)stack+256*1024;task->tc_SPReg=task->tc_SPUpper;
    NEWLIST(&task->tc_MemEntry);
    pthread_t client;CHECK(!pthread_create(&client,NULL,retirement_client,NULL));
    CHECK(AddTask(task,retired_worker,NULL));
    while (!__atomic_load_n(&retirement_taken,__ATOMIC_ACQUIRE)) Wait(retirement_signal);
    Forbid();CHECK(task->tc_State==TS_WAIT);RemTask(task);Permit();
    FreeMem(stack,256*1024);FreeMem(task,sizeof(*task));FreeSignal(bit);
    CHECK(!pthread_join(client,NULL));
    CHECK(retired_result.status==0 && retired_result.worker_retirements==1 && retired_result.drained==1);
    for (unsigned i=0;i<8;++i) CHECK(retired_result.data[512+i]==0xa5);
}
static struct AROSRestrictedResult removed_owner_result;
static void removed_owner(void)
{
    api->run(RestrictedWorkerDeath,(size_t)(RestrictedWorkerDeathEnd-RestrictedWorkerDeath),20480,&removed_owner_result);
    CHECK(0); /* This task is removed while its request is held. */
}
static void prove_owner_retirement(void)
{
    struct AROSRestrictedLifetime before,held,after;
    api->lifetime(&before);CHECK(!before.live && !before.pending && !before.pages);
    /* More removals than available domain slots prove slot/handle reuse. */
    for (unsigned cycle=0;cycle<8;++cycle) {
        memset(&removed_owner_result,0x6b,sizeof(removed_owner_result));
        struct Task *task=AllocMem(sizeof(*task),MEMF_PUBLIC|MEMF_CLEAR);
        void *stack=AllocMem(256*1024,MEMF_PUBLIC|MEMF_CLEAR);
        CHECK(task && stack);
        task->tc_Node.ln_Type=NT_TASK;task->tc_Node.ln_Name="restricted removable owner";
        task->tc_Node.ln_Pri=FindTask(NULL)->tc_Node.ln_Pri;
        task->tc_SPLower=stack;task->tc_SPUpper=(unsigned char *)stack+256*1024;task->tc_SPReg=task->tc_SPUpper;
        NEWLIST(&task->tc_MemEntry);CHECK(AddTask(task,removed_owner,NULL));
        struct AROSRestrictedWork work;
        while (!api->take(&work)) KrnSwitch();
        CHECK(work.length==8 && api->state(&work)==AROS_RESTRICTED_RUNNING);
        RemTask(task);FreeMem(stack,256*1024);FreeMem(task,sizeof(*task));
        api->lifetime(&held);
        CHECK(held.retired==before.retired+cycle+1 && held.reaped==before.reaped+cycle);
        CHECK(held.live==1 && held.pending==1 && held.pages==1027);
        CHECK(api->state(&work)==AROS_RESTRICTED_CANCELLED);
        memset(work.bytes,0x39,8);CHECK(api->complete(&work));
        CHECK(!api->complete(&work) && !api->state(&work));
        api->lifetime(&after);
        CHECK(after.retired==before.retired+cycle+1 && after.reaped==before.reaped+cycle+1);
        CHECK(!after.live && !after.pending && !after.pages);
        for (unsigned i=0;i<sizeof(removed_owner_result);++i)
            CHECK(((unsigned char *)&removed_owner_result)[i]==0x6b);
    }
}
int main(void)
{
    KernelBase = OpenResource((CONST_STRPTR)"kernel.resource");
    CHECK(KernelBase != NULL);
    intptr_t address = KrnGetSystemAttr(AROS_RESTRICTED_KATTR_API);
    CHECK(address != -1 && address != 0);
    api = (const void *)address;
    CHECK(api->version == 7 && api->size == sizeof(*api));
    /* No worker exists: queued cancellation is deterministic. */
    static struct AROSRestrictedResult queued;
    CHECK(api->run(RestrictedQueued,(size_t)(RestrictedQueuedEnd-RestrictedQueued),12288,&queued));
    CHECK(queued.status==0 && queued.cancelled==1 && queued.drained==1 && !queued.close_waits);
    pthread_t clients[2], heart, service;
    CHECK(!pthread_create(&heart,NULL,heartbeat,NULL));
    CHECK(!pthread_create(&service,NULL,worker,NULL));
    CHECK(!pthread_create(&clients[0],NULL,client,(void *)(uintptr_t)0));
    CHECK(!pthread_create(&clients[1],NULL,client,(void *)(uintptr_t)1));
    CHECK(!pthread_join(clients[0],NULL));
    CHECK(!pthread_join(clients[1],NULL));
    __atomic_store_n(&stop,1,__ATOMIC_RELEASE);
    CHECK(!pthread_join(heart,NULL));
    CHECK(!pthread_join(service,NULL));
    for (unsigned i=0;i<4;++i) for (unsigned j=i+1;j<4;++j) CHECK(identities[i] && identities[j] && identities[i]!=identities[j]);
    CHECK(identities[0] && identities[1] && identities[2]);
    CHECK(identities[0]!=identities[1] && identities[0]!=identities[2] && identities[1]!=identities[2]);
    CHECK(results[0].status == -2 && results[0].far == UINT64_C(0x40080000));
    CHECK(((results[0].esr >> 26) & 63) == 0x24);
    CHECK(results[1].status == 0);
    for (unsigned id=0; id<2; ++id) {
        CHECK(results[id].value == (id+1)*5120);
        CHECK(results[id].yields == (id+1)*1024);
        CHECK(results[id].denied == 16);
        CHECK(results[id].cancelled==1 && results[id].drained==1);
        for (unsigned byte=0;byte<8;++byte) {
            CHECK(results[id].data[512+byte]==0xa5);
            CHECK(results[id].data[544+byte]==0xa5);
        }
        CHECK(results[id].replies == 201 && results[id].rejected == 8 && results[id].callbacks == 100);
    }
    CHECK(worker_replies == 402 && worker_ack == 3);
    CHECK(results[0].close_waits>0 && results[1].close_waits==0);
    CHECK(results[0].resumed == 1 && results[1].resumed == 1);
    CHECK(beats > 0 && after_fault > 0);
    prove_worker_retirement();
    prove_owner_retirement();
    static struct AROSRestrictedResult bad_stack;
    for (unsigned operation=0;operation<3;++operation) {
        CHECK(api->run(RestrictedBadStack,
            (size_t)(RestrictedBadStackEnd-RestrictedBadStack),operation,&bad_stack));
        CHECK(bad_stack.status==-3 && bad_stack.value==operation && bad_stack.drained==1);
        CHECK(bad_stack.yields==(operation==0));
        struct AROSRestrictedLifetime lifetime;
        api->lifetime(&lifetime);
        CHECK(!lifetime.live && !lifetime.pending && !lifetime.pages);
    }
    bug("[ISOVM] invalid-stack yield=1 collect=1 status=1 drained=3 PASS\n");
    bug("[ISOVM] PASS restricted=2 yields=3072 denied=32 faulted=1 survived=1 heartbeat=%u after_fault=%u joined=5 replies=402 rejected=16 callbacks=200 resumed=2 cancelled=3 worker_ack=3 close_pending=1 drained=4 removed_worker=1 removed_owners=8 reaped_owners=8 retained_pages=0\n",beats,after_fault);
    return 0;
}
