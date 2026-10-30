/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/kernel.h>
#include <exec/tasks.h>
#include <aros/debug.h>
#include <stdint.h>
APTR KernelBase;
#define CHECK(x) do { if (!(x)) { bug("[RETIRE] FAIL line=%d\n",__LINE__); for (;;) Wait(0); } } while (0)
struct Work { unsigned mode,ready,cpu; uint64_t count; };
static struct Work work;
static struct Task *parent;
static ULONG completion;
static unsigned finished,removed;
static void victim(struct Work *w)
{
    w->cpu=KrnGetCPUNumber();
    __atomic_store_n(&w->ready,1,__ATOMIC_RELEASE);
    for (;;) {
        __atomic_fetch_add(&w->count,1,__ATOMIC_RELAXED);
        if (w->mode==1) KrnSwitch();
        else if (w->mode==2) Wait(0);
    }
}
static struct Task *start(unsigned cpu, const char *name, void *entry, void *arg)
{
    void *mask=KrnAllocCPUMask();CHECK(mask);
    KrnGetCPUMask(cpu,mask);
    struct Task *task=NewCreateTask(TASKTAG_NAME,(IPTR)name,TASKTAG_PRI,1,
        TASKTAG_PC,(IPTR)entry,TASKTAG_ARG1,(IPTR)arg,TASKTAG_AFFINITY,(IPTR)mask,
        TASKTAG_STACKSIZE,65536,TAG_DONE);
    CHECK(task);return task;
}
static void controller(void)
{
    CHECK(KrnGetCPUNumber()==0);
    for (unsigned cycle=0;cycle<24;++cycle) {
        work=(struct Work){.mode=cycle%3};
        struct Task *task=start(1,"remote retirement victim",victim,&work);
        while (!__atomic_load_n(&work.ready,__ATOMIC_ACQUIRE) ||
               !__atomic_load_n(&work.count,__ATOMIC_RELAXED)) KrnSwitch();
        CHECK(work.cpu==1 && KrnGetCPUNumber()==0);
        RemTask(task); /* Must stop CPU 1 before freeing task/context/stack. */
        uint64_t stopped=__atomic_load_n(&work.count,__ATOMIC_RELAXED);
        for (unsigned sample=0;sample<10000;++sample) {
            CHECK(__atomic_load_n(&work.count,__ATOMIC_RELAXED)==stopped);
            __asm__ volatile("yield" ::: "memory");
        }
        CHECK(FindTask((CONST_STRPTR)"remote retirement victim")==NULL);
        ++removed;
    }
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);
    Signal(parent,completion);
    for (;;) Wait(0);
}
int main(void)
{
    KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");CHECK(KernelBase);
    CHECK(KrnGetCPUCount()==2);
    parent=FindTask(NULL);BYTE bit=AllocSignal(-1);CHECK(bit>=0);completion=1UL<<bit;
    struct Task *task=start(0,"remote retirement controller",controller,NULL);
    while (!__atomic_load_n(&finished,__ATOMIC_ACQUIRE)) Wait(completion);
    RemTask(task);FreeSignal(bit);
    CHECK(removed==24 && FindTask((CONST_STRPTR)"remote retirement controller")==NULL);
    bug("[RETIRE] PASS cpus=2 removed=24 compute=8 switching=8 waiting=8 controller_removed=1\n");
    return 0;
}
