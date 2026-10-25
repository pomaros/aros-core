/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: RegisterStress - every general and vector register, TPIDR_EL0 and a
          thread_local survive preemption. Threads fill x0-x28, x30 and
          v0-v31 with their own pattern, spin long enough to be preempted by
          the timer, then store and compare everything. A context switch that
          loses or mixes up one register shows another thread's pattern.
*/
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define THREADS 4
#define WORDS (30 + 64)     /* x0-x28, x30, then q0-q31 as two words each */

extern void register_round(uint64_t pattern, uint64_t spins, uint64_t *out);

/* x0 pattern, x1 spins, x2 out. Callee-saved registers are saved first. */
__asm__(
    ".text\n"
    ".p2align 2\n"
    ".globl register_round\n"
    "register_round:\n"
    "    stp x29, x30, [sp, #-176]!\n"
    "    stp x19, x20, [sp, #16]\n"
    "    stp x21, x22, [sp, #32]\n"
    "    stp x23, x24, [sp, #48]\n"
    "    stp x25, x26, [sp, #64]\n"
    "    stp x27, x28, [sp, #80]\n"
    "    stp d8, d9, [sp, #96]\n"
    "    stp d10, d11, [sp, #112]\n"
    "    stp d12, d13, [sp, #128]\n"
    "    stp d14, d15, [sp, #144]\n"
    "    str x2, [sp, #160]\n"
    "    mov x29, x1\n"
    "    mov x30, x0\n"
#define V(n) \
    "    add x1, x30, #" #n "*2+100\n" \
    "    add x2, x30, #" #n "*2+101\n" \
    "    fmov d" #n ", x1\n" \
    "    mov v" #n ".d[1], x2\n"
    V(0) V(1) V(2) V(3) V(4) V(5) V(6) V(7) V(8) V(9) V(10) V(11) V(12)
    V(13) V(14) V(15) V(16) V(17) V(18) V(19) V(20) V(21) V(22) V(23)
    V(24) V(25) V(26) V(27) V(28) V(29) V(30) V(31)
#undef V
#define X(n) "    add x" #n ", x30, #" #n "\n"
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12)
    X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23)
    X(24) X(25) X(26) X(27) X(28)
#undef X
    "1:  subs x29, x29, #1\n"
    "    b.ne 1b\n"
    "    str x0, [sp, #168]\n"
    "    ldr x0, [sp, #160]\n"
    "    stp x1, x2, [x0, #8]\n"
    "    stp x3, x4, [x0, #24]\n"
    "    stp x5, x6, [x0, #40]\n"
    "    stp x7, x8, [x0, #56]\n"
    "    stp x9, x10, [x0, #72]\n"
    "    stp x11, x12, [x0, #88]\n"
    "    stp x13, x14, [x0, #104]\n"
    "    stp x15, x16, [x0, #120]\n"
    "    stp x17, x18, [x0, #136]\n"
    "    stp x19, x20, [x0, #152]\n"
    "    stp x21, x22, [x0, #168]\n"
    "    stp x23, x24, [x0, #184]\n"
    "    stp x25, x26, [x0, #200]\n"
    "    stp x27, x28, [x0, #216]\n"
    "    str x30, [x0, #232]\n"
    "    ldr x1, [sp, #168]\n"
    "    str x1, [x0, #0]\n"
    "    add x0, x0, #240\n"
    "    stp q0, q1, [x0, #0]\n"
    "    stp q2, q3, [x0, #32]\n"
    "    stp q4, q5, [x0, #64]\n"
    "    stp q6, q7, [x0, #96]\n"
    "    stp q8, q9, [x0, #128]\n"
    "    stp q10, q11, [x0, #160]\n"
    "    stp q12, q13, [x0, #192]\n"
    "    stp q14, q15, [x0, #224]\n"
    "    stp q16, q17, [x0, #256]\n"
    "    stp q18, q19, [x0, #288]\n"
    "    stp q20, q21, [x0, #320]\n"
    "    stp q22, q23, [x0, #352]\n"
    "    stp q24, q25, [x0, #384]\n"
    "    stp q26, q27, [x0, #416]\n"
    "    stp q28, q29, [x0, #448]\n"
    "    stp q30, q31, [x0, #480]\n"
    "    ldp d8, d9, [sp, #96]\n"
    "    ldp d10, d11, [sp, #112]\n"
    "    ldp d12, d13, [sp, #128]\n"
    "    ldp d14, d15, [sp, #144]\n"
    "    ldp x19, x20, [sp, #16]\n"
    "    ldp x21, x22, [sp, #32]\n"
    "    ldp x23, x24, [sp, #48]\n"
    "    ldp x25, x26, [sp, #64]\n"
    "    ldp x27, x28, [sp, #80]\n"
    "    ldp x29, x30, [sp], #176\n"
    "    ret\n");

