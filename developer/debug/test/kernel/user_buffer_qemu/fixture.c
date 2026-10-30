/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "user_service.h"
#define PAGE 4096U
#define DATA UINT64_C(0x10001000)
#define TEMP (DATA+3*PAGE)
extern void ProbeFail(const char *);
extern uintptr_t ProbeBacking(unsigned);
extern int ProbeMap(unsigned,uintptr_t,uintptr_t,unsigned);
extern int ProbeUnmap(unsigned,uintptr_t,uintptr_t);
extern unsigned char *ProbeAdmit(unsigned,uintptr_t,uintptr_t,size_t,unsigned);
#define CHECK(x,n) do { if(!(x))ProbeFail(n); } while(0)
static struct KrnBufferRegistry registry;
struct Space {
 unsigned id;
 struct KrnMappingDomain domain;
 struct KrnMappingArena arena;
 struct KrnMappingObject main,gap,temp;
 struct KrnMappingPage mainpages[1],gappages[2],temppages[2];
 struct KrnServiceQueue queue;
 struct KrnMappingOps mapops;
 struct KrnBufferOps bufops;
 uint64_t handle;
};
static struct Space spaces[2];
static unsigned char memory[2][2][PAGE] __attribute__((aligned(PAGE)));
static unsigned copied,refused,cancelled,retired,cross_queue;
static int install(void *context,uintptr_t va,uintptr_t pa,unsigned permissions)
{ struct Space *s=context;return ProbeMap(s->id,va,pa,permissions); }
static int remove_page(void *context,uintptr_t va,uintptr_t pa)
{ struct Space *s=context;return ProbeUnmap(s->id,va,pa); }
static unsigned char *admit(void *context,const struct KrnBufferSpace *space,
 uintptr_t va,uintptr_t pa,size_t size,unsigned access)
{
 struct Space *s=context;
 if(space->domain!=&s->domain)return NULL;
 /* The platform owns these physical allocations, not arbitrary RAM/MMIO. */
 if(pa!=ProbeBacking(s->id) && pa!=(uintptr_t)memory[s->id][0] && pa!=(uintptr_t)memory[s->id][1])return NULL;
 return ProbeAdmit(s->id,va,pa,size,access);
}
static void populate(struct Space *s,struct KrnMappingObject *m,unsigned i,uintptr_t pa)
{
 struct KrnMappingRequest r;
 CHECK(KrnMappingFault(m,m->start+i*PAGE,KMAP_WRITE,1,&r,&s->mapops)==KMAP_FAULT_QUEUE,"buffer-fault");
 CHECK(KrnMappingComplete(&r,pa,0,&s->mapops),"buffer-populate");
 CHECK(KrnMappingFault(m,m->start+i*PAGE,KMAP_WRITE,1,&r,&s->mapops)==KMAP_FAULT_RETRY,"buffer-dirty");
}
static int pin(struct Space *s,uintptr_t va,size_t len,unsigned access,struct KrnBufferLease *l)
{ return KrnBufferPin(&registry,s->id+1,s->handle,va,len,access,&s->bufops,l); }
void ProbeBufferInit(void)
{
 for(unsigned id=0;id<2;++id){
  struct Space *s=&spaces[id];s->id=id;
  s->mapops=(struct KrnMappingOps){s,install,remove_page};
  s->bufops=(struct KrnBufferOps){s,admit};
  CHECK(KrnMappingArenaInitialize(&s->arena,DATA,5*PAGE,PAGE),"buffer-arena");
  CHECK(KrnBufferRegister(&registry,id+1,&s->domain,&s->arena,&s->handle),"buffer-register");
  void *owner=KrnBufferLookup(&registry,id+1,s->handle);
  CHECK(KrnMappingReserve(&s->arena,&s->main,PAGE,0,3,0,owner,s->mainpages,1),"buffer-main");
  CHECK(KrnMappingReserve(&s->arena,&s->gap,2*PAGE,0,0,0,owner,s->gappages,2),"buffer-gap");
  CHECK(KrnMappingReserve(&s->arena,&s->temp,2*PAGE,0,3,0,owner,s->temppages,2),"buffer-temp");
  populate(s,&s->main,0,ProbeBacking(id));
  for(unsigned i=0;i<2;++i)populate(s,&s->temp,i,(uintptr_t)memory[id][i]);
  CHECK(KrnServiceInitialize(&s->queue,&s->domain),"buffer-service-init");
 }
 /* Distinct queues start at the same queue token: record generation must
    distinguish a delayed worker after cross-domain request-record reuse. */
 struct KrnBufferService r={0};unsigned char bytes[8];size_t n=0;
 CHECK(KrnBufferServiceSubmit(&registry,1,spaces[0].handle,&spaces[0].queue,TEMP,TEMP,8,&spaces[0].bufops,&r),"buffer-cross-queue-first");
 uint64_t old=r.token,queue_token=r.service_token;
 CHECK(KrnBufferServiceCancel(&r,1,old),"buffer-cross-queue-cancel");
 CHECK(KrnBufferServiceSubmit(&registry,2,spaces[1].handle,&spaces[1].queue,TEMP,TEMP,8,&spaces[1].bufops,&r),"buffer-cross-queue-second");
 CHECK(r.service_token==queue_token && r.token!=old,"buffer-cross-queue-generation");
 uint64_t token=r.token;
 CHECK(KrnBufferServiceTake(&r,token,bytes,sizeof(bytes),&n),"buffer-cross-queue-take");
 CHECK(!KrnBufferServiceComplete(&r,old,bytes,n),"buffer-cross-queue-worker");
 CHECK(KrnBufferServiceCancel(&r,2,token) && KrnBufferServiceComplete(&r,token,bytes,n),"buffer-cross-queue-drain");
 cross_queue=1;
}
int ProbeBufferService(unsigned id,uint64_t ptr,uint64_t len,unsigned mask)
{
 struct Space *s=&spaces[id];struct KrnBufferService r={0};unsigned char bytes[KBUF_BYTES];size_t n=0;
 if(!KrnBufferServiceSubmit(&registry,id+1,s->handle,&s->queue,ptr,ptr,len,&s->bufops,&r)){++refused;return 0;}
 uint64_t token=r.token;
 CHECK(KrnBufferServiceTake(&r,token,bytes,sizeof(bytes),&n),"buffer-service-take");
 for(size_t i=0;i<n;++i)bytes[i]^=(unsigned char)mask;
 CHECK(KrnBufferServiceComplete(&r,token,bytes,n),"buffer-service-complete");
 CHECK(KrnBufferServiceCollect(&r,id+1,token,&s->bufops),"buffer-service-collect");
 ++copied;return 1;
}
void ProbeBufferFinish(unsigned id)
{
 struct Space *s=&spaces[id];struct KrnBufferLease l={0};unsigned char bytes[16];
 for(unsigned j=0;j<16;++j)bytes[j]=(unsigned char)(0x80+id+j);
 /* A copy crossing real pages must reach each owned backing allocation. */
 CHECK(pin(s,TEMP+PAGE-8,16,KMAP_WRITE,&l),"buffer-cross-pin");
 CHECK(KrnBufferWrite(&l,id+1,bytes,16,&s->bufops),"buffer-cross-write");
 CHECK(memory[id][0][PAGE-1]==bytes[7] && memory[id][1][0]==bytes[8],"buffer-cross-bytes");
 CHECK(KrnBufferRelease(&l),"buffer-cross-release");
 CHECK(!KrnBufferPin(&registry,2-id,s->handle,TEMP,8,KMAP_READ,&s->bufops,&l),"buffer-foreign-domain");
 CHECK(!KrnBufferPin(&registry,id+1,s->handle+(UINT64_C(1)<<32),TEMP,8,KMAP_READ,&s->bufops,&l),"buffer-stale-domain");
 CHECK(!pin(s,UINTPTR_MAX-3,8,KMAP_READ,&l),"buffer-overflow");
 CHECK(!pin(s,DATA+PAGE-4,8,KMAP_READ,&l),"buffer-guard");
 CHECK(!pin(s,TEMP+2*PAGE-4,8,KMAP_READ,&l),"buffer-unmapped");
 void *owner=s->temp.owner;s->temp.owner=&s->domain;
 CHECK(!pin(s,TEMP,8,KMAP_READ,&l),"buffer-foreign-page");s->temp.owner=owner;
 /* Separate software-ledger and hardware-permission refusal oracles. */
 s->temppages[1].protection=KMAP_READ;
 CHECK(!pin(s,TEMP+PAGE-4,8,KMAP_WRITE,&l),"buffer-readonly-ledger");s->temppages[1].protection=3;
 CHECK(ProbeMap(id,TEMP+PAGE,(uintptr_t)memory[id][1],KMAP_READ),"buffer-hw-ro");
 CHECK(!pin(s,TEMP+PAGE-4,8,KMAP_WRITE,&l),"buffer-readonly-hardware");
 CHECK(ProbeMap(id,TEMP+PAGE,(uintptr_t)memory[id][1],3),"buffer-hw-rw");
 CHECK(pin(s,TEMP+PAGE-8,16,KMAP_WRITE,&l),"buffer-recheck-pin");
 CHECK(ProbeMap(id,TEMP+PAGE,(uintptr_t)memory[id][1],KMAP_READ),"buffer-recheck-ro");
 unsigned char saved=memory[id][0][PAGE-1];bytes[7]^=0xff;
 CHECK(!KrnBufferWrite(&l,id+1,bytes,16,&s->bufops) && memory[id][0][PAGE-1]==saved,"buffer-copy-atomic");
 CHECK(ProbeMap(id,TEMP+PAGE,(uintptr_t)memory[id][1],3),"buffer-recheck-rw");
 CHECK(KrnBufferRelease(&l),"buffer-recheck-release");
 struct KrnBufferService r={0};
 CHECK(KrnBufferServiceSubmit(&registry,id+1,s->handle,&s->queue,TEMP,TEMP+PAGE-4,8,&s->bufops,&r),"buffer-cancel-submit");
 uint64_t old=r.token;CHECK(KrnBufferServiceCancel(&r,id+1,old),"buffer-cancel-queued");++cancelled;
 CHECK(KrnBufferServiceSubmit(&registry,id+1,s->handle,&s->queue,TEMP,TEMP+PAGE-4,8,&s->bufops,&r),"buffer-running-submit");
 CHECK(!KrnBufferServiceCancel(&r,id+1,old),"buffer-stale-request");
 uint64_t token=r.token;
 size_t n=0;CHECK(KrnBufferServiceTake(&r,token,bytes,sizeof(bytes),&n),"buffer-running-take");
 CHECK(!KrnBufferServiceComplete(&r,old,bytes,n),"buffer-stale-worker");
 CHECK(KrnBufferServiceCancel(&r,id+1,r.token),"buffer-running-cancel");
 CHECK(!KrnMappingBeginClose(&s->temp),"buffer-unmap-retained");
 CHECK(!KrnMappingReleasePage(&s->temp,0,&s->mapops),"buffer-backing-retained");
 CHECK(KrnBufferServiceComplete(&r,token,bytes,n),"buffer-cancel-ack");++cancelled;
 CHECK(!r.queue && !s->temp.in_flight,"buffer-request-drain");
 for(unsigned i=0;i<2;++i)CHECK(KrnMappingReleasePage(&s->temp,i,&s->mapops)==(uintptr_t)memory[id][i],"buffer-unmap");
 CHECK(KrnMappingRelease(&s->arena,&s->temp),"buffer-retire-map");
 /* Reassign backing only after drain; old request must never reach it. */
 CHECK(KrnMappingReserve(&s->arena,&s->temp,2*PAGE,0,3,0,owner,s->temppages,2),"buffer-reassign");
 for(unsigned i=0;i<2;++i)populate(s,&s->temp,i,(uintptr_t)memory[id][i]);
 memory[id][0][0]=0x5e;
 CHECK(!KrnBufferServiceComplete(&r,token,bytes,n) && !KrnBufferServiceCollect(&r,id+1,old,&s->bufops) && memory[id][0][0]==0x5e,"buffer-no-reassigned-write");
 /* Completion-before-close must also refuse publication. */
 CHECK(KrnBufferServiceSubmit(&registry,id+1,s->handle,&s->queue,TEMP,TEMP,8,&s->bufops,&r),"buffer-ready-submit");
 token=r.token;
 CHECK(KrnBufferServiceTake(&r,token,bytes,sizeof(bytes),&n),"buffer-ready-take");bytes[0]=0xee;
 CHECK(KrnBufferServiceComplete(&r,token,bytes,n),"buffer-ready-complete");
 CHECK(KrnBufferClose(&registry,id+1,s->handle),"buffer-domain-close");
 CHECK(!KrnBufferServiceCollect(&r,id+1,token,&s->bufops) && memory[id][0][0]==0x5e,"buffer-close-no-write");
 CHECK(KrnServiceDrained(&s->queue),"buffer-queue-drained");
 CHECK(KrnMappingReleasePage(&s->main,0,&s->mapops)==ProbeBacking(id),"buffer-main-release");
 for(unsigned i=0;i<2;++i)CHECK(KrnMappingReleasePage(&s->temp,i,&s->mapops)==(uintptr_t)memory[id][i],"buffer-temp-release");
 CHECK(KrnMappingRelease(&s->arena,&s->main) && KrnMappingRelease(&s->arena,&s->gap) && KrnMappingRelease(&s->arena,&s->temp),"buffer-arena-drain");
 uint64_t handle=s->handle;
 CHECK(KrnBufferUnregister(&registry,id+1,handle) && !KrnBufferLookup(&registry,id+1,handle) && KrnMappingDomainCanReclaim(&s->domain),"buffer-domain-drain");++retired;
}
const char *ProbeBufferSummary(void)
{
 CHECK(copied==2 && refused==4 && cancelled==4 && retired==2 && cross_queue==1,"buffer-final-counts");
 return "PASS buffers domains=2 copied=2 refused=4 cancelled=4 retired=2 cross_queue=1\n";
}
