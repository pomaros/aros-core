/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "mapping.h"
static struct KrnMappingObject objects[2];
static struct KrnMappingPage pages[2][3];
static struct KrnMappingRequest batch[6];
static unsigned installs,fail_at;
static int install(void *context,uintptr_t va,uintptr_t pa,unsigned protection)
{
    (void)context;(void)va;(void)pa;
    ++installs;
    assert(!(protection&KMAP_WRITE));
    return installs!=fail_at;
}
static const struct KrnMappingOps ops={NULL,install,NULL};
static void reset(void)
{
    installs=fail_at=0;
    for (size_t m=0;m<2;++m) {
        assert(KrnMappingInitialize(&objects[m],0x100000+m*0x10000,3*4096,
            4096,0,KMAP_READ|KMAP_WRITE,1,NULL,pages[m],3));
        for (size_t p=0;p<3;++p) {
            pages[m][p].state=KMAP_DIRTY;
            pages[m][p].physical=0x400000+(m*3+p)*4096;
            pages[m][p].generation=11;
            batch[m*3+p]=(struct KrnMappingRequest){&objects[m],p,11};
        }
        objects[m].resident=3;
    }
}
static void assert_all(unsigned state,size_t in_flight)
{
    for (size_t m=0;m<2;++m) {
        assert(objects[m].in_flight==in_flight);
        for (size_t p=0;p<3;++p) assert(pages[m][p].state==state);
    }
}
int main(void)
{
    reset();batch[5].generation=10;
    assert(!KrnMappingBeginWritebackBatch(batch,6,&ops) && !installs);
    assert_all(KMAP_DIRTY,0);
    reset();batch[5]=batch[0];
    assert(!KrnMappingBeginWritebackBatch(batch,6,&ops) && !installs);
    reset();objects[1].shared=0;
    assert(!KrnMappingBeginWritebackBatch(batch,6,&ops) && !installs);
    reset();pages[1][2].generation=UINT64_MAX;batch[5].generation=UINT64_MAX;
    assert(!KrnMappingBeginWritebackBatch(batch,6,&ops) && !installs);
    reset();pages[1][2].state=KMAP_LOADING;
    assert(!KrnMappingBeginWritebackBatch(batch,6,&ops) && !installs);
    /* Every possible failed freeze leaves no in-flight transaction, including
     * failures after crossing into a second object. Generations never rewind. */
    for (unsigned failure=1;failure<=6;++failure) {
        reset();fail_at=failure;
        assert(!KrnMappingBeginWritebackBatch(batch,6,&ops));
        assert_all(KMAP_DIRTY,0);
        for (size_t i=0;i<6;++i)
            assert(pages[i/3][i%3].generation==(i<failure-1 ? 12u : 11u));
        fail_at=0;
        for (size_t i=0;i<6;++i)batch[i].generation=pages[i/3][i%3].generation;
        assert(KrnMappingBeginWritebackBatch(batch,6,&ops));
        assert(KrnMappingEndWritebackBatch(batch,6,9));
        assert_all(KMAP_DIRTY,0);
    }
    reset();assert(KrnMappingBeginWritebackBatch(batch,6,&ops));
    assert(installs==6);assert_all(KMAP_WRITEBACK,3);
    for (size_t i=0;i<6;++i) {
        struct KrnMappingRequest fault;
        assert(KrnMappingFault(batch[i].mapping,batch[i].mapping->start+
            batch[i].page*4096,KMAP_WRITE,1,&fault,&ops)==KMAP_FAULT_WAIT);
        assert(fault.generation==batch[i].generation);
    }
    --batch[5].generation;
    assert(!KrnMappingEndWritebackBatch(batch,6,0));
    assert_all(KMAP_WRITEBACK,3); /* No partial success publication. */
    ++batch[5].generation;
    assert(KrnMappingEndWritebackBatch(batch,6,0));assert_all(KMAP_CLEAN,0);
    assert(!KrnMappingEndWritebackBatch(batch,6,0));
    assert(KrnMappingBeginWritebackBatch(NULL,0,&ops));
    assert(KrnMappingEndWritebackBatch(NULL,0,0));
    puts("writeback batch PASS preflight, freeze rollback, no partial publication, waiting stores");
    return 0;
}