static _Thread_local uint64_t mine;

struct worker
{
    pthread_t thread;
    unsigned id;
    unsigned long rounds, bad, preempted;
    char first[160];
};

static unsigned long spins = 4000000, rounds = 400;
static volatile unsigned last_runner;

static uint64_t tpidr(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(value));
    return value;
}

static const char *word_name(unsigned word, char *name)
{
    if (word < 29)
        sprintf(name, "x%u", word);
    else if (word == 29)
        sprintf(name, "x30");
    else
        sprintf(name, "q%u.%s", (word - 30) / 2, (word & 1) ? "hi" : "lo");
    return name;
}

static void *run(void *arg)
{
    struct worker *w = arg;
    uint64_t out[WORDS], tp = tpidr();
    unsigned long round;

    mine = 0x5100000000000000ULL | w->id;
    for (round = 0; round < rounds; ++round) {
        uint64_t pattern = ((uint64_t)(w->id + 1) << 56) | ((uint64_t)round << 16);
        unsigned word;

        last_runner = w->id;
        register_round(pattern, spins, out);
        /* Another thread ran during the spin: this round was preempted */
        if (last_runner != w->id)
            ++w->preempted;
        for (word = 0; word < WORDS; ++word) {
            uint64_t expect = word < 29 ? pattern + word :
                word == 29 ? pattern :
                pattern + 100 + (word - 30);
            if (out[word] != expect) {
                char name[16];

                if (w->bad++ == 0)
                    snprintf(w->first, sizeof(w->first),
                        "round %lu %s expect 0x%llx got 0x%llx", round,
                        word_name(word, name), (unsigned long long)expect,
                        (unsigned long long)out[word]);
            }
        }
        if (tpidr() != tp || mine != (0x5100000000000000ULL | w->id)) {
            if (w->bad++ == 0)
                snprintf(w->first, sizeof(w->first),
                    "round %lu tpidr 0x%llx->0x%llx thread_local 0x%llx", round,
                    (unsigned long long)tp, (unsigned long long)tpidr(),
                    (unsigned long long)mine);
        }
        w->rounds = round + 1;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    struct worker workers[THREADS] = {{0}};
    unsigned long bad = 0;
    unsigned i;

    if (argc > 1)
        rounds = strtoul(argv[1], NULL, 0);
    if (argc > 2)
        spins = strtoul(argv[2], NULL, 0);
    printf("REGSTRESS threads=%d rounds=%lu spins=%lu\n", THREADS, rounds, spins);
    fflush(stdout);
    for (i = 0; i < THREADS; ++i) {
        workers[i].id = i;
        if (pthread_create(&workers[i].thread, NULL, run, &workers[i]) != 0)
            return 20;
    }
    for (i = 0; i < THREADS; ++i) {
        pthread_join(workers[i].thread, NULL);
        printf("REGSTRESS thread %u rounds=%lu preempted=%lu bad=%lu %s\n", i,
            workers[i].rounds, workers[i].preempted, workers[i].bad,
            workers[i].first);
        bad += workers[i].bad;
    }
    printf("REGSTRESS result=%s bad=%lu\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 10 : 0;
}
