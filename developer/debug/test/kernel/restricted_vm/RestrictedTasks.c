/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/kernel.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <aros/debug.h>
#include <aros/apple/restricted.h>
APTR KernelBase;
extern const unsigned char RestrictedCode[], RestrictedCodeEnd[];
static const struct AROSRestrictedAPI *api;
static struct AROSRestrictedResult results[2];
static unsigned worker_replies;
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
            for (unsigned i=0;i<work.length;++i) work.bytes[i]^=0x5a;
            CHECK(api->complete(&work));
            CHECK(!api->complete(&work)); /* Stale completion is rejected. */
            ++worker_replies;
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
    CHECK(api->version == 3 && api->size == sizeof(*api));
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
        CHECK(results[id].denied == 3);
        CHECK(results[id].replies == 200 && results[id].rejected == 5 && results[id].callbacks == 100);
    }
    CHECK(worker_replies == 400);
    CHECK(beats > 0 && after_fault > 0);
    bug("[ISOVM] PASS restricted=2 yields=3072 denied=6 faulted=1 survived=1 heartbeat=%u after_fault=%u joined=4 replies=400 rejected=10 callbacks=200\n",beats,after_fault);
    return 0;
}
