/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1. */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
typedef void *APTR;
typedef uintptr_t BPTR;
typedef uintptr_t IPTR;
typedef uint32_t ULONG;
#define BNULL ((BPTR)0)
#define MKBADDR(p) ((BPTR)(p))
#define MEMF_ANY 0
/* @MEMFLAGS@ */
#define MAP_Readable 0x100
#define MAP_Writable 0x200
#define MAP_Executable 0x400
#define CACRF_ClearI 0x800
#define CACRF_ClearD 0x8
#define D(x)
#define AROS_USERFUNC_INIT
#define AROS_USERFUNC_EXIT
#define AROS_UFHA(t,n,r) t n
#define AROS_UFH3(t,n,a,b,c) t n(a,b,c)
struct ExecBase { struct { unsigned lib_Version; } LibNode; } exec_base = {{50}};
struct KernelBase { int unused; } kernel_base;
struct FullJumpVec { uint32_t instruction[2]; APTR target; } __attribute__((packed));
#define __AROS_SET_FULLJMP(code, fn) do { (code)->instruction[0]=0x58000050; (code)->instruction[1]=0xd61f0200; (code)->target=(fn); } while (0)
static int have_kernel, borrowed, allocation_failure;
static unsigned pool_calls, page_calls, pool_frees, page_frees, cache_calls, protection_calls;
static size_t page_size;
static APTR allocation;
static ULONG allocation_size, request_flags;
static int owner, is_exec_range;
static void check(int yes,const char *name) { if(!yes) { fprintf(stderr,"FAIL: %s\n",name); exit(1); } }
static struct KernelBase *OpenResource(const char *name) { check(!strcmp(name,"kernel.resource"),"resource name"); return have_kernel?&kernel_base:NULL; }
static APTR new_memory(ULONG size, int kind) {
    check(!allocation,"single live allocation"); allocation_size=size; owner=kind;
    allocation=mmap(NULL,page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    check(allocation!=MAP_FAILED,"host mapping"); return allocation;
}
static APTR AllocMem(ULONG size, ULONG flags) { (void)flags; ++pool_calls; is_exec_range=1; return new_memory(size,1); }
static APTR KrnAllocPages(APTR preferred, ULONG size, ULONG flags) {
    check(!preferred,"no preferred address"); ++page_calls; request_flags=flags;
    if(allocation_failure) return NULL;
    /* A code mapping has its own virtual address. Physical arena growth
       borrows pool addresses; TypeOfMem still reports that enclosing pool. */
    is_exec_range=borrowed && !(flags & MEMF_EXECUTABLE);
    return new_memory(size,2);
}
static ULONG TypeOfMem(APTR memory) { check(memory==allocation,"query allocation base"); return is_exec_range?0x507:0; }
static void FreeMem(APTR memory, ULONG size) {
    check(memory==allocation && size==allocation_size,"pool free extent"); ++pool_frees;
    if(owner!=1) {
        fputs("WRONG_OWNER: pool free on protected page\n",stderr); fflush(stderr);
        *(volatile uintptr_t*)memory=0; /* The real free list writes here. */
        check(0,"wrong owner did not fault");
    }
    check(!munmap(memory,page_size),"pool unmap"); allocation=NULL;
}
static void KrnFreePages(APTR memory, ULONG size) {
    check(memory==allocation && size==allocation_size && owner==2,"page free owner and extent");
    ++page_frees; check(!munmap(memory,page_size),"page unmap"); allocation=NULL;
}
static void CacheClearE(APTR memory, ULONG size, ULONG flags) {
    (void)memory; (void)size; check(flags==(CACRF_ClearI|CACRF_ClearD),"cache flags"); ++cache_calls;
}
static void KrnSetProtection(APTR memory, ULONG size, ULONG flags) {
    check(memory==allocation && size==allocation_size && owner==2,"protect dedicated allocation");
    check(flags==(MAP_Readable|MAP_Executable),"final RX flags"); ++protection_calls;
    /* Enforce write protection with the host MMU; execution itself is a guest test. */
    check(!mprotect(memory,page_size,PROT_READ),"read-only host mapping");
}
/* @CREATE@ */
/* @FREE@ */
static void target(void) {}
int main(int argc,char **argv) {
    struct rlimit lim={0,0}; setrlimit(RLIMIT_CORE,&lim);
    check(argc==2,"scenario argument"); page_size=(size_t)sysconf(_SC_PAGESIZE);
    have_kernel=strcmp(argv[1],"bootstrap")!=0; borrowed=!strcmp(argv[1],"borrowed");
    allocation_failure=!strcmp(argv[1],"failure");
    BPTR segment=__CreateSegList((APTR)target,&exec_base);
#if defined(__AROSPLATFORM_WXSEG__)
    if(allocation_failure) {
        check(!segment && page_calls==1 && pool_calls==0 && !protection_calls,"page failure stays closed");
        puts("PASS allocation failure"); return 0;
    }
#endif
    check(segment && allocation,"created segment");
    struct phony_segment *header=allocation;
    check(header->Size==sizeof(*header)+sizeof(struct FullJumpVec) && !header->Next,"segment header");
    struct FullJumpVec *code=(void*)((unsigned char*)allocation+sizeof(*header));
    check(code->target==(APTR)target && code->instruction[0]==0x58000050 && code->instruction[1]==0xd61f0200,"jump target");
    check(cache_calls==1,"cache publication");
    FreeFunc(allocation,header->Size,&exec_base);
    check(!allocation,"released allocation");
#if defined(__AROSPLATFORM_WXSEG__)
    if(have_kernel) check(page_calls==1 && page_frees==1 && !pool_calls && !pool_frees && protection_calls==1,"page lifetime");
    else check(pool_calls==1 && pool_frees==1 && !page_calls && !protection_calls,"bootstrap lifetime");
#else
    check(pool_calls==1 && pool_frees==1 && !page_calls && !protection_calls,"ordinary executable pool lifetime");
#endif
    printf("PASS %s flags=%x\n",argv[1],request_flags); return 0;
}
