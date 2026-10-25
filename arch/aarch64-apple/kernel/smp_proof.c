/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Ordinary Exec tasks: pinned concurrent compute, Wait/Signal, and teardown. */
#include <exec/tasks.h>
#include <exec/errors.h>
#include <proto/timer.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <aros/apple/runtime.h>
#include "smp.h"
#include "cpu_monitor.h"
#include "page_allocator.h"
#include <exec/memory.h>
#include <aros/kernel.h>
#include <aros/debug.h>
struct SMPWorker {
    struct Task *task;
    unsigned cpu;
    uint32_t command,state;
    uint64_t until,start,end,count,gpr,simd;
    uint64_t ap_ticks_before,ap_ticks_after;
    unsigned ran_before,ran_after;
};
static struct SMPWorker workers[2];
static const char *names[2]={"SMP ordinary task 0","SMP ordinary task 1"};
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
#define PROOF_GPR "x18"
#define PROOF_REGISTERS "x18/FPSIMD"
#else
#define PROOF_GPR "x17"
#define PROOF_REGISTERS "x17/FPSIMD"
#endif
static void worker(struct SMPWorker *w)
{
    __atomic_store_n(&w->state,1,__ATOMIC_RELEASE);
    for(;;) {
        Wait(SIGF_SINGLE);
        uint32_t command=__atomic_load_n(&w->command,__ATOMIC_ACQUIRE);
        if(command==3) {
            __atomic_store_n(&w->state,3,__ATOMIC_RELEASE);
            return;
        }
        if(command!=1) continue;
        w->ran_before=KrnGetCPUNumber();
        uint64_t now,count,gpr,simd,seed=UINT64_C(0x12345678abcdef00)+w->cpu;
        uint64_t deadline=w->until;
        w->ap_ticks_before=AROSAppleSMPTimerTicks();
        __asm__ volatile("isb; mrs %0,cntvct_el0":"=r"(w->start));
        /* Leave both registers live across real timer IRQ preemptions. */
        __asm__ volatile(
            ".arch_extension fp\n\t"
            "mov " PROOF_GPR ",%[seed]\n\t"
            "fmov d0,%[seed]\n\t"
            "mov %[count],xzr\n"
            "1: add %[count],%[count],#1\n\t"
            "isb\n\tmrs %[now],cntvct_el0\n\t"
            "cmp %[now],%[until]\n\tb.lo 1b\n\t"
            "mov %[gpr]," PROOF_GPR "\n\tfmov %[simd],d0"
            : [now]"=&r"(now),[count]"=&r"(count),[gpr]"=&r"(gpr),[simd]"=&r"(simd)
            : [seed]"r"(seed),[until]"r"(deadline) : PROOF_GPR,"v0","cc","memory");
        w->end=now;w->count=count;w->gpr=gpr;w->simd=simd;
        w->ap_ticks_after=AROSAppleSMPTimerTicks();
        w->ran_after=KrnGetCPUNumber();
        __atomic_store_n(&w->state,2,__ATOMIC_RELEASE);
    }
}
static void pause_sample(struct timerequest *timer)
{
    timer->tr_node.io_Command=TR_ADDREQUEST;
    timer->tr_time.tv_secs=0;timer->tr_time.tv_micro=20000;
    DoIO(&timer->tr_node);
}
static int await_state(struct timerequest *timer,unsigned state)
{
    uint64_t until=AROSAppleSMPTimeUS()+3000000U;
    while(AROSAppleSMPTimeUS()<until) {
        if(__atomic_load_n(&workers[0].state,__ATOMIC_ACQUIRE)==state &&
           __atomic_load_n(&workers[1].state,__ATOMIC_ACQUIRE)==state) return 1;
        pause_sample(timer);
    }
    return 0;
}
/* Emit each proof record through the shared formatter in one call. Raw
 * character writes can interleave with another task's driver diagnostics.
 * Keep records below the formatter's bounded 512-byte SMP chunk. */
static int scheduler_load_proof(struct timerequest *timer);
static int memory_work_proof(struct timerequest *timer);
static int timer_work_proof(struct timerequest *timer);
static int boot_timeslice_proof(struct timerequest *timer);

