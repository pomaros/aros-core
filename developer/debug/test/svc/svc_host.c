/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: [SMP13] on the host: the synthetic service in the hosted vehicle
          (one host thread per side, a condition variable as SvcWake), and
          its dropped-wake control.

    Build and run: developer/debug/test/svc/run-host.sh
*/

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "svc_synth.h"

struct HostWake {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int flag;
};

static void host_deliver(struct SvcWaker *w)
{
    struct HostWake *h = w->target;

    pthread_mutex_lock(&h->lock);
    h->flag = 1;
    pthread_cond_signal(&h->cond);
    pthread_mutex_unlock(&h->lock);
}

int svc_synth_wait(struct SvcSynth *s, struct SvcWaker *w, uint32_t timeout_ms)
{
    struct HostWake *h = w->target;
    struct timespec until;
    int got;

    (void)s;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += (long)timeout_ms * 1000000L;
    until.tv_sec += until.tv_nsec / 1000000000L;
    until.tv_nsec %= 1000000000L;
    pthread_mutex_lock(&h->lock);
    while (!h->flag && pthread_cond_timedwait(&h->cond, &h->lock, &until) == 0)
        ;
    got = h->flag;
    h->flag = 0;
    pthread_mutex_unlock(&h->lock);
    if (got)
        SvcTake(w);
    return got;
}

static void *engine_thread(void *arg)
{
    svc_synth_engine(arg);
    return NULL;
}

static int run(const char *name, uint32_t drop_every, uint32_t *digest_out)
{
    static struct SvcSynth s;
    struct HostWake he = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };
    struct HostWake hc = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };
    pthread_t engine;
    uint32_t digest;

    memset(&s, 0, sizeof(s));
    svc_synth_init(&s);
    SvcWakerCustom(&s.to_engine, SVC_VEHICLE_HOSTED, host_deliver, &he);
    SvcWakerCustom(&s.to_client, SVC_VEHICLE_HOSTED, host_deliver, &hc);
    s.to_engine.drop_every = s.to_client.drop_every = drop_every;
    pthread_create(&engine, NULL, engine_thread, &s);
    digest = svc_synth_client(&s);
    pthread_join(engine, NULL);
    *digest_out = digest;
    printf("[SMP13] vehicle=%s digest=%08x lost-wakes=%u ring-overruns=%u idle-spin=%u "
        "wakes=%u/%u taken=%u/%u timeouts=%u/%u\n", name, digest,
        s.to_engine.lost + s.to_client.lost, s.overruns, s.spins,
        (unsigned)s.to_engine.sent, (unsigned)s.to_client.sent,
        (unsigned)s.to_engine.taken, (unsigned)s.to_client.taken, s.timeouts_engine, s.timeouts_client);
    return (int)(s.to_engine.lost + s.to_client.lost);
}

int main(void)
{
    uint32_t hosted, control;
    int lost = run("hosted", 0, &hosted);
    int dropped = run("hosted-control-drop-100", 100, &control);

    if (lost == 0 && dropped > 0 && hosted == control) {
        printf("[SMP13] PASS host: vehicle=hosted digest=%08x lost-wakes=0 "
            "(control) dropped-wakes: lost-wakes=%d digest-equal=1\n", hosted, dropped);
        return 0;
    }
    printf("[SMP13] FAIL host: lost=%d control-lost=%d digest %08x/%08x\n", lost, dropped, hosted, control);
    return 1;
}
