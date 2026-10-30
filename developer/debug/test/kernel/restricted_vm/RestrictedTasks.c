/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/kernel.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <aros/debug.h>
#include <aros/apple/restricted.h>
APTR KernelBase;
extern const unsigned char RestrictedCode[], RestrictedCodeEnd[];
extern const unsigned char RestrictedQueued[], RestrictedQueuedEnd[];
static const struct AROSRestrictedAPI *api;
static struct AROSRestrictedResult results[2];
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
int main(void)
{
    KernelBase = OpenResource((CONST_STRPTR)"kernel.resource");
    CHECK(KernelBase != NULL);
    intptr_t address = KrnGetSystemAttr(AROS_RESTRICTED_KATTR_API);
    CHECK(address != -1 && address != 0);
    api = (const void *)address;
    CHECK(api->version == 5 && api->size == sizeof(*api));
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
        CHECK(results[id].denied == 10);
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
    bug("[ISOVM] PASS restricted=2 yields=3072 denied=20 faulted=1 survived=1 heartbeat=%u after_fault=%u joined=4 replies=402 rejected=16 callbacks=200 resumed=2 cancelled=3 worker_ack=3 close_pending=1 drained=3\n",beats,after_fault);
    return 0;
}