int AROSAppleSMPWorkProof(struct timerequest *timer)
{
    uint32_t online=AROSAppleSMPOnlineMask();
    if(!timer || __builtin_popcount(online)<2) return 0;
    workers[0].cpu=AROSCPUMonitorTopology()->boot_cpu;
    workers[1].cpu=__builtin_ctz(online & ~(1U<<workers[0].cpu));
    for(unsigned i=0;i<2;i++) {
        void *mask=KrnAllocCPUMask();
        if(!mask) return 0;
        KrnGetCPUMask(workers[i].cpu,mask);
        workers[i].task=NewCreateTask(TASKTAG_NAME,(IPTR)names[i],
            TASKTAG_PRI,1,TASKTAG_PC,(IPTR)worker,TASKTAG_ARG1,(IPTR)&workers[i],
            TASKTAG_AFFINITY,(IPTR)mask,TASKTAG_STACKSIZE,32768,TAG_DONE);
        if(!workers[i].task) return 0;
    }
    if(!await_state(timer,1)) return 0;
    for(unsigned phase=0;phase<2;phase++) {
        uint64_t hz,now;
        __asm__ volatile("mrs %0,cntfrq_el0; isb; mrs %1,cntvct_el0":"=r"(hz),"=r"(now));
        for(unsigned i=0;i<2;i++) {
            workers[i].until=now+hz;
            __atomic_store_n(&workers[i].state,0,__ATOMIC_RELEASE);
            __atomic_store_n(&workers[i].command,1,__ATOMIC_RELEASE);
        }
        /* Start AP first: starting the higher-priority BSP task preempts us. */
        Signal(workers[1].task,SIGF_SINGLE);
        Signal(workers[0].task,SIGF_SINGLE);
        if(!await_state(timer,2)) return 0;
        uint64_t begin=workers[0].start>workers[1].start?workers[0].start:workers[1].start;
        uint64_t end=workers[0].end<workers[1].end?workers[0].end:workers[1].end;
        if(end<=begin || end-begin<hz/2) return 0;
        for(unsigned i=0;i<2;i++) {
            uint64_t seed=UINT64_C(0x12345678abcdef00)+workers[i].cpu;
            if(workers[i].ran_before!=workers[i].cpu || workers[i].ran_after!=workers[i].cpu ||
                !workers[i].count || workers[i].gpr!=seed || workers[i].simd!=seed) return 0;
        }
        for(unsigned i=0;i<2;i++) {
            bug("[SMP] task cpu=0x%016llx phase=0x%016llx start=0x%016llx end=0x%016llx iterations=0x%016llx\n",
                (unsigned long long)workers[i].ran_before, (unsigned long long)phase,
                (unsigned long long)workers[i].start, (unsigned long long)workers[i].end,
                (unsigned long long)workers[i].count);
        }
        uint64_t irq_delta=workers[1].ap_ticks_after-workers[1].ap_ticks_before;
        if(irq_delta<2) return 0;
        bug("[SMP] AP compute timer ticks phase=0x%016llx ticks=0x%016llx\n",
            (unsigned long long)phase, (unsigned long long)irq_delta);
        bug(phase?"[SMP] ordinary task wake + concurrent compute PASS\n":
                  "[SMP] ordinary task concurrent compute + " PROOF_REGISTERS " PASS\n");
        uint64_t counts[2]={workers[0].count,workers[1].count};
        for(unsigned i=0;i<5;i++) pause_sample(timer);
        if(workers[0].count!=counts[0] || workers[1].count!=counts[1]) return 0;
    }
    for(unsigned i=0;i<2;i++) {
        __atomic_store_n(&workers[i].command,3,__ATOMIC_RELEASE);
        Signal(workers[i].task,SIGF_SINGLE);
    }
    if(!await_state(timer,3)) return 0;
    uint64_t until=AROSAppleSMPTimeUS()+3000000U;
    while(AROSAppleSMPTimeUS()<until) {
        if(!FindTask((STRPTR)names[0]) && !FindTask((STRPTR)names[1])) {
            bug("[SMP] AP timer ticks=0x%016llx\n", (unsigned long long)AROSAppleSMPTimerTicks());
            bug("[SMP] ordinary task teardown PASS\n");
            return scheduler_load_proof(timer);
        }
        pause_sample(timer);
    }
    return 0;
}

