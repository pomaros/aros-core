/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "service.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <aros/debug.h>

static pthread_mutex_t lock;
static pthread_cond_t changed;
static struct KrnMappingDomain domain;
static struct KrnMappingDomainMember creator;
static struct KrnServiceQueue queue;
struct Client { uint64_t token; unsigned phase, hold; };
static struct Client clients[2];
static unsigned stop, beats, replies, cancellations, acknowledgements, arrived;
static struct Task *identities[4];
#define CHECK(x) do { if (!(x)) { bug("[SERVICEVM] FAIL line=%d\n", __LINE__); abort(); } } while (0)
static void acquire(void) { CHECK(!pthread_mutex_lock(&lock)); }
static void release(void) { CHECK(!pthread_mutex_unlock(&lock)); }
static void wake(void) { CHECK(!pthread_cond_broadcast(&changed)); }
static void wait_change(void) { CHECK(!pthread_cond_wait(&changed, &lock)); }

static void enter(unsigned index)
{
    acquire();
    identities[index] = FindTask(NULL);
    CHECK(identities[index] != NULL);
    ++arrived; wake();
    while (arrived != 4) wait_change();
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned j = i + 1; j < 4; ++j) CHECK(identities[i] != identities[j]);
    release();
}

static void *heartbeat(void *unused)
{
    (void)unused;
    enter(0);
    for (;;) {
        acquire();
        if (stop) { release(); return NULL; }
        ++beats; wake(); release(); Delay(1);
    }
}

static void *worker(void *unused)
{
    (void)unused;
    enter(1);
    for (;;) {
        acquire();
        unsigned id;
        for (;;) {
            for (id = 0; id < 2; ++id) if (clients[id].phase == 1) break;
            if (id < 2) break;
            if (stop) { release(); return NULL; }
            wait_change();
        }
        struct Client *c = &clients[id];
        unsigned char bytes[32]; size_t n = 0;
        uint64_t token = c->token;
        CHECK(KrnServiceTake(&queue, token, bytes, sizeof(bytes), &n) && n == sizeof(bytes));
        c->phase = 2; wake();
        while (c->hold && c->phase != 4) wait_change();
        unsigned cancelled = c->hold;
        release();
        for (unsigned j = 0; j < sizeof(bytes); ++j) bytes[j] ^= 0x5a;
        acquire();
        CHECK(KrnServiceComplete(&queue, token, bytes, n));
        if (cancelled) ++acknowledgements;
        c->phase = 3; wake(); release();
    }
}

static void *client(void *arg)
{
    unsigned id = (unsigned)(uintptr_t)arg;
    enter(id + 2);
    struct Client *c = &clients[id];
    unsigned char input[32], output[32];
    for (unsigned round = 0; round < 100; ++round) {
        for (unsigned j = 0; j < sizeof(input); ++j) input[j] = (unsigned char)(id * 71 + round + j);
        acquire();
        CHECK(KrnServiceSubmit(&queue, id + 1, input, sizeof(input), &c->token));
        c->phase = 1; wake();
        while (c->phase != 3) wait_change();
        size_t n = 0;
        CHECK(KrnServiceCollect(&queue, id + 1, c->token, output, sizeof(output), &n) && n == sizeof(output));
        for (unsigned j = 0; j < sizeof(output); ++j) CHECK(output[j] == (unsigned char)(input[j] ^ 0x5a));
        CHECK(!KrnServiceCollect(&queue, id + 1, c->token, output, sizeof(output), &n));
        ++replies; c->phase = 0; release();
    }
    acquire();
    uint64_t stale;
    CHECK(KrnServiceSubmit(&queue, id + 1, input, sizeof(input), &stale));
    CHECK(KrnServiceCancel(&queue, id + 1, stale));
    ++cancellations;
    CHECK(KrnServiceSubmit(&queue, id + 1, input, sizeof(input), &c->token));
    size_t n = 0;
    CHECK(!KrnServiceTake(&queue, stale, output, sizeof(output), &n));
    CHECK(!KrnServiceCancel(&queue, id + 1, stale));
    c->hold = 1; c->phase = 1; wake();
    while (c->phase != 2) wait_change();
    unsigned prior = beats;
    while (beats == prior) wait_change();
    CHECK(KrnServiceCancel(&queue, id + 1, c->token));
    CHECK(!KrnServiceDrained(&queue));
    ++cancellations; c->phase = 4; wake();
    while (c->phase != 3) wait_change();
    memset(output, 0xa5, sizeof(output)); n = 77;
    CHECK(!KrnServiceCollect(&queue, id + 1, c->token, output, sizeof(output), &n));
    CHECK(n == 77 && output[0] == 0xa5 && output[31] == 0xa5);
    c->phase = 0; release();
    return NULL;
}

int main(void)
{
    pthread_t task[4];
    CHECK(!pthread_mutex_init(&lock, NULL));
    CHECK(!pthread_cond_init(&changed, NULL));
    CHECK(KrnMappingDomainJoin(&domain, &creator, &creator));
    CHECK(KrnServiceInitialize(&queue, &domain));
    CHECK(!pthread_create(&task[0], NULL, heartbeat, NULL));
    CHECK(!pthread_create(&task[1], NULL, worker, NULL));
    CHECK(!pthread_create(&task[2], NULL, client, (void *)(uintptr_t)0));
    CHECK(!pthread_create(&task[3], NULL, client, (void *)(uintptr_t)1));
    CHECK(!pthread_join(task[2], NULL)); CHECK(!pthread_join(task[3], NULL));
    acquire();
    CHECK(replies == 200 && cancellations == 4 && acknowledgements == 2 && beats >= 2);
    CHECK(KrnServiceDrained(&queue));
    KrnServiceClose(&queue);
    CHECK(KrnMappingDomainLeave(&creator));
    CHECK(KrnMappingDomainCanReclaim(&domain));
    stop = 1; wake(); release();
    CHECK(!pthread_join(task[1], NULL)); CHECK(!pthread_join(task[0], NULL));
    CHECK(!pthread_cond_destroy(&changed)); CHECK(!pthread_mutex_destroy(&lock));
    bug("[SERVICEVM] PASS clients=2 replies=200 cancelled=4 worker_ack=2 heartbeat=%u drained=1 joined=4 distinct_tasks=4\n", beats);
    puts("[SERVICEVM] command-return=0");
    return 0;
}
