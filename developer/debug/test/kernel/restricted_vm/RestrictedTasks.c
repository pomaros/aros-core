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
#if defined(__AROSEXEC_SMP__)
#include <proto/task.h>
#include <resources/task.h>
APTR TaskResBase;
static APTR cpu_masks[2];
static unsigned migration_ack[2];
static unsigned removed_worker_cpu,removed_owner_cpu,reaper_stop,reaper_polls;
#endif
APTR KernelBase;
extern const unsigned char RestrictedFaultPolicy[], RestrictedFaultPolicyEnd[];
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
#if defined(__AROSEXEC_SMP__)
static void pin(unsigned cpu)
{
    CHECK(SetTaskAffinity(NULL,cpu_masks[cpu]));
    while (KrnGetCPUNumber()!=cpu) KrnSwitch();
}
static void *competing_reaper(void *unused)
{
    (void)unused;pin(1);
    while (!__atomic_load_n(&reaper_stop,__ATOMIC_ACQUIRE)) {
        struct AROSRestrictedLifetime state;
        api->lifetime(&state);++reaper_polls;
        CHECK(KrnGetCPUNumber()==1);
        KrnSwitch();
    }
    return NULL;
}
#endif
static void *heartbeat(void *unused)
{
    (void)unused;
    identities[2] = FindTask(NULL);
    while (!__atomic_load_n(&stop,__ATOMIC_ACQUIRE)) {
        ++beats;
        if (__atomic_load_n(&done[0],__ATOMIC_ACQUIRE)) ++after_fault;
#if defined(__AROSEXEC_SMP__)
        for (unsigned id=0;id<2;++id) {
            if (__atomic_load_n(&migration_ack[id],__ATOMIC_RELAXED)) continue;
            if (__atomic_load_n(&done[id],__ATOMIC_ACQUIRE)) {
                /* The client retains its Task until this acknowledgement. */
                __atomic_store_n(&migration_ack[id],1,__ATOMIC_RELEASE);
                continue;
            }
            struct Task *task=__atomic_load_n(&identities[id],__ATOMIC_ACQUIRE);
            if (task) CHECK(SetTaskAffinity(task,cpu_masks[(beats/8+id)&1]));
        }
#endif
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
    __atomic_store_n(&identities[id],FindTask(NULL),__ATOMIC_RELEASE);
    CHECK(api->run(RestrictedCode,(size_t)(RestrictedCodeEnd-RestrictedCode),
        (id+1)*4096,&results[id]));
    __atomic_store_n(&done[id],1,__ATOMIC_RELEASE);
#if defined(__AROSEXEC_SMP__)
    while (!__atomic_load_n(&migration_ack[id],__ATOMIC_ACQUIRE)) KrnSwitch();
#endif
    return NULL;
}
extern const unsigned char RestrictedShared[],RestrictedSharedEnd[];
static uint64_t shared_domains[2];
static struct AROSRestrictedResult shared_results[4];
static unsigned shared_stop,shared_replies;
#if defined(__AROSEXEC_SMP__)
static struct Task *shared_identities[4];
static unsigned shared_done[4],shared_ack[4];
static void *shared_migrator(void *unused)
{
    (void)unused;unsigned ticks=0;
    while (!__atomic_load_n(&shared_stop,__ATOMIC_ACQUIRE)) {
        for (unsigned id=0;id<4;++id) {
            if (__atomic_load_n(&shared_ack[id],__ATOMIC_RELAXED)) continue;
            if (__atomic_load_n(&shared_done[id],__ATOMIC_ACQUIRE)) {
                __atomic_store_n(&shared_ack[id],1,__ATOMIC_RELEASE);continue;
            }
            struct Task *task=__atomic_load_n(&shared_identities[id],__ATOMIC_ACQUIRE);
            if (task) CHECK(SetTaskAffinity(task,cpu_masks[(ticks/8+id)&1]));
        }
        ++ticks;KrnSwitch();
    }
    return NULL;
}
#endif
static void *shared_worker(void *unused)
{
    (void)unused;
    while (!__atomic_load_n(&shared_stop,__ATOMIC_ACQUIRE)) {
        struct AROSRestrictedWork work;
        if (api->take(&work)) {
            CHECK(work.length==8);
            for (unsigned i=0;i<8;++i) work.bytes[i]^=0x5a;
            CHECK(api->complete(&work));++shared_replies;
        }
        KrnSwitch();
    }
    return NULL;
}
static void *shared_client(void *argument)
{
    unsigned id=(unsigned)(uintptr_t)argument;
#if defined(__AROSEXEC_SMP__)
    pin(id&1);
    __atomic_store_n(&shared_identities[id],FindTask(NULL),__ATOMIC_RELEASE);
#endif
    CHECK(api->close_domain(shared_domains[id/2])==-1); /* Controller ownership. */
    CHECK(api->run_domain(shared_domains[id/2],(id/2+1)*4096+(id&1),&shared_results[id]));
#if defined(__AROSEXEC_SMP__)
    __atomic_store_n(&shared_done[id],1,__ATOMIC_RELEASE);
    while (!__atomic_load_n(&shared_ack[id],__ATOMIC_ACQUIRE)) KrnSwitch();
#endif
    return NULL;
}
static void prove_shared_domains(void)
{
    pthread_t clients[4],service;
#if defined(__AROSEXEC_SMP__)
    pthread_t migrator;
    CHECK(!pthread_create(&migrator,NULL,shared_migrator,NULL));
#endif
    for (unsigned d=0;d<2;++d) {
        shared_domains[d]=api->create_domain(RestrictedShared,
            (size_t)(RestrictedSharedEnd-RestrictedShared),(d+1)*4096);
        CHECK(shared_domains[d]);
    }
    CHECK(shared_domains[0]!=shared_domains[1]);
    CHECK(!pthread_create(&service,NULL,shared_worker,NULL));
    for (unsigned id=0;id<4;++id)
        CHECK(!pthread_create(&clients[id],NULL,shared_client,(void *)(uintptr_t)id));
    for (unsigned id=0;id<4;++id) {
        CHECK(!pthread_join(clients[id],NULL));
        CHECK(shared_results[id].status==0 && shared_results[id].drained==1 && !shared_results[id].snapshot_valid);
        CHECK(shared_results[id].value==(id/2+1)*4096 && shared_results[id].replies==1);
        CHECK(shared_results[id].yields>=((id&1) ? 1024 : 512));
#if defined(__AROSEXEC_SMP__)
        CHECK(shared_results[id].cpus==3 && shared_results[id].migrations>0);
#endif
    }
    __atomic_store_n(&shared_stop,1,__ATOMIC_RELEASE);
    CHECK(!pthread_join(service,NULL) && shared_replies==4);
#if defined(__AROSEXEC_SMP__)
    CHECK(!pthread_join(migrator,NULL));
    bug("[ISOVM] shared-migration threads=4 moves0=%llu moves1=%llu moves2=%llu moves3=%llu PASS\n",
        (unsigned long long)shared_results[0].migrations,(unsigned long long)shared_results[1].migrations,
        (unsigned long long)shared_results[2].migrations,(unsigned long long)shared_results[3].migrations);
#endif
    struct AROSRestrictedLifetime state;
    api->lifetime(&state);
    CHECK(!state.live && state.domains==2 && state.pages-state.metadata_pages-state.table_pages==4);
    for (unsigned d=0;d<2;++d) CHECK(api->close_domain(shared_domains[d])==1);
    uint64_t replacement=api->create_domain(RestrictedShared,
        (size_t)(RestrictedSharedEnd-RestrictedShared),4096);
    CHECK(replacement && replacement!=shared_domains[0]);
    CHECK(!api->run_domain(shared_domains[0],0,&shared_results[0]));
    CHECK(api->close_domain(shared_domains[0])==-1);
    CHECK(api->close_domain(replacement)==1);
    api->lifetime(&state);
    CHECK(!state.live && !state.domains && !state.pending && !state.pages);
    bug("[ISOVM] shared-domains domains=2 threads=4 joins=4 replies=4 tls=4 stale=1 retained_pages=0 PASS\n");
}
extern const unsigned char RestrictedMemory[],RestrictedMemoryEnd[];
static void *foreign_memory(void *opaque)
{
    uint64_t *args=opaque,handle=args[0];uintptr_t va=args[1];
    CHECK(!api->reserve_memory(handle,va+16384,4096));
    CHECK(!api->commit_memory(handle,va+4096,3));
    CHECK(!api->decommit_memory(handle,va));
    CHECK(!api->protect_memory(handle,va,0));
    CHECK(!api->release_memory(handle,va));return NULL;
}
static void prove_private_memory(uintptr_t va)
{
    uint64_t handles[2];struct AROSRestrictedResult result;
    struct AROSRestrictedLifetime life;
    for (unsigned i=0;i<2;++i) {
        handles[i]=api->create_domain(RestrictedMemory,(size_t)(RestrictedMemoryEnd-RestrictedMemory),0);
        CHECK(handles[i] && api->reserve_memory(handles[i],va,8192));
        CHECK(!api->reserve_memory(handles[i],va,4096));
        CHECK(!api->reserve_memory(handles[i],AROS_RESTRICTED_STACK,4096));
        /* QEMU places both native code and API data inside the admitted low
         * window. Refusal must come from transition protection, not bounds. */
        uintptr_t kernel_code=(uintptr_t)api->run_domain & ~(uintptr_t)4095;
        uintptr_t kernel_data=(uintptr_t)api & ~(uintptr_t)4095;
        CHECK(kernel_code>=0x10000 && kernel_code<UINT64_C(0x100000000));
        CHECK(kernel_data>=0x10000 && kernel_data<UINT64_C(0x100000000));
        CHECK(!api->reserve_memory(handles[i],kernel_code,4096));
        CHECK(!api->reserve_memory(handles[i],kernel_data,4096));
        CHECK(!api->reserve_memory(handles[i],0,4096));
        CHECK(!api->reserve_memory(handles[i],UINT64_C(0xfffff000),8192));
        CHECK(!api->reserve_memory(handles[i],UINT64_C(0x1fffff000),8192));
        CHECK(!api->reserve_memory(handles[i],UINTPTR_MAX-4095,8192));
        CHECK(!api->commit_memory(handles[i],va,5));
        CHECK(api->commit_memory(handles[i],va,3));
        CHECK(!api->commit_memory(handles[i],va,3));
        CHECK(api->run_domain(handles[i],va,&result) && result.status==0 && result.value==0 && result.denied==3);
    }
    pthread_t foreign;
    uint64_t args[]={handles[0],va};
    CHECK(!pthread_create(&foreign,NULL,foreign_memory,args));
    CHECK(!pthread_join(foreign,NULL));
    CHECK(api->protect_memory(handles[0],va,0));
    CHECK(api->run_domain(handles[0],va,&result) && result.status==-2 &&
        result.far==va && ((result.esr>>26)&63)==0x24);
    CHECK(api->protect_memory(handles[0],va,3));
    CHECK(api->run_domain(handles[0],va,&result) && result.status==0 && result.value==1);
    CHECK(api->decommit_memory(handles[0],va));
    CHECK(api->commit_memory(handles[0],va,0));
    CHECK(api->protect_memory(handles[0],va,3));
    CHECK(api->run_domain(handles[0],va,&result) && result.status==0 && result.value==0);
    CHECK(api->run_domain(handles[1],va,&result) && result.status==0 && result.value==1);
    api->lifetime(&life);CHECK(!life.live && life.domains==2 && life.pages-life.metadata_pages-life.table_pages==6);
    CHECK(api->release_memory(handles[0],va));
    CHECK(!api->release_memory(handles[0],va));
    CHECK(api->reserve_memory(handles[0],va,4096) && api->commit_memory(handles[0],va,3));
    for (unsigned i=0;i<2;++i) CHECK(api->close_domain(handles[i])==1);
    CHECK(!api->commit_memory(handles[0],va,3));
    api->lifetime(&life);CHECK(!life.live && !life.domains && !life.pages);
    bug("[ISOVM] private-memory domains=2 fixed=2 zero=3 isolated=2 recommit=1 guard_fault=1 foreign=5 close_regions=2 retained_pages=0 PASS\n");
}
static void *orphan_shared(void *unused)
{
    (void)unused;CHECK(api->create_shared(4096));return NULL;
}
static void prove_shared_memory(void)
{
    const uintptr_t va=UINT64_C(0x200000000);
    uint64_t object=api->create_shared(4096),domains[2];
    struct AROSRestrictedResult result;struct AROSRestrictedLifetime life;
    CHECK(object);
    for (unsigned i=0;i<2;++i) {
        domains[i]=api->create_domain(RestrictedMemory,(size_t)(RestrictedMemoryEnd-RestrictedMemory),0);
        CHECK(domains[i] && api->reserve_memory(domains[i],va,4096));
        CHECK(api->attach_shared(domains[i],va,object));
        CHECK(!api->attach_shared(domains[i],va,object));
        CHECK(!api->commit_memory(domains[i],va,3));
        CHECK(!api->commit_shared(domains[i],va,5));
        CHECK(api->commit_shared(domains[i],va,3));
        CHECK(api->run_domain(domains[i],0,&result) && result.status==0 && result.value==i);
    }
    api->lifetime(&life);CHECK(life.shared_objects==1 && life.pages-life.metadata_pages-life.table_pages==5);
    CHECK(api->decommit_memory(domains[0],va));
    CHECK(api->commit_shared(domains[0],va,3));
    CHECK(api->run_domain(domains[0],0,&result) && result.status==0 && result.value==2);
    CHECK(api->protect_memory(domains[0],va,1));
    CHECK(api->run_domain(domains[0],0,&result) && result.status==-2 && result.far==va);
    CHECK(api->run_domain(domains[1],0,&result) && result.status==0 && result.value==3);
    CHECK(api->release_memory(domains[0],va));
    CHECK(api->close_domain(domains[0])==1 && !api->close_shared(object));
    CHECK(api->run_domain(domains[1],0,&result) && result.status==0 && result.value==4);
    CHECK(api->close_domain(domains[1])==1 && api->close_shared(object));
    CHECK(!api->close_shared(object));
    uint64_t replacement=api->create_shared(4096);CHECK(replacement && replacement!=object);
    CHECK(!api->close_shared(object) && api->close_shared(replacement));
    pthread_t orphan;CHECK(!pthread_create(&orphan,NULL,orphan_shared,NULL));CHECK(!pthread_join(orphan,NULL));
    /* pthread completion follows CRT teardown but can precede native context
     * deletion on another CPU. Ownership ends at that kernel retirement hook,
     * not at the earlier join indication. Keep the drain explicitly bounded. */
    for (unsigned attempts=0;;++attempts) {
        api->lifetime(&life);
        CHECK(!life.domains && !life.live);
        if (!life.shared_objects) break;
        CHECK(life.shared_objects==1 && attempts<100000);KrnSwitch();
    }
    CHECK(!life.pages);
    bug("[ISOVM] shared-memory domains=2 views=2 shared_values=5 private_permissions=1 decommit_preserved=1 orphan=1 retained_pages=0 PASS\n");
}
extern const unsigned char RestrictedRegionFault[],RestrictedRegionFaultEnd[];
static void prove_region_faults(void)
{
    const uintptr_t va=UINT64_C(0x200000000);
    uint64_t domain=api->create_domain(RestrictedRegionFault,(size_t)(RestrictedRegionFaultEnd-RestrictedRegionFault),0);
    struct AROSRestrictedResult result;struct AROSRestrictedLifetime life;
    CHECK(domain && api->reserve_memory(domain,va,4096) && api->commit_memory(domain,va,0));
    CHECK(api->run_domain(domain,0,&result));
    CHECK(result.status==0 && result.value==0x77 && result.resumed==2 && result.denied==2);
    CHECK(api->close_domain(domain)==1);api->lifetime(&life);
    CHECK(!life.live && !life.domains && !life.shared_objects && !life.pages);
    bug("[ISOVM] region-fault guard_read=1 readonly_write=1 alternate_stack=2 unrepaired_refused=2 resumed=2 retained_pages=0 PASS\n");
}
extern const unsigned char RestrictedReplacedFault[],RestrictedReplacedFaultEnd[];
static uint64_t replaced_domain;
static struct AROSRestrictedResult replaced_result;
static void *replaced_client(void *unused)
{
    (void)unused;CHECK(api->run_domain(replaced_domain,0,&replaced_result));return NULL;
}
static void prove_replaced_fault(void)
{
    const uintptr_t va=UINT64_C(0x200000000);
    for (unsigned mode=0;mode<2;++mode) {
    replaced_domain=api->create_domain(RestrictedReplacedFault,
        (size_t)(RestrictedReplacedFaultEnd-RestrictedReplacedFault),0);
    CHECK(replaced_domain && api->reserve_memory(replaced_domain,va,4096) &&
        api->commit_memory(replaced_domain,va,0));
    pthread_t client;CHECK(!pthread_create(&client,NULL,replaced_client,NULL));
    struct AROSRestrictedWork work;
    while (!api->take(&work)) KrnSwitch();
    uint64_t input;memcpy(&input,work.bytes,sizeof(input));CHECK(work.length==8 && input==0x71);
    /* Fault handler is waiting on DATA-page service; the faulted region itself
     * can be replaced. Its old continuation must not authorize the new page. */
    if (mode) {
        CHECK(api->release_memory(replaced_domain,va));
        CHECK(api->reserve_memory(replaced_domain,va,4096));
    } else CHECK(api->decommit_memory(replaced_domain,va));
    CHECK(api->commit_memory(replaced_domain,va,3));
    CHECK(api->complete(&work));CHECK(!pthread_join(client,NULL));
    CHECK(replaced_result.status==0 && replaced_result.denied==1 &&
        replaced_result.resumed==0 && replaced_result.replies==1);
    CHECK(api->close_domain(replaced_domain)==1);
    struct AROSRestrictedLifetime life;api->lifetime(&life);
    CHECK(!life.live && !life.domains && !life.shared_objects && !life.pages);
    }
    bug("[ISOVM] replaced-fault held_handler=2 replaced_page=2 re_reserved=1 stale_resume_refused=2 retained_pages=0 PASS\n");
}
static void prove_domain_growth(void)
{
    enum { COUNT=12 };uint64_t domains[COUNT];
    for (unsigned i=0;i<COUNT;++i) {
        domains[i]=api->create_domain(RestrictedQueued,(size_t)(RestrictedQueuedEnd-RestrictedQueued),12288+i);
        CHECK(domains[i]);
    }
    struct AROSRestrictedLifetime life;api->lifetime(&life);
    CHECK(life.domains==COUNT && life.pages-life.metadata_pages-life.table_pages==COUNT*2 && !life.live);
    struct AROSRestrictedResult result;
    for (unsigned i=0;i<COUNT;++i) {
        CHECK(api->run_domain(domains[i],12288+i,&result));
        CHECK(result.status==0 && result.cancelled==1 && result.drained==1);
    }
    for (unsigned parity=0;parity<2;++parity) for (unsigned i=parity;i<COUNT;i+=2)
        CHECK(api->close_domain(domains[i])==1);
    api->lifetime(&life);CHECK(!life.domains && !life.pages && !life.live && !life.shared_objects);
    bug("[ISOVM] domain-growth domains=12 runs=12 joined=12 retained_pages=0 PASS\n");
}
extern const unsigned char RestrictedThreadGrowth[],RestrictedThreadGrowthEnd[];
static uint64_t growth_domain;
static struct AROSRestrictedResult growth_results[32];
static void *growth_client(void *argument)
{
    unsigned id=(unsigned)(uintptr_t)argument;
#if defined(__AROSEXEC_SMP__)
    pin(id&1);
#endif
    CHECK(api->run_domain(growth_domain,id,&growth_results[id]));return NULL;
}
static void prove_thread_growth(void)
{
    enum { COUNT=32 };pthread_t clients[COUNT];struct AROSRestrictedWork held[COUNT];
    growth_domain=api->create_domain(RestrictedThreadGrowth,
        (size_t)(RestrictedThreadGrowthEnd-RestrictedThreadGrowth),0);
    CHECK(growth_domain);
    for (unsigned i=0;i<COUNT;++i) CHECK(!pthread_create(&clients[i],NULL,growth_client,(void *)(uintptr_t)i));
    uint64_t seen=0;
    for (unsigned i=0;i<COUNT;++i) {
        while (!api->take(&held[i])) KrnSwitch();
        uint64_t id;memcpy(&id,held[i].bytes,sizeof(id));
        CHECK(held[i].length==8 && id<COUNT && !(seen&(UINT64_C(1)<<id)));
        seen|=UINT64_C(1)<<id;
    }
    struct AROSRestrictedLifetime life;api->lifetime(&life);
    CHECK(seen==UINT64_C(0xffffffff) && life.live==COUNT && life.domains==1);
    CHECK(life.pages-life.metadata_pages-life.table_pages==2+COUNT*3);
    for (unsigned i=0;i<COUNT;++i) CHECK(api->complete(&held[i]));
    for (unsigned i=0;i<COUNT;++i) {
        CHECK(!pthread_join(clients[i],NULL));
        CHECK(growth_results[i].status==0 && growth_results[i].replies==1 && growth_results[i].drained==1);
    }
    CHECK(api->close_domain(growth_domain)==1);api->lifetime(&life);
    CHECK(!life.live && !life.domains && !life.pages && !life.metadata_pages);
    bug("[ISOVM] thread-growth concurrent=32 held_workers=32 joined=32 tls=32 retained_pages=0 PASS\n");
}
static void prove_table_growth(void)
{
    const uintptr_t base=UINT64_C(0x200000000),stride=UINT64_C(0x200000);
    uint64_t domain=api->create_domain(RestrictedQueued,(size_t)(RestrictedQueuedEnd-RestrictedQueued),0);
    CHECK(domain);struct AROSRestrictedLifetime before,after;api->lifetime(&before);
    CHECK(before.domains==1 && before.table_pages);
    CHECK(api->reserve_memory(domain,base,32*stride));
    for (unsigned i=0;i<32;++i) CHECK(api->commit_memory(domain,base+i*stride,3));
    api->lifetime(&after);
    CHECK(after.table_pages>=before.table_pages+16 && after.pages-after.metadata_pages-after.table_pages==34);
    CHECK(api->release_memory(domain,base));api->lifetime(&after);
    CHECK(after.pages-after.metadata_pages-after.table_pages==2);
    CHECK(api->close_domain(domain)==1);api->lifetime(&after);
    CHECK(!after.pages && !after.table_pages && !after.domains && !after.live);
    bug("[ISOVM] table-growth sparse_commits=32 replenished=1 retained_pages=0 PASS\n");
}
extern const unsigned char RestrictedSpin[],RestrictedSpinEnd[];
static uint64_t closing_domain;
static unsigned closing_done,creator_release;
static struct AROSRestrictedResult closing_result;
static void *closing_client(void *unused)
{
    (void)unused;
#if defined(__AROSEXEC_SMP__)
    pin(1);
#endif
    CHECK(api->run_domain(closing_domain,0,&closing_result));
    __atomic_store_n(&closing_done,1,__ATOMIC_RELEASE);return NULL;
}
static void *orphan_creator(void *unused)
{
    (void)unused;
    uint64_t handle=api->create_domain(RestrictedSpin,
        (size_t)(RestrictedSpinEnd-RestrictedSpin),32768);
    CHECK(handle);__atomic_store_n(&closing_domain,handle,__ATOMIC_RELEASE);
    while (!__atomic_load_n(&creator_release,__ATOMIC_ACQUIRE)) KrnSwitch();
    return NULL;
}
static void prove_domain_close(void)
{
    for (unsigned orphan=0;orphan<2;++orphan) {
        pthread_t client,creator;closing_done=creator_release=0;closing_domain=0;
        memset(&closing_result,0,sizeof(closing_result));
        if (orphan) {
            CHECK(!pthread_create(&creator,NULL,orphan_creator,NULL));
            while (!__atomic_load_n(&closing_domain,__ATOMIC_ACQUIRE)) KrnSwitch();
        } else closing_domain=api->create_domain(RestrictedSpin,
            (size_t)(RestrictedSpinEnd-RestrictedSpin),32768);
        CHECK(closing_domain);
        CHECK(!pthread_create(&client,NULL,closing_client,NULL));
        struct AROSRestrictedWork work;
        while (!api->take(&work)) KrnSwitch();
        CHECK(work.length==8 && api->state(&work)==AROS_RESTRICTED_RUNNING);
        for (unsigned turn=0;turn<64;++turn) KrnSwitch();
        if (orphan) {
            __atomic_store_n(&creator_release,1,__ATOMIC_RELEASE);
            CHECK(!pthread_join(creator,NULL));
        } else CHECK(api->close_domain(closing_domain)==0);
        while (api->state(&work)==AROS_RESTRICTED_RUNNING) KrnSwitch();
        CHECK(api->state(&work)==AROS_RESTRICTED_CANCELLED);
        CHECK(!__atomic_load_n(&closing_done,__ATOMIC_ACQUIRE));
        struct AROSRestrictedLifetime state;
        api->lifetime(&state);
        CHECK(state.live==1 && state.domains==1 && state.pages-state.metadata_pages-state.table_pages==5);
        memset(work.bytes,0x55,work.length);
        CHECK(api->complete(&work));
        CHECK(!pthread_join(client,NULL));
        CHECK(closing_result.status==-5 && closing_result.drained==1 && closing_result.snapshot_valid);
        uint64_t spins;memcpy(&spins,closing_result.data+640,sizeof(spins));CHECK(spins>0);
        for (unsigned i=0;i<8;++i) CHECK(closing_result.data[544+i]==0xa5);
        if (!orphan) CHECK(api->close_domain(closing_domain)==1);
        api->lifetime(&state);
        CHECK(!state.live && !state.domains && !state.pending && !state.pages);
        CHECK(api->close_domain(closing_domain)==-1);
    }
    bug("[ISOVM] domain-close cpu_bound=2 held_workers=2 cancelled=2 no_late_write=2 orphan=1 retained_pages=0 PASS\n");
}
static void retired_worker(void)
{
#if defined(__AROSEXEC_SMP__)
    pin(1);__atomic_store_n(&removed_worker_cpu,1,__ATOMIC_RELEASE);
#endif
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
#if defined(__AROSEXEC_SMP__)
    pin(1);__atomic_store_n(&removed_owner_cpu,1,__ATOMIC_RELEASE);
#endif
    api->run(RestrictedWorkerDeath,(size_t)(RestrictedWorkerDeathEnd-RestrictedWorkerDeath),20480,&removed_owner_result);
    CHECK(0); /* This task is removed while its request is held. */
}
static void prove_owner_retirement(void)
{
#if defined(__AROSEXEC_SMP__)
    pthread_t reaper;
    CHECK(KrnGetCPUNumber()==0 && __atomic_load_n(&removed_worker_cpu,__ATOMIC_ACQUIRE)==1);
    CHECK(!pthread_create(&reaper,NULL,competing_reaper,NULL));
#endif
    struct AROSRestrictedLifetime before,held,after;
    api->lifetime(&before);CHECK(!before.live && !before.pending && !before.pages);
    /* More removals than available domain slots prove slot/handle reuse. */
    for (unsigned cycle=0;cycle<8;++cycle) {
#if defined(__AROSEXEC_SMP__)
        __atomic_store_n(&removed_owner_cpu,0,__ATOMIC_RELEASE);
#endif
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
#if defined(__AROSEXEC_SMP__)
        CHECK(KrnGetCPUNumber()==0 && __atomic_load_n(&removed_owner_cpu,__ATOMIC_ACQUIRE)==1);
#endif
        RemTask(task);FreeMem(stack,256*1024);FreeMem(task,sizeof(*task));
        api->lifetime(&held);
        CHECK(held.retired==before.retired+cycle+1 && held.reaped==before.reaped+cycle);
        CHECK(held.live==1 && held.pending==1 && held.pages-held.metadata_pages-held.table_pages==5);
        CHECK(api->state(&work)==AROS_RESTRICTED_CANCELLED);
        memset(work.bytes,0x39,8);CHECK(api->complete(&work));
        CHECK(!api->complete(&work) && !api->state(&work));
        unsigned attempts=0;
        do {
            api->lifetime(&after);
            if (!after.live) break;
            CHECK(++attempts<100000);KrnSwitch();
        } while (1);
        CHECK(after.retired==before.retired+cycle+1 && after.reaped==before.reaped+cycle+1);
        CHECK(!after.live && !after.pending && !after.pages);
        for (unsigned i=0;i<sizeof(removed_owner_result);++i)
            CHECK(((unsigned char *)&removed_owner_result)[i]==0x6b);
    }
#if defined(__AROSEXEC_SMP__)
    __atomic_store_n(&reaper_stop,1,__ATOMIC_RELEASE);
    CHECK(!pthread_join(reaper,NULL) && reaper_polls>0);
    bug("[ISOVM] remote-lifetime caller=0 worker=1 owners=8 owner_cpu=1 reaper_cpu=1 polls=%u PASS\n",reaper_polls);
#endif
}
#if defined(__AROSEXEC_SMP__)
static unsigned task_list_stop,task_list_active,task_list_sweeps[2],task_list_concurrent[2];
static void *task_list_reader(void *argument)
{
    unsigned cpu=(unsigned)(uintptr_t)argument;pin(cpu);
    while (!__atomic_load_n(&task_list_stop,__ATOMIC_ACQUIRE)) {
        struct TaskList *list=LockTaskList(LTF_ALL);CHECK(list);
        unsigned count=0;
        while (NextTaskEntry(list,LTF_ALL) && ++count<10000) { }
        UnLockTaskList(list,LTF_ALL);
        CHECK(count && count<10000 && KrnGetCPUNumber()==cpu);
        __atomic_fetch_add(&task_list_sweeps[cpu],1,__ATOMIC_RELEASE);
        if (__atomic_load_n(&task_list_active,__ATOMIC_ACQUIRE))
            __atomic_fetch_add(&task_list_concurrent[cpu],1,__ATOMIC_RELEASE);
        KrnSwitch();
    }
    return NULL;
}
static void *task_list_worker(void *argument)
{
    uintptr_t id=(uintptr_t)argument;pin(id&1);
    for (unsigned turn=0;turn<16;++turn) KrnSwitch();
    return (void *)(id+1);
}
static void prove_task_list_churn(void)
{
    pthread_t readers[2],workers[4];
    for (unsigned cpu=0;cpu<2;++cpu)
        CHECK(!pthread_create(&readers[cpu],NULL,task_list_reader,(void *)(uintptr_t)cpu));
    while (!__atomic_load_n(&task_list_sweeps[0],__ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&task_list_sweeps[1],__ATOMIC_ACQUIRE)) KrnSwitch();
    __atomic_store_n(&task_list_active,1,__ATOMIC_RELEASE);
    for (unsigned batch=0;batch<32;++batch) {
        for (unsigned i=0;i<4;++i)
            CHECK(!pthread_create(&workers[i],NULL,task_list_worker,(void *)(uintptr_t)(4*batch+i)));
        for (unsigned i=0;i<4;++i) {
            void *value=NULL;CHECK(!pthread_join(workers[i],&value));
            CHECK((uintptr_t)value==4*batch+i+1);
        }
    }
    __atomic_store_n(&task_list_active,0,__ATOMIC_RELEASE);
    __atomic_store_n(&task_list_stop,1,__ATOMIC_RELEASE);
    for (unsigned cpu=0;cpu<2;++cpu) CHECK(!pthread_join(readers[cpu],NULL));
    CHECK(task_list_concurrent[0] && task_list_concurrent[1]);
    bug("[ISOVM] task-list readers=2 cpus=3 created=128 joined=128 sweeps0=%u sweeps1=%u PASS\n",
        task_list_concurrent[0],task_list_concurrent[1]);
}
#endif
extern const unsigned char RestrictedLargeStacks[],RestrictedLargeStacksEnd[];
static void prove_large_stacks(void)
{
    for (unsigned mode=0;mode<5;++mode) {
        uint64_t domain=api->create_domain(RestrictedLargeStacks,
            RestrictedLargeStacksEnd-RestrictedLargeStacks,0);CHECK(domain);
        struct AROSRestrictedResult result={0};
        CHECK(!api->run_domain_stacks(domain,mode,0,4096,&result));
        CHECK(!api->run_domain_stacks(domain,mode,4097,4096,&result));
        CHECK(!api->run_domain_stacks(domain,mode,SIZE_MAX,4096,&result));
        CHECK(api->run_domain_stacks(domain,mode,17*4096,3*4096,&result));
        CHECK(result.drained && result.status==(mode ? -2 : 0));
        CHECK(result.resumed==(mode ? 0 : 1));
        CHECK(api->close_domain(domain)==1);
        struct AROSRestrictedLifetime life;api->lifetime(&life);
        CHECK(!life.pages && !life.live && !life.domains);
    }
    bug("[ISOVM] large-stacks stack_pages=17 fault_pages=3 repaired=1 guards=4 drained=5 PASS\n");
}
extern const unsigned char RestrictedLargeCode[],RestrictedLargeCodeEnd[];
static void prove_large_code(void)
{
    size_t bytes=RestrictedLargeCodeEnd-RestrictedLargeCode;
    CHECK(bytes>8192 && bytes<=12288);
    CHECK(!api->create_domain(RestrictedLargeCode,0,0));
    CHECK(!api->create_domain(RestrictedLargeCode,SIZE_MAX,0));
    uint64_t domains[2];
    for (unsigned i=0;i<2;++i) {
        domains[i]=api->create_domain(RestrictedLargeCode,bytes,0);CHECK(domains[i]);
    }
    struct AROSRestrictedLifetime life;api->lifetime(&life);
    CHECK(life.domains==2 && life.pages-life.metadata_pages-life.table_pages==8);
    for (unsigned i=0;i<2;++i) {
        struct AROSRestrictedResult result={0};
        CHECK(api->run_domain(domains[i],77+i,&result));
        CHECK(!result.status && result.resumed==1 && result.drained);
    }
    struct AROSRestrictedResult refused={0};
    CHECK(api->run_domain(domains[0],1,&refused));
    CHECK(refused.status==-2 && refused.drained && !refused.resumed);
    for (unsigned i=0;i<2;++i) CHECK(api->close_domain(domains[i])==1);
    api->lifetime(&life);CHECK(!life.pages && !life.domains && !life.live);
    bug("[ISOVM] large-code pages=3 domains=2 repaired=2 write_refused=1 retained_pages=0 PASS\n");
}
extern const unsigned char RestrictedDynamicCode[],RestrictedDynamicCodeEnd[];
struct DynamicCodeClient {
    uint64_t domain,seed;
    struct Task *task;
    struct AROSRestrictedResult result;
};
static void *dynamic_code_client(void *argument)
{
    struct DynamicCodeClient *client=argument;
#if defined(__AROSEXEC_SMP__)
    pin(0);
#endif
    __atomic_store_n(&client->task,FindTask(NULL),__ATOMIC_RELEASE);
    CHECK(api->run_domain(client->domain,client->seed,&client->result));
    return NULL;
}
static void prove_dynamic_code(void)
{
    const uintptr_t address=UINT64_C(0x200000000);
    struct DynamicCodeClient clients[2]={{.seed=42},{.seed=77}};
    pthread_t threads[2];unsigned phases[2]={0};
    for (unsigned i=0;i<2;++i) {
        clients[i].domain=api->create_domain(RestrictedDynamicCode,RestrictedDynamicCodeEnd-RestrictedDynamicCode,0);
        CHECK(clients[i].domain);
        CHECK(api->reserve_memory(clients[i].domain,address,4096));
        CHECK(api->commit_memory(clients[i].domain,address,3));
        CHECK(!pthread_create(&threads[i],NULL,dynamic_code_client,&clients[i]));
    }
    for (unsigned completed=0;completed<8;) {
        struct AROSRestrictedWork work;
        if (!api->take(&work)) { KrnSwitch();continue; }
        uint64_t words[2];CHECK(work.length==sizeof(words));memcpy(words,work.bytes,sizeof(words));
        unsigned id=words[1]==clients[0].seed ? 0 : 1;
        CHECK(words[1]==clients[id].seed && words[0]==++phases[id] && phases[id]<=4);
        CHECK(!api->protect_memory(clients[id].domain,address,7));
        if (phases[id]!=4) CHECK(api->protect_memory(clients[id].domain,address,phases[id]==2 ? 3 : 5));
#if defined(__AROSEXEC_SMP__)
        if (phases[id]!=2) {
            struct Task *task=__atomic_load_n(&clients[id].task,__ATOMIC_ACQUIRE);CHECK(task);
            CHECK(SetTaskAffinity(task,cpu_masks[phases[id]==3 ? 0 : 1]));
        }
#endif
        CHECK(api->complete(&work));++completed;
    }
    for (unsigned i=0;i<2;++i) {
        CHECK(!pthread_join(threads[i],NULL));
        CHECK(!clients[i].result.status && clients[i].result.drained && clients[i].result.replies==4);
#if defined(__AROSEXEC_SMP__)
        CHECK(clients[i].result.cpus==3 && clients[i].result.migrations>=3);
#endif
        struct AROSRestrictedResult result={0};
        CHECK(api->run_domain(clients[i].domain,1,&result) && !result.status && result.rejected==1);
        CHECK(api->run_domain(clients[i].domain,0,&result) && result.status==-2 && result.far==address);
        CHECK(api->close_domain(clients[i].domain)==1);
    }
    struct AROSRestrictedLifetime life;api->lifetime(&life);
    CHECK(!life.pages && !life.domains && !life.live);
#if defined(__AROSEXEC_SMP__)
    bug("[ISOVM] dynamic-code domains=2 publications=4 rewrites=2 cpus=3 write_refused=2 scalar_refused=2 retained_pages=0 PASS\n");
#endif
}
extern const unsigned char RestrictedDynamicContinuation[],RestrictedDynamicContinuationEnd[];
static void prove_dynamic_continuations(void)
{
    const uintptr_t address=UINT64_C(0x200000000);
    const unsigned kinds[]={0,1,2,3,4,5,4,2,1,5};
    const unsigned requests[]={1,2,2,2,2,3};
    const int statuses[]={0,0,-2,0,-4,-4};
    for (unsigned mode=0;mode<10;++mode) {
        unsigned kind=kinds[mode];
        struct DynamicCodeClient client={.seed=mode};pthread_t thread;
        client.domain=api->create_domain(RestrictedDynamicContinuation,
            RestrictedDynamicContinuationEnd-RestrictedDynamicContinuation,0);
        CHECK(client.domain);
        for (unsigned page=0;page<3;++page) {
            CHECK(api->reserve_memory(client.domain,address+page*4096,4096));
            CHECK(api->commit_memory(client.domain,address+page*4096,3));
        }
        CHECK(!pthread_create(&thread,NULL,dynamic_code_client,&client));
        for (unsigned step=0;step<requests[kind];) {
            struct AROSRestrictedWork work;if (!api->take(&work)) { KrnSwitch();continue; }
            uint64_t words[2];CHECK(work.length==sizeof(words));memcpy(words,work.bytes,sizeof(words));
            unsigned phase=step==0 ? 0 : kind==1 ? 2 : kind==2 ? 1 : step==1 ? 3 : 4;
            CHECK(words[0]==phase && words[1]==mode);
            if (!phase) {
                for (unsigned page=0;page<3;++page) CHECK(api->protect_memory(client.domain,address+page*4096,5));
#if defined(__AROSEXEC_SMP__)
                struct Task *task=__atomic_load_n(&client.task,__ATOMIC_ACQUIRE);CHECK(task);
                CHECK(SetTaskAffinity(task,cpu_masks[1]));
#endif
            } else if (kind==1 || kind==2 || kind==4 || (kind==5 && phase==4)) {
                unsigned page=kind==2 ? 1 : kind==4 ? 2 : 0;
                uintptr_t target=address+page*4096;
                if (mode==7 || mode==9) {
                    CHECK(api->release_memory(client.domain,target));
                    CHECK(api->reserve_memory(client.domain,target,4096));
                    CHECK(api->commit_memory(client.domain,target,3));
                } else if (mode==6 || mode==8) {
                    CHECK(api->decommit_memory(client.domain,target));
                    CHECK(api->commit_memory(client.domain,target,3));
                } else CHECK(api->protect_memory(client.domain,target,3));
                CHECK(api->protect_memory(client.domain,target,5));
            }
            CHECK(api->complete(&work));++step;
        }
        CHECK(!pthread_join(thread,NULL));
        CHECK(client.result.status==statuses[kind] && client.result.drained);
        CHECK(client.result.resumed==(kind==0) && client.result.callbacks==(kind==3));
        CHECK(client.result.denied==(kind==1));
#if defined(__AROSEXEC_SMP__)
        CHECK(client.result.cpus==3 && client.result.migrations);
#endif
        CHECK(api->close_domain(client.domain)==1);
    }
    struct AROSRestrictedLifetime life;api->lifetime(&life);
    CHECK(!life.pages && !life.live && !life.domains);
    bug("[ISOVM] dynamic-continuations cases=10 repaired=1 callbacks=1 stale_fault=2 stale_handler=2 stale_entry=2 stale_return=2 retained_pages=0 PASS\n");
}
extern const unsigned char RestrictedSharedCode[],RestrictedSharedCodeEnd[];
static void prove_shared_code(void)
{
    const uintptr_t address=UINT64_C(0x200000000);
    uint64_t shared=api->create_shared(4096);CHECK(shared);
    struct DynamicCodeClient clients[2]={{.seed=0},{.seed=1}};pthread_t threads[2];
    for (unsigned i=0;i<2;++i) {
        clients[i].domain=api->create_domain(RestrictedSharedCode,RestrictedSharedCodeEnd-RestrictedSharedCode,0);
        CHECK(clients[i].domain && api->reserve_memory(clients[i].domain,address,4096));
        CHECK(api->attach_shared(clients[i].domain,address,shared));
        CHECK(api->commit_shared(clients[i].domain,address,i ? 1 : 3));
    }
    CHECK(api->reserve_memory(clients[1].domain,address+4096,4096));
    CHECK(api->attach_shared(clients[1].domain,address+4096,shared));
    for (unsigned i=0;i<2;++i) CHECK(!pthread_create(&threads[i],NULL,dynamic_code_client,&clients[i]));
    for (unsigned phase=1;phase<=4;++phase) {
        struct AROSRestrictedWork work[2];unsigned held=0;
        while (held!=3) {
            struct AROSRestrictedWork next;if (!api->take(&next)) { KrnSwitch();continue; }
            uint64_t words[2];CHECK(next.length==sizeof(words));memcpy(words,next.bytes,sizeof(words));
            CHECK(words[0]==phase && words[1]<2 && !(held&(1U<<words[1])));
            work[words[1]]=next;held|=1U<<words[1];
        }
        if (phase==2) {
            CHECK(!api->protect_memory(clients[0].domain,address,3));
            CHECK(api->protect_memory(clients[1].domain,address,1));
            CHECK(api->protect_memory(clients[0].domain,address,3));
        } else if (phase!=4) {
            CHECK(!api->protect_memory(clients[1].domain,address,5));
            CHECK(api->protect_memory(clients[0].domain,address,1));
            for (unsigned i=0;i<2;++i) {
                CHECK(api->protect_memory(clients[i].domain,address,5));
#if defined(__AROSEXEC_SMP__)
                struct Task *task=__atomic_load_n(&clients[i].task,__ATOMIC_ACQUIRE);CHECK(task);
                CHECK(SetTaskAffinity(task,cpu_masks[phase==1 ? 1 : 0]));
#endif
            }
            if (phase==1) {
                CHECK(!api->commit_shared(clients[1].domain,address+4096,3));
                CHECK(api->commit_shared(clients[1].domain,address+4096,1));
            }
        }
#if defined(__AROSEXEC_SMP__)
        if (phase==4) for (unsigned i=0;i<2;++i)
            CHECK(SetTaskAffinity(__atomic_load_n(&clients[i].task,__ATOMIC_ACQUIRE),cpu_masks[1]));
#endif
        for (unsigned i=0;i<2;++i) CHECK(api->complete(&work[i]));
    }
    for (unsigned i=0;i<2;++i) {
        CHECK(!pthread_join(threads[i],NULL));
        CHECK(!clients[i].result.status && clients[i].result.drained && clients[i].result.replies==4);
#if defined(__AROSEXEC_SMP__)
        CHECK(clients[i].result.cpus==3 && clients[i].result.migrations>=3);
#endif
        struct AROSRestrictedResult result={0};
        CHECK(api->run_domain(clients[i].domain,3,&result) && !result.status && result.rejected==1);
        CHECK(api->run_domain(clients[i].domain,2,&result) && result.status==-2 && result.far==address);
        CHECK(api->close_domain(clients[i].domain)==1);
    }
    CHECK(api->close_shared(shared));
    struct AROSRestrictedLifetime life;api->lifetime(&life);
    CHECK(!life.pages && !life.domains && !life.live && !life.shared_objects);
#if defined(__AROSEXEC_SMP__)
    bug("[ISOVM] shared-code domains=2 views=3 publications=4 rewrites=1 cpus=3 conflicts=4 write_refused=2 scalar_refused=2 retained_pages=0 PASS\n");
#endif
}
extern const unsigned char RestrictedReservedFault[],RestrictedReservedFaultEnd[];
static void prove_reserved_faults(void)
{
    const uintptr_t address=UINT64_C(0x200000000);
    for (unsigned mode=0;mode<4;++mode) {
        struct DynamicCodeClient client={.seed=mode};pthread_t thread;
        client.domain=api->create_domain(RestrictedReservedFault,RestrictedReservedFaultEnd-RestrictedReservedFault,0);
        CHECK(client.domain && api->reserve_memory(client.domain,address,4096));
        CHECK(!pthread_create(&thread,NULL,dynamic_code_client,&client));
        struct AROSRestrictedWork work;while (!api->take(&work)) KrnSwitch();
        uint64_t input;CHECK(work.length==sizeof(input));memcpy(&input,work.bytes,sizeof(input));CHECK(input==mode);
        if (mode==2) {
            CHECK(api->release_memory(client.domain,address));
            CHECK(api->reserve_memory(client.domain,address,4096));
        }
        CHECK(api->commit_memory(client.domain,address,3));
        if (mode==1) {
            CHECK(api->decommit_memory(client.domain,address));
            CHECK(api->commit_memory(client.domain,address,3));
        }
#if defined(__AROSEXEC_SMP__)
        struct Task *task=__atomic_load_n(&client.task,__ATOMIC_ACQUIRE);CHECK(task);
        CHECK(SetTaskAffinity(task,cpu_masks[1]));
#endif
        CHECK(api->complete(&work));CHECK(!pthread_join(thread,NULL));
        unsigned repaired=mode==0 || mode==3;
        CHECK(!client.result.status && client.result.drained && client.result.replies==1);
        CHECK(client.result.resumed==repaired && client.result.denied==(repaired ? 1 : 2));
#if defined(__AROSEXEC_SMP__)
        CHECK(client.result.cpus==3 && client.result.migrations);
#endif
        CHECK(api->close_domain(client.domain)==1);
    }
    struct AROSRestrictedLifetime life;api->lifetime(&life);
    CHECK(!life.pages && !life.live && !life.domains);
    bug("[ISOVM] reserved-fault cases=4 read=1 write=1 premature=4 extra_commit=1 re_reserved=1 stale_refused=2 retained_pages=0 PASS\n");
}
extern const unsigned char RestrictedWait[],RestrictedWaitEnd[];
struct WaitForeign { uint64_t domain,channel; };
static void *wait_foreign_controller(void *argument)
{
    struct WaitForeign *p=argument;
    CHECK(!api->wake_thread(p->domain,p->channel));
    CHECK(!api->thread_state(p->domain,p->channel));
    return NULL;
}
static void wait_requests(struct AROSRestrictedWork work[2],unsigned phase,unsigned role)
{
    unsigned seen=0;
    for (unsigned polls=0;seen!=3 && polls<1000000;++polls) {
        struct AROSRestrictedWork next;
        if (!api->take(&next)) { KrnSwitch();continue; }
        uint64_t words[4];CHECK(next.length==sizeof(words));memcpy(words,next.bytes,sizeof(words));
        CHECK(words[0]==phase && words[1]<4 && (words[1]&1)==role);
        unsigned id=words[1]/2;CHECK(!(seen&(1U<<id)) && words[2]==next.channel);
        work[id]=next;seen|=1U<<id;
    }
    CHECK(seen==3);
}
static void wait_parked(uint64_t domain,uint64_t channel)
{
    unsigned state=0;
    for (unsigned polls=0;polls<1000000;++polls) {
        state=api->thread_state(domain,channel);
        CHECK(state&AROS_RESTRICTED_THREAD_ACTIVE);
        if (state&AROS_RESTRICTED_THREAD_PARKED) break;
        KrnSwitch();
    }
    CHECK(state==(AROS_RESTRICTED_THREAD_ACTIVE|AROS_RESTRICTED_THREAD_WAITING|AROS_RESTRICTED_THREAD_PARKED));
}
static void suspend_parked(uint64_t domain,uint64_t channel)
{
    unsigned state=0;
    for (unsigned polls=0;polls<1000000;++polls) {
        state=api->thread_state(domain,channel);CHECK(state&AROS_RESTRICTED_THREAD_ACTIVE);
        if (state&AROS_RESTRICTED_THREAD_PARKED) break;
        KrnSwitch();
    }
    CHECK(state==(AROS_RESTRICTED_THREAD_ACTIVE|AROS_RESTRICTED_THREAD_SUSPENDED|AROS_RESTRICTED_THREAD_PARKED));
}
static void prove_waits(void)
{
    struct DynamicCodeClient clients[4]={{.seed=0},{.seed=1},{.seed=2},{.seed=3}};
    uint64_t domains[2],channels[2];pthread_t threads[4];struct AROSRestrictedWork work[2];
    for (unsigned i=0;i<2;++i) {
        domains[i]=api->create_domain(RestrictedWait,RestrictedWaitEnd-RestrictedWait,0);CHECK(domains[i]);
        clients[i*2].domain=clients[i*2+1].domain=domains[i];
        CHECK(!pthread_create(&threads[i*2],NULL,dynamic_code_client,&clients[i*2]));
    }
    wait_requests(work,0,0);
    for (unsigned i=0;i<2;++i) channels[i]=work[i].channel;
    for (unsigned i=0;i<2;++i) {
        CHECK(!api->wake_thread(domains[i],channels[1-i]));
        CHECK(!api->thread_state(domains[i],channels[1-i]));
        pthread_t foreign;struct WaitForeign request={domains[i],channels[i]};
        CHECK(!pthread_create(&foreign,NULL,wait_foreign_controller,&request));CHECK(!pthread_join(foreign,NULL));
        CHECK(api->wake_thread(domains[i],channels[i])); /* Older epoch must not park. */
        CHECK(api->complete(&work[i]));
    }
    wait_requests(work,1,0);
    for (unsigned i=0;i<2;++i) CHECK(api->complete(&work[i]));
    for (unsigned i=0;i<2;++i) {
        wait_parked(domains[i],channels[i]);
#if defined(__AROSEXEC_SMP__)
        CHECK(SetTaskAffinity(__atomic_load_n(&clients[i*2].task,__ATOMIC_ACQUIRE),cpu_masks[1]));
#endif
        CHECK(api->suspend_thread(domains[i],channels[i])==0);
        CHECK(api->suspend_thread(domains[i],channels[i])==1);
        CHECK(api->wake_thread(domains[i],channels[i]));
        suspend_parked(domains[i],channels[i]);
        CHECK(api->resume_thread(domains[i],channels[i])==2);
        suspend_parked(domains[i],channels[i]);
        CHECK(api->resume_thread(domains[i],channels[i])==1);
    }
    wait_requests(work,2,0);
    for (unsigned i=0;i<2;++i) CHECK(api->complete(&work[i]));
    for (unsigned i=0;i<2;++i) {
        wait_parked(domains[i],channels[i]);
#if defined(__AROSEXEC_SMP__)
        CHECK(SetTaskAffinity(__atomic_load_n(&clients[i*2].task,__ATOMIC_ACQUIRE),cpu_masks[0]));
#endif
        CHECK(api->suspend_thread(domains[i],channels[i])==0);
        CHECK(api->resume_thread(domains[i],channels[i])==1);
        wait_parked(domains[i],channels[i]);
        CHECK(!pthread_create(&threads[i*2+1],NULL,dynamic_code_client,&clients[i*2+1]));
    }
    wait_requests(work,0,1);
    for (unsigned i=0;i<2;++i) {
        uint64_t words[4];memcpy(words,work[i].bytes,sizeof(words));
        words[2]=channels[i];words[3]=channels[1-i];memcpy(work[i].bytes,words,sizeof(words));
#if defined(__AROSEXEC_SMP__)
        CHECK(SetTaskAffinity(__atomic_load_n(&clients[i*2+1].task,__ATOMIC_ACQUIRE),cpu_masks[1]));
#endif
        CHECK(api->complete(&work[i]));
    }
    wait_requests(work,3,0);
    for (unsigned i=0;i<2;++i) CHECK(!pthread_join(threads[i*2+1],NULL));
    for (unsigned i=0;i<2;++i) CHECK(api->complete(&work[i]));
    for (unsigned i=0;i<2;++i) {
        wait_parked(domains[i],channels[i]);
        CHECK(api->suspend_thread(domains[i],channels[i])==0);
        CHECK(api->thread_state(domains[i],channels[i])==15);
        CHECK(api->close_domain(domains[i])==0);
    }
    for (unsigned i=0;i<4;++i) {
        if (!(i&1)) CHECK(!pthread_join(threads[i],NULL));
        CHECK(clients[i].result.status==((i&1) ? 0 : -5) && clients[i].result.drained);
        CHECK(clients[i].result.replies==((i&1) ? 1 : 4));
        CHECK(clients[i].result.denied==((i&1) ? 2 : 0));
#if defined(__AROSEXEC_SMP__)
        CHECK(clients[i].result.cpus==3 && clients[i].result.migrations>=((i&1) ? 1 : 2));
#endif
    }
    for (unsigned i=0;i<2;++i) {
        CHECK(!api->wake_thread(domains[i],channels[i]));CHECK(!api->thread_state(domains[i],channels[i]));
        CHECK(api->close_domain(domains[i])==1);
    }
    struct AROSRestrictedLifetime life;api->lifetime(&life);CHECK(!life.pages && !life.live && !life.domains);
    bug("[ISOVM] wait-wake domains=2 threads=4 early=2 controller=2 peer=2 foreign=6 native_refused=4 parked_close=2 contexts=6 retained_pages=0 PASS\n");
    bug("[ISOVM] wait-suspend wait_then_suspend=2 wake_held=2 resume_still_waiting=2 close_both=2 PASS\n");
}

extern const unsigned char RestrictedTimedWait[],RestrictedTimedWaitEnd[];
static void prove_timed_waits(void)
{
    for (unsigned mode=0;mode<4;++mode) {
        struct DynamicCodeClient client={.seed=mode};pthread_t thread;
        client.domain=api->create_domain(RestrictedTimedWait,RestrictedTimedWaitEnd-RestrictedTimedWait,0);
        CHECK(client.domain && !pthread_create(&thread,NULL,dynamic_code_client,&client));
        struct AROSRestrictedWork work;unsigned found=0;
        for (unsigned polls=0;polls<1000000;++polls) {
            if (api->take(&work)) { found=1;break; } KrnSwitch();
        }
        CHECK(found && work.length==32);
        uint64_t words[4];memcpy(words,work.bytes,sizeof(words));
        CHECK(words[0]==mode && words[1]==work.channel);
        uint64_t channel=work.channel;
        CHECK(api->complete(&work));
        if (mode) {
            wait_parked(client.domain,channel);
            struct AROSRestrictedLifetime life;api->lifetime(&life);
            CHECK(life.waits==1 && life.timed_waits==1);
            if (mode==1) CHECK(api->wake_thread(client.domain,channel));
            if (mode==2) {
                CHECK(api->suspend_thread(client.domain,channel)==0);
                unsigned state=0;
                for (unsigned polls=0;polls<10000000;++polls) {
                    state=api->thread_state(client.domain,channel);
                    CHECK(state&AROS_RESTRICTED_THREAD_SUSPENDED);
                    if (!(state&AROS_RESTRICTED_THREAD_WAITING)) break;
                    KrnSwitch();
                }
                CHECK(state==13); /* Expiry clears WAIT, never suspension. */
                api->lifetime(&life);
                CHECK(life.waits==1 && !life.timed_waits && life.suspended_threads==1);
                CHECK(api->wake_thread(client.domain,channel)); /* Cannot replace latched expiry. */
                suspend_parked(client.domain,channel);
                CHECK(api->resume_thread(client.domain,channel)==1);
            }
            if (mode==3) CHECK(api->close_domain(client.domain)==0);
        }
        CHECK(!pthread_join(thread,NULL));
        CHECK(client.result.status==(mode==3 ? -5 : 0) && client.result.drained);
        CHECK(client.result.replies==1);
        CHECK(api->close_domain(client.domain)==1);
        struct AROSRestrictedLifetime life;api->lifetime(&life);
        CHECK(!life.pages && !life.live && !life.domains && !life.waits &&
            !life.timed_waits && !life.suspended_threads);
    }
    bug("[ISOVM] timed-wait cases=4 zero=4 overflow=4 expired=2 woken=1 suspended_expiry=1 late_wake=1 closed=1 retained_pages=0 PASS\n");
}

static uint64_t timed_retirement_domain;
static struct AROSRestrictedResult timed_retirement_result;
static void timed_retirement_owner(void)
{
#if defined(__AROSEXEC_SMP__)
    pin(1);
#endif
    api->run_domain(timed_retirement_domain,4,&timed_retirement_result);
    CHECK(0); /* Removed while a finite wait is parked, before returning native. */
}
static void prove_timed_retirement(void)
{
    struct AROSRestrictedLifetime before,after;api->lifetime(&before);
    CHECK(!before.live && !before.pages && !before.waits && !before.timed_waits);
    timed_retirement_domain=api->create_domain(RestrictedTimedWait,RestrictedTimedWaitEnd-RestrictedTimedWait,0);
    CHECK(timed_retirement_domain);
    memset(&timed_retirement_result,0x6b,sizeof(timed_retirement_result));
    struct Task *task=AllocMem(sizeof(*task),MEMF_PUBLIC|MEMF_CLEAR);
    void *stack=AllocMem(256*1024,MEMF_PUBLIC|MEMF_CLEAR);CHECK(task && stack);
    task->tc_Node.ln_Type=NT_TASK;task->tc_Node.ln_Name="restricted timed retirement";
    task->tc_Node.ln_Pri=FindTask(NULL)->tc_Node.ln_Pri;
    task->tc_SPLower=stack;task->tc_SPUpper=(unsigned char *)stack+256*1024;task->tc_SPReg=task->tc_SPUpper;
    NEWLIST(&task->tc_MemEntry);CHECK(AddTask(task,timed_retirement_owner,NULL));
    struct AROSRestrictedWork work;unsigned found=0;
    for (unsigned polls=0;polls<1000000;++polls) {
        if (api->take(&work)) { found=1;break; } KrnSwitch();
    }
    CHECK(found && work.length==32);
    uint64_t words[4];memcpy(words,work.bytes,sizeof(words));
    CHECK(words[0]==4 && words[1]==work.channel && api->complete(&work));
    wait_parked(timed_retirement_domain,work.channel);
    api->lifetime(&after);CHECK(after.waits==1 && after.timed_waits==1);
    RemTask(task);FreeMem(stack,256*1024);FreeMem(task,sizeof(*task));
    api->lifetime(&after);
    CHECK(!after.waits && !after.timed_waits && !after.suspended_threads);
    CHECK(after.retired==before.retired+1);
    CHECK(!api->thread_state(timed_retirement_domain,work.channel));
    CHECK(!api->wake_thread(timed_retirement_domain,work.channel));
    int closed=0;
    for (unsigned polls=0;polls<1000000;++polls) {
        closed=api->close_domain(timed_retirement_domain);CHECK(closed>=0);
        if (closed==1) break;KrnSwitch();
    }
    CHECK(closed==1);api->lifetime(&after);
    CHECK(!after.live && !after.pages && !after.domains && !after.pending &&
        !after.waits && !after.timed_waits && !after.suspended_threads);
    for (size_t i=0;i<sizeof(timed_retirement_result);++i)
        CHECK(((const unsigned char *)&timed_retirement_result)[i]==0x6b);
    bug("[ISOVM] timed-retirement cases=1 retired=1 stale_refused=2 result_untouched=1 retained_pages=0 PASS\n");
}

extern const unsigned char RestrictedCombined[],RestrictedCombinedEnd[];
static void combined_requests(struct AROSRestrictedWork work[4],unsigned phase,
    uint64_t channels[4])
{
    unsigned seen=0;
    for (unsigned polls=0;seen!=15 && polls<1000000;++polls) {
        struct AROSRestrictedWork next;
        if (!api->take(&next)) { KrnSwitch();continue; }
        uint64_t words[4];CHECK(next.length==sizeof(words));memcpy(words,next.bytes,sizeof(words));
        CHECK(words[0]==phase && words[1]<4 && words[2]==next.channel);
        unsigned id=words[1];CHECK(!(seen&(1U<<id)));
        if (!phase) channels[id]=next.channel;else CHECK(channels[id]==next.channel);
        uint64_t value=phase==1 || phase==2 ?
            UINT64_C(0x200000000)+(id&1)*8192+(phase==1 ? 4096 : 0) :
            (phase ? 0x300 : 0x100)+id;
        CHECK(words[3]==value);work[id]=next;seen|=1U<<id;
    }
    CHECK(seen==15);
}
static void prove_combined_memory_threads(void)
{
    const uintptr_t address=UINT64_C(0x200000000),shared_address=UINT64_C(0x200010000);
    uint64_t domains[2],channels[4]={0},shared=api->create_shared(4096);CHECK(shared);
    struct DynamicCodeClient clients[4]={{.seed=0},{.seed=1},{.seed=2},{.seed=3}};
    pthread_t threads[4];struct AROSRestrictedWork work[4];
    for (unsigned d=0;d<2;++d) {
        domains[d]=api->create_domain(RestrictedCombined,RestrictedCombinedEnd-RestrictedCombined,0x1000+d);
        CHECK(domains[d] && api->reserve_memory(domains[d],address,16384));
        for (unsigned role=0;role<2;++role) {
            CHECK(api->commit_memory(domains[d],address+role*8192,3));
            CHECK(api->commit_memory(domains[d],address+role*8192+4096,0));
        }
        CHECK(api->reserve_memory(domains[d],shared_address,4096));
        CHECK(api->attach_shared(domains[d],shared_address,shared));
        CHECK(api->commit_shared(domains[d],shared_address,3));
        for (unsigned role=0;role<2;++role) clients[d*2+role].domain=domains[d];
    }
    for (unsigned id=0;id<4;++id) CHECK(!pthread_create(&threads[id],NULL,dynamic_code_client,&clients[id]));
    combined_requests(work,0,channels);
    for (unsigned id=0;id<4;++id) {
        CHECK(api->protect_memory(domains[id/2],address+(id&1)*8192,1));
#if defined(__AROSEXEC_SMP__)
        CHECK(SetTaskAffinity(__atomic_load_n(&clients[id].task,__ATOMIC_ACQUIRE),cpu_masks[1]));
#endif
        CHECK(api->complete(&work[id]));
    }
    for (unsigned phase=1;phase<=2;++phase) {
        combined_requests(work,phase,channels);
        for (unsigned id=0;id<4;++id) {
            CHECK(api->protect_memory(domains[id/2],address+(id&1)*8192+(phase==1 ? 4096 : 0),3));
#if defined(__AROSEXEC_SMP__)
            if (phase==2) CHECK(SetTaskAffinity(__atomic_load_n(&clients[id].task,__ATOMIC_ACQUIRE),cpu_masks[0]));
#endif
            CHECK(api->complete(&work[id]));
        }
    }
    combined_requests(work,3,channels);
    for (unsigned id=0;id<4;++id) CHECK(api->complete(&work[id]));
    for (unsigned id=0;id<4;++id) wait_parked(domains[id/2],channels[id]);
    for (unsigned id=0;id<4;++id) {
        CHECK(api->decommit_memory(domains[id/2],address+(id&1)*8192+4096));
        CHECK(api->commit_memory(domains[id/2],address+(id&1)*8192+4096,3));
        CHECK(api->wake_thread(domains[id/2],channels[id]));
    }
    combined_requests(work,4,channels);
    CHECK(!api->close_shared(shared)); /* Refused while views retain backing. */
    for (unsigned d=0;d<2;++d) CHECK(api->close_domain(domains[d])==0);
    struct AROSRestrictedLifetime held;api->lifetime(&held);
    /* pending counts abandoned native contexts, not worker leases. */
    CHECK(held.live==4 && !held.pending && held.domains==2 && held.shared_objects==1 && held.pages);
    for (unsigned id=0;id<4;++id) {
        /* Close requests cancellation; the client acknowledges it on dispatch.
         * Retain the worker lease until that transition is observable. */
        unsigned attempts=0;
        while (api->state(&work[id])==AROS_RESTRICTED_RUNNING) {
            CHECK(++attempts<100000);KrnSwitch();
        }
        CHECK(api->state(&work[id])==AROS_RESTRICTED_CANCELLED);
        memset(work[id].bytes,0xee,work[id].length);
        CHECK(api->complete(&work[id]));CHECK(!api->complete(&work[id]));
    }
    unsigned snapshots[2]={0};
    for (unsigned id=0;id<4;++id) {
        CHECK(!pthread_join(threads[id],NULL));
        struct AROSRestrictedResult *result=&clients[id].result;
        CHECK(result->status==-5 && result->drained && result->resumed==2 && result->denied==2 && result->replies==4);
#if defined(__AROSEXEC_SMP__)
        CHECK(result->cpus==3 && result->migrations>=2);
#endif
        if (result->snapshot_valid) {
            ++snapshots[id/2];uint64_t value;memcpy(&value,result->data,sizeof(value));
            CHECK(value==0x1000+id/2);
            for (unsigned role=0;role<2;++role)
                for (unsigned byte=0;byte<32;++byte) CHECK(result->data[512+role*256+64+byte]==0xa5);
        }
    }
    for (unsigned d=0;d<2;++d) CHECK(snapshots[d]==1 && api->close_domain(domains[d])==1);
    CHECK(api->close_shared(shared));
    CHECK(!api->close_shared(shared));
    api->lifetime(&held);
    CHECK(!held.live && !held.pending && !held.domains && !held.shared_objects && !held.pages &&
        !held.waits && !held.timed_waits && !held.suspended_threads);
    bug("[ISOVM] combined domains=2 threads=4 private_values=4 shared_slots=4 repaired=8 premature=8 waits=4 recommit_zero=4 held_workers=4 snapshots=2 no_late_write=4 retained_pages=0 PASS\n");
}

extern const unsigned char RestrictedSuspend[],RestrictedSuspendEnd[];
extern const unsigned char RestrictedSuspendNoCalls[],RestrictedSuspendNoCallsEnd[];
extern int NativeRunChecked(int (*run)(uint64_t,uint64_t,struct AROSRestrictedResult *),
    uint64_t domain,uint64_t seed,struct AROSRestrictedResult *result);
static void *suspend_client(void *argument)
{
    struct DynamicCodeClient *client=argument;
#if defined(__AROSEXEC_SMP__)
    pin((client->seed&1) ? 0 : 1);
#endif
    __atomic_store_n(&client->task,FindTask(NULL),__ATOMIC_RELEASE);
    CHECK(NativeRunChecked(api->run_domain,client->domain,client->seed,&client->result));return NULL;
}
static void *suspend_foreign_controller(void *argument)
{
    struct WaitForeign *p=argument;
    CHECK(api->suspend_thread(p->domain,p->channel)==-1);
    CHECK(api->resume_thread(p->domain,p->channel)==-1);return NULL;
}
static void prove_suspension(void)
{
    const uintptr_t address=UINT64_C(0x200000000);
    for (unsigned mode=0;mode<2;++mode) {
        struct DynamicCodeClient clients[2]={{.seed=mode*2},{.seed=1}};pthread_t threads[2];
        uint64_t shared=api->create_shared(4096);CHECK(shared);
        for (unsigned i=0;i<2;++i) {
            clients[i].domain=api->create_domain(RestrictedSuspend,RestrictedSuspendEnd-RestrictedSuspend,0);CHECK(clients[i].domain);
            CHECK(api->reserve_memory(clients[i].domain,address,4096));
            CHECK(api->attach_shared(clients[i].domain,address,shared));CHECK(api->commit_shared(clients[i].domain,address,3));
            CHECK(!pthread_create(&threads[i],NULL,suspend_client,&clients[i]));
        }
        struct AROSRestrictedWork work[2];unsigned held=0;
        for (unsigned polls=0;held!=3 && polls<1000000;++polls) {
            struct AROSRestrictedWork next;if (!api->take(&next)) { KrnSwitch();continue; }
            uint64_t seed;CHECK(next.length==8);memcpy(&seed,next.bytes,8);
            unsigned id=seed&1;CHECK(seed==clients[id].seed && !(held&(1U<<id)));work[id]=next;held|=1U<<id;
        }
        CHECK(held==3); /* Writer submits only after reader entered its no-SVC region. */
        uint64_t domain=clients[0].domain,channel=work[0].channel;
        CHECK(api->suspend_thread(clients[1].domain,channel)==-1);
        CHECK(api->resume_thread(clients[1].domain,channel)==-1);
        struct WaitForeign request={domain,channel};pthread_t foreign;
        CHECK(!pthread_create(&foreign,NULL,suspend_foreign_controller,&request));CHECK(!pthread_join(foreign,NULL));
        CHECK(api->resume_thread(domain,channel)==0);
        CHECK(api->suspend_thread(domain,channel)==0);suspend_parked(domain,channel);
        CHECK(api->suspend_thread(domain,channel)==1);
        CHECK(api->wake_thread(domain,channel));suspend_parked(domain,channel);
        CHECK(api->complete(&work[0]));CHECK(api->complete(&work[1]));
        CHECK(!pthread_join(threads[1],NULL));
        CHECK(!clients[1].result.status && clients[1].result.drained && clients[1].result.replies==1 && clients[1].result.denied==2);
        /* Writer has published the loop exit flag, yet the reader must remain parked. */
        for (unsigned check=0;check<64;++check) { CHECK(api->thread_state(domain,channel)==13);KrnSwitch(); }
        CHECK(api->resume_thread(domain,channel)==2);
        for (unsigned check=0;check<64;++check) { CHECK(api->thread_state(domain,channel)==13);KrnSwitch(); }
        if (!mode) {
#if defined(__AROSEXEC_SMP__)
            CHECK(SetTaskAffinity(__atomic_load_n(&clients[0].task,__ATOMIC_ACQUIRE),cpu_masks[0]));
#endif
            CHECK(api->resume_thread(domain,channel)==1);
        } else CHECK(api->close_domain(domain)==0);
        CHECK(!pthread_join(threads[0],NULL));
        CHECK(clients[0].result.status==(mode ? -5 : 0) && clients[0].result.drained);
        CHECK(clients[0].result.replies==(mode ? 0 : 1));
        if (mode) CHECK(clients[0].result.pc>=AROS_RESTRICTED_CODE+(RestrictedSuspendNoCalls-RestrictedSuspend) &&
            clients[0].result.pc<AROS_RESTRICTED_CODE+(RestrictedSuspendNoCallsEnd-RestrictedSuspend));
#if defined(__AROSEXEC_SMP__)
        if (!mode) CHECK(clients[0].result.cpus==3 && clients[0].result.migrations);
#endif
        CHECK(api->suspend_thread(domain,channel)==-1 && api->resume_thread(domain,channel)==-1);
        for (unsigned i=0;i<2;++i) CHECK(api->close_domain(clients[i].domain)==1);
        CHECK(api->close_shared(shared));
    }
    struct AROSRestrictedLifetime life;api->lifetime(&life);CHECK(!life.pages && !life.live && !life.domains && !life.shared_objects);
    bug("[ISOVM] native-return calls=4 normal=3 cancelled=1 gp=11 simd_low=8 fpcr=1 tls=1 stack=1 PASS\n");
    bug("[ISOVM] suspend-context stable_gp=30 loop_result=1 simd=32 fp=1 nzcv=1 tls=1 stack=1 migrated=1 PASS\n");
    bug("[ISOVM] suspend cpu_bound=2 nested=2 wake_held=2 peer_publications=2 held_checks=256 resumed=1 closed=1 foreign=8 native_refused=4 retained_pages=0 PASS\n");
}

int main(void)
{
    KernelBase = OpenResource((CONST_STRPTR)"kernel.resource");
    CHECK(KernelBase != NULL);
#if defined(__AROSEXEC_SMP__)
    CHECK(KrnGetCPUCount()==2);
    TaskResBase=OpenResource((CONST_STRPTR)"task.resource");CHECK(TaskResBase);
    for (unsigned cpu=0;cpu<2;++cpu) {
        cpu_masks[cpu]=KrnAllocCPUMask();CHECK(cpu_masks[cpu]);
        KrnGetCPUMask(cpu,cpu_masks[cpu]);
    }
#endif
    intptr_t address = KrnGetSystemAttr(AROS_RESTRICTED_KATTR_API);
    CHECK(address != -1 && address != 0);
    api = (const void *)address;
    CHECK(api->version == 30 && api->size == sizeof(*api));
    extern const unsigned char RestrictedRestore[],RestrictedRestoreEnd[];
    static struct AROSRestrictedResult restored;
    CHECK(api->run(RestrictedRestore,(size_t)(RestrictedRestoreEnd-RestrictedRestore),0,&restored));
    CHECK(restored.status==0 && restored.rejected==1);
    struct AROSRestrictedLifetime restore_life;api->lifetime(&restore_life);
    CHECK(!restore_life.pages && !restore_life.live && !restore_life.domains);
    bug("[ISOVM] restore-context gp=31 simd=32 fp=1 nzcv=1 stack=1 size_refused=1 retained_pages=0 PASS\n");
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
        CHECK(results[id].denied == 23);
        CHECK(results[id].cancelled==1 && results[id].drained==1 && results[id].snapshot_valid);
        for (unsigned byte=0;byte<8;++byte) {
            CHECK(results[id].data[512+byte]==0xa5);
            CHECK(results[id].data[544+byte]==0xa5);
        }
        CHECK(results[id].replies == 201 && results[id].rejected == 8 && results[id].callbacks == 100);
    }
    CHECK(worker_replies == 402 && worker_ack == 3);
    CHECK(results[0].close_waits>0 && results[1].close_waits==0);
    CHECK(results[0].resumed == 1 && results[1].resumed == 1);
    bug("[ISOVM] context-transitions gp=30 fault_gp=31 simd=32 yields=3072 callbacks=200 faults=2 fp=1 nzcv=1 tls=1 stack=1 PASS\n");
    CHECK(beats > 0 && after_fault > 0);
#if defined(__AROSEXEC_SMP__)
    for (unsigned id=0;id<2;++id) CHECK(results[id].cpus==3 && results[id].migrations>0);
    bug("[ISOVM] migration cpus0=%llu cpus1=%llu moves0=%llu moves1=%llu PASS\n",
        (unsigned long long)results[0].cpus,(unsigned long long)results[1].cpus,
        (unsigned long long)results[0].migrations,(unsigned long long)results[1].migrations);
    pin(0);
#endif
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
    static struct AROSRestrictedResult fault_policy;
    for (unsigned policy=0;policy<5;++policy) {
        CHECK(api->run(RestrictedFaultPolicy,
            (size_t)(RestrictedFaultPolicyEnd-RestrictedFaultPolicy),policy,&fault_policy));
        CHECK(fault_policy.status==(policy==4 ? 0 : -2) && fault_policy.drained==1);
        CHECK(fault_policy.resumed==(policy==4) && fault_policy.rejected==(policy==4));
    }
    struct AROSRestrictedLifetime fault_lifetime;
    api->lifetime(&fault_lifetime);
    CHECK(!fault_lifetime.live && !fault_lifetime.pending && !fault_lifetime.pages);
    bug("[ISOVM] fault-stack alternate=1 unregister=1 active-refusal=1 nested=1 guards=2 drained=5 PASS\n");
    prove_shared_domains();
    prove_private_memory(UINT64_C(0x200000000));
    prove_private_memory(UINT64_C(0x7ffe0000));
    bug("[ISOVM] low-address fixed=0x7ffe0000 domains=2 zero=3 isolated=2 recommit=1 guard_fault=1 foreign=5 exclusions=4 kernel_exclusions=2 retained_pages=0 PASS\n");
    prove_shared_memory();
    prove_region_faults();
    prove_replaced_fault();
    prove_domain_growth();
    prove_thread_growth();
    prove_table_growth();
    prove_large_stacks();
    prove_large_code();
    prove_dynamic_code();
    prove_dynamic_continuations();
    prove_shared_code();
#if defined(__AROSEXEC_SMP__)
    bug("[ISOVM] code-revisit private=2 shared=2 original_cpu=1 rewritten_values=4 PASS\n");
#endif
    prove_reserved_faults();
    prove_waits();
    prove_suspension();
    prove_timed_waits();
    prove_timed_retirement();
    bug("[ISOVM] wait-context gp=30 simd=32 fp=1 nzcv=1 tls=1 stack=1 returned=9 PASS\n");
    prove_combined_memory_threads();
    prove_domain_close();
#if defined(__AROSEXEC_SMP__)
    prove_task_list_churn();
    for (unsigned cpu=0;cpu<2;++cpu) KrnFreeCPUMask(cpu_masks[cpu]);
#endif
    bug("[ISOVM] PASS restricted=2 yields=3072 denied=46 faulted=1 survived=1 heartbeat=%u after_fault=%u joined=5 replies=402 rejected=16 callbacks=200 resumed=2 cancelled=3 worker_ack=3 close_pending=1 drained=4 removed_worker=1 removed_owners=8 reaped_owners=8 retained_pages=0\n",beats,after_fault);
    return 0;
}