/* More runnable tasks than CPUs: let Exec choose placement and preempt them.
 * Keep the measuring task above the workers, like an interactive service.
 * This measures scheduler wake latency, not end-to-end physical input. */
#define LOAD_WORKERS_MAX 16
static unsigned load_count;
struct LoadWorker {
    struct Task *task;
    uint32_t state,cpus;
    uint64_t until,iterations;
    unsigned id;
};
static struct LoadWorker load_workers[LOAD_WORKERS_MAX];
static const char *load_names[LOAD_WORKERS_MAX]={
    "SMP load 0","SMP load 1","SMP load 2","SMP load 3","SMP load 4","SMP load 5","SMP load 6","SMP load 7","SMP load 8","SMP load 9","SMP load 10","SMP load 11","SMP load 12","SMP load 13","SMP load 14","SMP load 15"
};
static void load_worker(struct LoadWorker *w)
{
    __atomic_store_n(&w->state,1,__ATOMIC_RELEASE);
    Wait(SIGF_SINGLE);
    uint64_t now,count=0;
    uint32_t cpus=0;
    do {
        unsigned cpu=KrnGetCPUNumber();
        if(cpu<32) cpus|=1U<<cpu;
        /* Bounded arithmetic between samples; no voluntary yield. */
        for(unsigned i=0;i<4096;i++)
            __asm__ volatile("add %0,%0,#1":"+r"(count));
        __asm__ volatile("isb; mrs %0,cntvct_el0":"=r"(now));
    } while(now<w->until);
    w->cpus=cpus;w->iterations=count;
    __atomic_store_n(&w->state,2,__ATOMIC_RELEASE);
}
static int scheduler_load_proof(struct timerequest *timer)
{
    load_count=2U*__builtin_popcount(AROSAppleSMPOnlineMask());
    if(load_count>LOAD_WORKERS_MAX) return 0;
    unsigned ready,done; uint64_t limit=AROSAppleSMPTimeUS()+3000000U;
    for(unsigned i=0;i<load_count;i++) {
        load_workers[i].id=i;
        load_workers[i].task=NewCreateTask(TASKTAG_NAME,(IPTR)load_names[i],
            TASKTAG_PRI,0,TASKTAG_PC,(IPTR)load_worker,
            TASKTAG_ARG1,(IPTR)&load_workers[i],TASKTAG_AFFINITY,TASKAFFINITY_ANY,
            TASKTAG_STACKSIZE,32768,TAG_DONE);
        if(!load_workers[i].task) return 0;
    }
    do {
        ready=0;
        for(unsigned i=0;i<load_count;i++)
            ready+=__atomic_load_n(&load_workers[i].state,__ATOMIC_ACQUIRE)==1;
        if(ready==load_count) break;
        pause_sample(timer);
    } while(AROSAppleSMPTimeUS()<limit);
    if(ready!=load_count) return 0;
    struct Task *observer=FindTask(NULL);
    BYTE oldpri=SetTaskPri(observer,5);
    uint64_t hz,now,max_wake=0,samples=0;
    __asm__ volatile("mrs %0,cntfrq_el0; isb; mrs %1,cntvct_el0":"=r"(hz),"=r"(now));
    for(unsigned i=0;i<load_count;i++) load_workers[i].until=now+hz;
    for(unsigned i=0;i<load_count;i++) Signal(load_workers[i].task,SIGF_SINGLE);
    limit=AROSAppleSMPTimeUS()+3000000U;
    do {
        uint64_t start=AROSAppleSMPTimeUS();
        pause_sample(timer);
        uint64_t elapsed=AROSAppleSMPTimeUS()-start;
        if(elapsed>max_wake) max_wake=elapsed;
        samples++;
        done=0;
        for(unsigned i=0;i<load_count;i++)
            done+=__atomic_load_n(&load_workers[i].state,__ATOMIC_ACQUIRE)==2;
    } while(done!=load_count && AROSAppleSMPTimeUS()<limit);
    SetTaskPri(observer,oldpri);
    if(done!=load_count) return 0;
    uint32_t seen=0,online=AROSAppleSMPOnlineMask();
    for(unsigned i=0;i<load_count;i++) {
        const struct LoadWorker *w=&load_workers[i];
        bug("[SMP] unpinned worker=0x%016llx cpus=0x%016llx iterations=0x%016llx\n",
            (unsigned long long)i, (unsigned long long)w->cpus,
            (unsigned long long)w->iterations);
        if(!w->iterations || !w->cpus || (w->cpus&~online)) return 0;
        seen|=w->cpus;
    }
    bug("[SMP] load wake samples=0x%016llx max_us=0x%016llx\n",
        (unsigned long long)samples, (unsigned long long)max_wake);
    if(seen!=online || samples<5 || max_wake>100000U) return 0;
    limit=AROSAppleSMPTimeUS()+3000000U;
    do {
        done=0;
        for(unsigned i=0;i<load_count;i++) done+=FindTask((STRPTR)load_names[i])==NULL;
        if(done==load_count) {
            bug("[SMP] unpinned load + BSP wake latency + teardown PASS\n");
            return memory_work_proof(timer);
        }
        pause_sample(timer);
    } while(AROSAppleSMPTimeUS()<limit);
    return 0;
}

/* Each CPU writes and executes its own freshly allocated page; freeing it
 * allows a different CPU to reuse the address in a later iteration. No CPU
 * is allowed to execute a page while another changes its permissions. */
#define MEMORY_CPUS 8
#define MEMORY_CYCLES 64
struct MemoryWorker {
    struct Task *task;
    uint32_t state,cycles,failed,cpu;
    uint64_t begin,end;
};
static struct MemoryWorker memory_workers[MEMORY_CPUS];
static const char *memory_names[MEMORY_CPUS]={
    "SMP memory 0","SMP memory 1","SMP memory 2","SMP memory 3",
    "SMP memory 4","SMP memory 5","SMP memory 6","SMP memory 7"
};
static uint32_t memory_live,memory_peak;
static void memory_worker(struct MemoryWorker *w)
{
    __atomic_store_n(&w->state,1,__ATOMIC_RELEASE);
    Wait(SIGF_SINGLE);
    __asm__ volatile("isb; mrs %0,cntvct_el0":"=r"(w->begin));
    for(unsigned n=0;n<MEMORY_CYCLES;n++) {
        uint32_t value=1+w->cpu*MEMORY_CYCLES+n;
        volatile uint32_t *code=KrnAllocPages(NULL,AROS_APPLE_PAGE_SIZE,MEMF_CLEAR);
        if(!code) { w->failed=1; break; }
        uint32_t live=__atomic_add_fetch(&memory_live,1,__ATOMIC_ACQ_REL);
        uint32_t peak=__atomic_load_n(&memory_peak,__ATOMIC_RELAXED);
        while(peak<live && !__atomic_compare_exchange_n(&memory_peak,&peak,live,0,
              __ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
        /* First allocation is held until all workers own a page. */
        if(n==0) {
            __atomic_store_n(&w->state,2,__ATOMIC_RELEASE);
            /* Let the BSP coordinator run even without a pending timer:
             * a spin barrier can starve an equal-priority coordinator. */
            Wait(SIGF_SINGLE);
        }
        if(KrnGetCPUNumber()!=w->cpu || code[0]!=0 || code[1]!=0) w->failed=3;
        /* Exercise the already-mapped path concurrently with protection. */
        if(!w->failed && !KrnMapGlobal((void *)code,(void *)code,AROS_APPLE_PAGE_SIZE,
                        MAP_CacheInhibit|MAP_Writable)) w->failed=4;
        if(!w->failed) {
            code[0]=UINT32_C(0x52800000)|(value<<5); /* mov w0,#value */
            code[1]=UINT32_C(0xd65f03c0);             /* ret */
            /* The public void API cannot report a failed RX transition.
             * Never turn a resource/protection failure into an instruction
             * abort: retain the failing proof and perform normal cleanup. */
            if(!AROSApplePageProtect((void *)code,AROS_APPLE_PAGE_SIZE,
                                    MAP_Readable|MAP_Executable)) w->failed=6;
            else {
                unsigned result=((unsigned (*)(void))(uintptr_t)code)();
                if(result!=value) w->failed=5;
            }
        }
        __atomic_sub_fetch(&memory_live,1,__ATOMIC_ACQ_REL);
        KrnFreePages((void *)code,AROS_APPLE_PAGE_SIZE);
        if(w->failed) break;
        w->cycles++;
    }
    __asm__ volatile("isb; mrs %0,cntvct_el0":"=r"(w->end));
    __atomic_store_n(&w->state,3,__ATOMIC_RELEASE);
}
static int memory_work_proof(struct timerequest *timer)
{
    struct AROSApplePageAllocatorStats before,after;
    uint32_t online=AROSAppleSMPOnlineMask();
    unsigned count=__builtin_popcount(online),ready;
    if(count>MEMORY_CPUS) return 0;
    AROSApplePageAllocatorGetStats(&before);
    for(unsigned cpu=0;cpu<MEMORY_CPUS;cpu++) if(online&(1U<<cpu)) {
        struct MemoryWorker *w=&memory_workers[cpu]; w->cpu=cpu;
        void *mask=KrnAllocCPUMask(); if(!mask) return 0;
        KrnGetCPUMask(cpu,mask);
        w->task=NewCreateTask(TASKTAG_NAME,(IPTR)memory_names[cpu],TASKTAG_PRI,0,
            TASKTAG_PC,(IPTR)memory_worker,TASKTAG_ARG1,(IPTR)w,
            TASKTAG_AFFINITY,(IPTR)mask,TASKTAG_STACKSIZE,32768,TAG_DONE);
        if(!w->task) return 0;
    }
    uint64_t deadline=AROSAppleSMPTimeUS()+3000000U;
    do {
        ready=0;
        for(unsigned cpu=0;cpu<MEMORY_CPUS;cpu++) if(online&(1U<<cpu))
            ready+=__atomic_load_n(&memory_workers[cpu].state,__ATOMIC_ACQUIRE)==1;
        if(ready==count) break;
        pause_sample(timer);
    } while(AROSAppleSMPTimeUS()<deadline);
    if(ready!=count) return 0;
    for(unsigned cpu=MEMORY_CPUS;cpu-->0;) if(online&(1U<<cpu)) Signal(memory_workers[cpu].task,SIGF_SINGLE);
    deadline=AROSAppleSMPTimeUS()+3000000U;
    do {
        ready=0;
        for(unsigned cpu=0;cpu<MEMORY_CPUS;cpu++) if(online&(1U<<cpu))
            ready+=__atomic_load_n(&memory_workers[cpu].state,__ATOMIC_ACQUIRE)==2;
        if(ready==count) break;
        pause_sample(timer);
    } while(AROSAppleSMPTimeUS()<deadline);
    for(unsigned cpu=MEMORY_CPUS;cpu-->0;) if(online&(1U<<cpu))
        if(__atomic_load_n(&memory_workers[cpu].state,__ATOMIC_ACQUIRE)==2)
            Signal(memory_workers[cpu].task,SIGF_SINGLE);
    if(ready!=count) {
        bug("[SMP] memory barrier ready=0x%016llx\n", (unsigned long long)ready);
        for(unsigned cpu=0;cpu<MEMORY_CPUS;cpu++) if(online&(1U<<cpu)) {
            bug("[SMP] memory pending cpu=0x%016llx state=0x%016llx\n",
                (unsigned long long)cpu,
                (unsigned long long)__atomic_load_n(&memory_workers[cpu].state,__ATOMIC_ACQUIRE));
        }
        return 0;
    }
    deadline=AROSAppleSMPTimeUS()+3000000U;
    do {
        ready=0;
        for(unsigned cpu=0;cpu<MEMORY_CPUS;cpu++) if(online&(1U<<cpu))
            ready+=__atomic_load_n(&memory_workers[cpu].state,__ATOMIC_ACQUIRE)==3;
        if(ready==count) break;
        pause_sample(timer);
    } while(AROSAppleSMPTimeUS()<deadline);
    if(ready!=count) return 0;
    for(unsigned cpu=0;cpu<MEMORY_CPUS;cpu++) if(online&(1U<<cpu)) {
        const struct MemoryWorker *w=&memory_workers[cpu];
        bug("[SMP] memory cpu=0x%016llx cycles=0x%016llx error=0x%016llx begin=0x%016llx end=0x%016llx\n",
            (unsigned long long)cpu, (unsigned long long)w->cycles,
            (unsigned long long)w->failed, (unsigned long long)w->begin,
            (unsigned long long)w->end);
        if(w->failed || w->cycles!=MEMORY_CYCLES) return 0;
    }
    deadline=AROSAppleSMPTimeUS()+3000000U;
    do {
        ready=0;
        for(unsigned cpu=0;cpu<MEMORY_CPUS;cpu++) if(online&(1U<<cpu))
            ready+=FindTask((STRPTR)memory_names[cpu])==NULL;
        if(ready==count) break;
        pause_sample(timer);
    } while(AROSAppleSMPTimeUS()<deadline);
    if(ready!=count) return 0;
    AROSApplePageAllocatorGetStats(&after);
    bug("[SMP] memory peak=0x%016llx live=0x%016llx pages_before=0x%016llx pages_after=0x%016llx tables_before=0x%016llx tables_after=0x%016llx\n",
        (unsigned long long)memory_peak, (unsigned long long)memory_live,
        (unsigned long long)before.pages_in_use, (unsigned long long)after.pages_in_use,
        (unsigned long long)before.internal_pages_in_use, (unsigned long long)after.internal_pages_in_use);
    if(memory_live || memory_peak!=count || before.pages_in_use!=after.pages_in_use ||
       before.internal_pages_in_use!=after.internal_pages_in_use) return 0;
    bug("[SMP] concurrent page W^X + execution + reclamation PASS\n");
    return boot_timeslice_proof(timer);
}

static int boot_timeslice_proof(struct timerequest *timer)
{
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    (void)timer; return 1;
#else
    /* Reuse the torn-down worker slots, pinning both to the BSP. They do
     * not voluntarily yield while computing; one long timer request leaves
     * timeslicing to the scheduler heartbeat, not our observation loop. */
    unsigned boot=AROSCPUMonitorTopology()->boot_cpu;
    for(unsigned i=0;i<2;i++) {
        workers[i].state=0;workers[i].command=0;workers[i].cpu=boot;
        void *mask=KrnAllocCPUMask(); if(!mask) return 0;
        KrnGetCPUMask(boot,mask);
        workers[i].task=NewCreateTask(TASKTAG_NAME,(IPTR)names[i],TASKTAG_PRI,1,
            TASKTAG_PC,(IPTR)worker,TASKTAG_ARG1,(IPTR)&workers[i],
            TASKTAG_AFFINITY,(IPTR)mask,TASKTAG_STACKSIZE,32768,TAG_DONE);
        if(!workers[i].task) return 0;
    }
    if(!await_state(timer,1)) return 0;
    struct Task *observer=FindTask(NULL);
    BYTE oldpri=SetTaskPri(observer,5);
    uint64_t now,hz,before=AROSAppleSMPBootTicks();
    __asm__ volatile("mrs %0,cntfrq_el0; isb; mrs %1,cntvct_el0":"=r"(hz),"=r"(now));
    for(unsigned i=0;i<2;i++) {
        workers[i].until=now+hz/2;
        __atomic_store_n(&workers[i].state,0,__ATOMIC_RELEASE);
        __atomic_store_n(&workers[i].command,1,__ATOMIC_RELEASE);
        Signal(workers[i].task,SIGF_SINGLE);
    }
    timer->tr_node.io_Command=TR_ADDREQUEST;
    timer->tr_time.tv_secs=1;timer->tr_time.tv_micro=0;
    DoIO(&timer->tr_node);
    uint64_t ticks=AROSAppleSMPBootTicks()-before;
    SetTaskPri(observer,oldpri);
    if(!await_state(timer,2)) return 0;
    uint64_t begin=workers[0].start>workers[1].start?workers[0].start:workers[1].start;
    uint64_t end=workers[0].end<workers[1].end?workers[0].end:workers[1].end;
    uint64_t seed=UINT64_C(0x12345678abcdef00)+boot;
    for(unsigned i=0;i<2;i++) {
        bug("[SMP] BSP worker=0x%016llx cpu=0x%016llx start=0x%016llx end=0x%016llx iterations=0x%016llx\n",
            (unsigned long long)i, (unsigned long long)workers[i].ran_before,
            (unsigned long long)workers[i].start, (unsigned long long)workers[i].end,
            (unsigned long long)workers[i].count);
        if(workers[i].ran_before!=boot || workers[i].ran_after!=boot || !workers[i].count ||
           workers[i].gpr!=seed || workers[i].simd!=seed) return 0;
    }
    bug("[SMP] BSP heartbeat ticks=0x%016llx hz=0x%016llx\n",
        (unsigned long long)ticks, (unsigned long long)hz);
    if(ticks<40 || end<=begin || end-begin<hz/4 || begin-now>hz/10) return 0;
    for(unsigned i=0;i<2;i++) {
        __atomic_store_n(&workers[i].command,3,__ATOMIC_RELEASE);
        Signal(workers[i].task,SIGF_SINGLE);
    }
    if(!await_state(timer,3)) return 0;
    uint64_t deadline=AROSAppleSMPTimeUS()+3000000U;
    do {
        if(!FindTask((STRPTR)names[0]) && !FindTask((STRPTR)names[1])) {
            bug("[SMP] BSP equal-priority timeslicing + registers + teardown PASS\n");
            return timer_work_proof(timer);
        }
        pause_sample(timer);
    } while(AROSAppleSMPTimeUS()<deadline);
    return 0;
#endif
}


#define TIMER_CYCLES 16
struct TimerWorker {
    struct Task *task;
    struct MsgPort port;
    struct timerequest request;
    uint32_t state, cpu, completed, aborted, error;
    uint64_t max_us;
};
static struct TimerWorker timer_workers[8];
static const char *timer_names[8] = {
    "SMP timer 0", "SMP timer 1", "SMP timer 2", "SMP timer 3",
    "SMP timer 4", "SMP timer 5", "SMP timer 6", "SMP timer 7"
};
static void timer_worker(struct TimerWorker *w)
{
    struct Device *TimerBase = w->request.tr_node.io_Device;
    BYTE bit = AllocSignal(-1);
    if (bit < 0) { w->error = 1; __atomic_store_n(&w->state, 2, __ATOMIC_RELEASE); return; }
    NEWLIST(&w->port.mp_MsgList);
    w->port.mp_Node.ln_Type = NT_MSGPORT;
    w->port.mp_Flags = PA_SIGNAL;
    w->port.mp_SigBit = bit;
    w->port.mp_SigTask = FindTask(NULL);
    w->request.tr_node.io_Message.mn_ReplyPort = &w->port;
    __atomic_store_n(&w->state, 1, __ATOMIC_RELEASE);
    Wait(SIGF_SINGLE);
    for (unsigned n = 0; n < TIMER_CYCLES; ++n) {
        struct timeval before, after;
        struct EClockVal ec;
        GetUpTime(&before);
        if (!ReadEClock(&ec)) { w->error = 2; break; }
        uint64_t start = AROSAppleSMPTimeUS();
        w->request.tr_node.io_Command = TR_ADDREQUEST;
        w->request.tr_time.tv_secs = 0;
        w->request.tr_time.tv_micro = 2000;
        if (DoIO(&w->request.tr_node)) { w->error = 3; break; }
        uint64_t elapsed = AROSAppleSMPTimeUS() - start;
        if (elapsed > w->max_us) w->max_us = elapsed;
        GetUpTime(&after);
        if (KrnGetCPUNumber() != w->cpu || elapsed < 2000 || elapsed > 100000 ||
            after.tv_secs < before.tv_secs ||
            (after.tv_secs == before.tv_secs && after.tv_micro < before.tv_micro)) {
            w->error = 4; break;
        }
        w->completed++;
        w->request.tr_time.tv_secs = 1;
        w->request.tr_time.tv_micro = 0;
        SendIO(&w->request.tr_node);
        AbortIO(&w->request.tr_node);
        if (WaitIO(&w->request.tr_node) != IOERR_ABORTED || GetMsg(&w->port)) {
            w->error = 5; break;
        }
        w->aborted++;
        GetSysTime(&after);
        if (after.tv_micro >= 1000000) { w->error = 6; break; }
    }
    FreeSignal(bit);
    __atomic_store_n(&w->state, 2, __ATOMIC_RELEASE);
}
static int timer_work_proof(struct timerequest *timer)
{
    uint32_t online = AROSAppleSMPOnlineMask();
    unsigned count = __builtin_popcount(online), ready;
    if (online & ~255U) return 0;
    for (unsigned cpu = 0; cpu < 8; ++cpu) if (online & (1U << cpu)) {
        struct TimerWorker *w = &timer_workers[cpu];
        w->cpu = cpu;
        w->request.tr_node.io_Message.mn_Length = sizeof(w->request);
        if (OpenDevice("timer.device", UNIT_MICROHZ, &w->request.tr_node, 0)) return 0;
        void *mask = KrnAllocCPUMask(); if (!mask) return 0;
        KrnGetCPUMask(cpu, mask);
        w->task = NewCreateTask(TASKTAG_NAME, (IPTR)timer_names[cpu], TASKTAG_PRI, 0,
            TASKTAG_PC, (IPTR)timer_worker, TASKTAG_ARG1, (IPTR)w,
            TASKTAG_AFFINITY, (IPTR)mask, TASKTAG_STACKSIZE, 32768, TAG_DONE);
        if (!w->task) return 0;
    }
    uint64_t deadline = AROSAppleSMPTimeUS() + 3000000;
    do {
        ready = 0;
        for (unsigned cpu = 0; cpu < 8; ++cpu) if (online & (1U << cpu))
            ready += __atomic_load_n(&timer_workers[cpu].state, __ATOMIC_ACQUIRE) == 1;
        if (ready == count) break;
        pause_sample(timer);
    } while (AROSAppleSMPTimeUS() < deadline);
    if (ready != count) return 0;
    for (unsigned cpu = 8; cpu-- > 0;) if (online & (1U << cpu))
        Signal(timer_workers[cpu].task, SIGF_SINGLE);
    deadline = AROSAppleSMPTimeUS() + 3000000;
    do {
        ready = 0;
        for (unsigned cpu = 0; cpu < 8; ++cpu) if (online & (1U << cpu))
            ready += __atomic_load_n(&timer_workers[cpu].state, __ATOMIC_ACQUIRE) == 2;
        if (ready == count) break;
        pause_sample(timer);
    } while (AROSAppleSMPTimeUS() < deadline);
    for (unsigned cpu = 0; cpu < 8; ++cpu) if (online & (1U << cpu)) {
        struct TimerWorker *w = &timer_workers[cpu];
        bug("[SMP] timer cpu=0x%016llx completed=0x%016llx aborted=0x%016llx error=0x%016llx max_us=0x%016llx\n",
            (unsigned long long)cpu, (unsigned long long)w->completed,
            (unsigned long long)w->aborted, (unsigned long long)w->error,
            (unsigned long long)w->max_us);
    }
    if (ready != count) return 0;
    deadline = AROSAppleSMPTimeUS() + 3000000;
    do {
        ready = 0;
        for (unsigned cpu = 0; cpu < 8; ++cpu) if (online & (1U << cpu))
            ready += FindTask((STRPTR)timer_names[cpu]) == NULL;
        if (ready == count) break;
        pause_sample(timer);
    } while (AROSAppleSMPTimeUS() < deadline);
    if (ready != count) return 0;
    for (unsigned cpu = 0; cpu < 8; ++cpu) if (online & (1U << cpu)) {
        struct TimerWorker *w = &timer_workers[cpu];
        CloseDevice(&w->request.tr_node);
        if (w->error || w->completed != TIMER_CYCLES || w->aborted != TIMER_CYCLES) return 0;
    }
    bug("[SMP] cross-CPU timer requests + abort + clocks + teardown PASS\n");
    return 1;
}
