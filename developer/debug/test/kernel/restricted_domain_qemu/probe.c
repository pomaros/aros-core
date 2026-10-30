/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include <stdint.h>
#include <stddef.h>
#include "mapping.h"

#ifndef MUTANT
#define MUTANT 0
#endif
#ifndef ROOT_CLONE
#define ROOT_CLONE 0
#endif
#define PAGE 4096U
#define CODE UINT64_C(0x10000000)
#define DATA UINT64_C(0x10001000)
#define STACK UINT64_C(0x10003000)
#define TCR (UINT64_C(25) | (UINT64_C(1)<<8) | (UINT64_C(1)<<10) | (UINT64_C(3)<<12) | (UINT64_C(1)<<23) | (UINT64_C(2)<<32))
#define RW (AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE|AARCH64_PROTECT_USER)
#define RO (AARCH64_PROTECT_READ|AARCH64_PROTECT_USER)
struct Frame { uint64_t x[31], sp, pc, psr, esr, far, q[64], fpcr, fpsr; };
_Static_assert(sizeof(struct Frame)==816, "assembly frame size");
_Static_assert(offsetof(struct Frame,q)==288, "assembly SIMD offset");
static uint64_t tables[32][512] __attribute__((aligned(PAGE)));
static uint64_t data[2][512] __attribute__((aligned(PAGE)));
static uint64_t stacks[2][512] __attribute__((aligned(PAGE)));
static struct Frame frames[2];
static uint64_t roots[2];
static uint64_t trusted_template, template_hash;
static unsigned template_tables;
extern unsigned char _start[], __kernel_text_end[];
static unsigned used, current, yields[2], faults[2], denials[2], done[2];
static unsigned protections[2], bridge_ok[2], bridge_bad[2];
static unsigned kernel_faults[2], guard_faults[2], nx_faults[2];
static volatile uint64_t kernel_secret __attribute__((aligned(PAGE)))=UINT64_C(0xdefaced);
extern unsigned char owned_write_probe[], kernel_read_probe[], kernel_write_probe[], guard_read_probe[], after_nx_probe[];
extern unsigned char guest_start[], guest_end[];
static uint64_t guest_pc(unsigned char *label) { return CODE+(uintptr_t)label-(uintptr_t)guest_start; }
extern void enter_guest(struct Frame *) __attribute__((noreturn));
void *memcpy(void *dst,const void *src,size_t n) {
 unsigned char *d=dst;const unsigned char *s=src;
 for(size_t i=0;i<n;i++)d[i]=s[i];return dst;
}
void *memset(void *dst,int value,size_t n) {
 unsigned char *d=dst;for(size_t i=0;i<n;i++)d[i]=(unsigned char)value;return dst;
}
static void putc_uart(char c) {
 volatile uint32_t *u=(void *)UINT64_C(0x09000000);
 while(u[6]&(1U<<5)){}u[0]=(unsigned char)c;
}
static void text(const char *s){while(*s)putc_uart(*s++);}
static void hex(uint64_t v){for(int i=60;i>=0;i-=4)putc_uart("0123456789abcdef"[(v>>i)&15]);}
static void stop(unsigned status) __attribute__((noreturn));
static void stop(unsigned status) {
 uint64_t args[2]={0x20026,status};
 register uint64_t op __asm__("x0")=0x20;
 register uint64_t block __asm__("x1")=(uintptr_t)args;
 __asm__ volatile("hlt #0xf000" : "+r"(op),"+r"(block)::"memory");
 for(;;)__asm__ volatile("wfe");
}
static void fail(const char *s){text("FAIL ");text(s);text("\n");stop(1);}
void unexpected_exception(uint64_t esr,uint64_t pc){text("UNEXPECTED ");hex(esr);text(" ");hex(pc);text("\n");stop(1);}
static uint64_t *resolve(void *c,uint64_t pa){(void)c;return (uint64_t *)(uintptr_t)pa;}
static uint64_t allocate(void *c,unsigned bits){(void)c;if(bits!=12||used==32)return 0;return (uintptr_t)tables[used++];}
static void clean(void *c,const void *p,size_t n){(void)c;(void)p;(void)n;__asm__ volatile("dsb ishst":::"memory");}
static void invalidate(void *c){(void)c;__asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb":::"memory");}
static void sync_code(void *c,uint64_t pa,size_t n){(void)c;(void)pa;(void)n;__asm__ volatile("dsb ish; ic iallu; dsb ish; isb":::"memory");}
static const struct AArch64ProtectionOps ops={0,resolve,allocate,clean,invalidate,sync_code,0};
static uint64_t backing(unsigned id){return (uintptr_t)data[(MUTANT==1)?0:id];}
static void switch_root(unsigned id){
 __asm__ volatile("dsb ish; msr ttbr0_el1,%0; isb; tlbi vmalle1is; dsb ish; isb"::"r"(roots[id]):"memory");
}
/* Optional linked mechanism fixture; the default retains this test's scope. */
__attribute__((weak)) void ProbeBufferInit(void) {}
__attribute__((weak)) void ProbeBufferFinish(unsigned id) { (void)id; }
__attribute__((weak)) const char *ProbeBufferSummary(void) { return NULL; }
__attribute__((weak)) int ProbeBufferService(unsigned id, uint64_t ptr, uint64_t len, unsigned mask)
{
 if(ptr<DATA || len>PAGE || ptr-DATA>PAGE-len)return 0;
 unsigned char *p=(unsigned char *)(uintptr_t)backing(id)+(ptr-DATA);
 for(uint64_t i=0;i<len;i++)p[i]^=(unsigned char)mask;
 return 1;
}
void ProbeFail(const char *message) { fail(message); }
uintptr_t ProbeBacking(unsigned id) { return backing(id); }
int ProbeMap(unsigned id, uintptr_t va, uintptr_t pa, unsigned permissions)
{
 uint64_t existing;
 if(AArch64MappingPhysical(&ops,roots[id],TCR,va,&existing))
  return existing==pa && AArch64RemapPage(&ops,roots[id],TCR,va,pa,permissions|AARCH64_PROTECT_USER);
 return AArch64MapPage(&ops,roots[id],TCR,va,pa,permissions|AARCH64_PROTECT_USER,1);
}
int ProbeUnmap(unsigned id, uintptr_t va, uintptr_t pa)
{ return AArch64UnmapPage(&ops,roots[id],TCR,va,pa); }
unsigned char *ProbeAdmit(unsigned id, uintptr_t va, uintptr_t pa, size_t size, unsigned access)
{
 if(size!=PAGE || (va&(PAGE-1)) || (pa&(PAGE-1)))return NULL;
 uint64_t saved,par;
 __asm__ volatile("mrs %0,ttbr0_el1":"=r"(saved));
 switch_root(id);
 if(access==AARCH64_PROTECT_WRITE)
  __asm__ volatile("at s1e0w,%0; isb"::"r"(va):"memory");
 else if(access==AARCH64_PROTECT_READ)
  __asm__ volatile("at s1e0r,%0; isb"::"r"(va):"memory");
 else { fail("probe-admit-access"); }
 __asm__ volatile("mrs %0,par_el1":"=r"(par));
 __asm__ volatile("dsb ish; msr ttbr0_el1,%0; isb; tlbi vmalle1is; dsb ish; isb"::"r"(saved):"memory");
 return !(par&1) && (par&UINT64_C(0x0000fffffffff000))==pa ? (unsigned char *)pa : NULL;
}
void handle_exception(struct Frame *f){
 unsigned id=current;uint64_t ec=f->esr>>26;
 if(ec==0x20){
  if(f->far!=DATA || f->pc!=DATA || (f->esr&63)!=15 || f->x[30]!=guest_pc(after_nx_probe) || nx_faults[id]++)fail("nx-context");
  f->pc=f->x[30];return;
 }
 if(ec==0x24){
  if(f->far==(uintptr_t)&kernel_secret){
   unsigned write=(f->esr&64)!=0;
   if((f->esr&63)!=(ROOT_CLONE?15:13) || kernel_faults[id]!=write || f->pc!=guest_pc(write?kernel_write_probe:kernel_read_probe))fail("kernel-fault-context");
   kernel_faults[id]++;f->pc+=4;return;
  }
  if(f->far==DATA+PAGE){
   if((f->esr&63)!=7 || (f->esr&64) || f->pc!=guest_pc(guard_read_probe) || guard_faults[id]++)fail("guard-context");
   f->pc+=4;return;
  }
  if(f->pc!=guest_pc(owned_write_probe) || f->far!=DATA || !(f->esr&64) || (f->esr&63)!=15 || !protections[id] || faults[id])fail("fault-context");
  if(!AArch64RemapPage(&ops,roots[id],TCR,DATA,backing(id),RW))fail("fault-remap");
  faults[id]++;return; // PC deliberately unchanged: retry the faulting store.
 }
 if(ec!=0x15)fail("exception-class");
 unsigned svc=f->esr&65535;
 if(svc==0x7f){
  if(f->x[0]==1)fail("guest-data");
  if(f->x[0]==2)fail("guest-registers");
  if(f->x[0]==3)fail("guest-svc");
  if(f->x[0]==5)fail("guest-kernel-access");
  fail("guest-bridge");
 }
 if(svc==1){
  yields[id]++;memcpy(&frames[id],f,sizeof(*f));current=1-id;
  switch_root(current);memcpy(f,&frames[current],sizeof(*f));return;
 }
 if(svc==2){
  if(protections[id]++)fail("protect-repeat");
  if(MUTANT!=3 && !AArch64RemapPage(&ops,roots[id],TCR,DATA,backing(id),RO))fail("protect");
  f->x[0]=0;return;
 }
 if(svc==4){
  if(!ProbeBufferService(id,f->x[0],f->x[1],(unsigned)f->x[2])){
   f->x[0]=UINT64_MAX;bridge_bad[id]++;return;
  }
  bridge_ok[id]++;f->x[0]=0;return;
 }
 if(svc==3){
  if(yields[id]!=1000 || faults[id]!=1 || denials[id]!=3 || data[id][0]!=(uint64_t)(id+1)*4096+1001 || bridge_ok[id]!=1 || bridge_bad[id]!=2)fail("completion-counts");
  if(kernel_faults[id]!=2 || guard_faults[id]!=1 || nx_faults[id]!=1 || kernel_secret!=UINT64_C(0xdefaced))fail("isolation-counts");
  ProbeBufferFinish(id);
  done[id]=1;
  if(done[0]&&done[1]){
   const char *summary=ProbeBufferSummary();if(summary)text(summary);
   text("PASS domains=2 switches=2000 faults=2 denied=6 bridge=2 rejected_buffers=4 kernel_faults=4 guard_faults=2 nx_faults=2\n");stop(0);
  }
  current=1-id;switch_root(current);memcpy(f,&frames[current],sizeof(*f));return;
 }
 // This diagnostic's restricted dispatcher does not call the production SVC
 // implementation. It tests the mechanism, not production service isolation.
 denials[id]++;
 f->x[0]=(MUTANT==2 && svc==0xa501)?0:UINT64_MAX;
}
static uint64_t template_fingerprint(void)
{
 uint64_t hash=0;
 for(unsigned t=0;t<template_tables;t++)
  for(unsigned i=0;i<512;i++)hash=(hash<<7)^(hash>>57)^tables[t][i];
 return hash;
}
static void prepare_template(void)
{
 trusted_template=allocate(0,12);
 ((uint64_t *)(uintptr_t)trusted_template)[1]=UINT64_C(0x40000000)|1|4|64|(3U<<8)|(1U<<10)|(UINT64_C(1)<<53)|(UINT64_C(1)<<54);
 if(!AArch64MapPage(&ops,trusted_template,TCR,0x09000000,0x09000000,AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE,0))fail("template-uart");
 for(uintptr_t p=(uintptr_t)_start;p<(uintptr_t)__kernel_text_end;p+=PAGE)
  if(!AArch64ProtectPage(&ops,trusted_template,TCR,p,AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER|AARCH64_PROTECT_EXEC_BOTH))fail("template-text");
 if(((uintptr_t)&kernel_secret>>21)!=((uintptr_t)_start>>21))fail("template-fault-level");
 template_tables=used;template_hash=template_fingerprint();
}
void main(void){
 uint64_t el;__asm__ volatile("mrs %0,CurrentEL":"=r"(el));
 if(el!=4)fail("entry-not-el1");
 if((uintptr_t)guest_start%PAGE || (size_t)(guest_end-guest_start)>PAGE)fail("guest-layout");
 if(ROOT_CLONE)prepare_template();
 for(unsigned id=0;id<2;id++){
  if(ROOT_CLONE){
   if(!AArch64ClonePrivilegedRoot(&ops,trusted_template,TCR,16,&roots[id]))fail("root-clone");
  }else{
  roots[id]=allocate(0,12);
  // Permanent identity RAM map is privileged only. Diagnostic EL1 code/data
  // share a RWX block; production alias-wide W^X is explicitly outside scope.
  ((uint64_t *)(uintptr_t)roots[id])[1]=UINT64_C(0x40000000)|1|4|(3U<<8)|(1U<<10)|(UINT64_C(1)<<54);
  if(!AArch64MapPage(&ops,roots[id],TCR,0x09000000,0x09000000,AARCH64_PROTECT_READ|AARCH64_PROTECT_WRITE,0))fail("uart-map");
  }
  if(!AArch64MapPage(&ops,roots[id],TCR,CODE,(uintptr_t)guest_start,AARCH64_PROTECT_READ|AARCH64_PROTECT_EXEC|AARCH64_PROTECT_USER,1))fail("code-map");
  if(!AArch64MapPage(&ops,roots[id],TCR,DATA,backing(id),RW,1))fail("data-map");
  if(!AArch64MapPage(&ops,roots[id],TCR,STACK,(uintptr_t)stacks[id],RW,1))fail("stack-map");
  if(MUTANT==4 && !AArch64ProtectPage(&ops,roots[id],TCR,(uintptr_t)&kernel_secret,RW))fail("mutant-kernel-map");
  data[id][0]=(id+1)*4096;
  frames[id].pc=CODE;frames[id].sp=STACK+PAGE;frames[id].x[0]=id;frames[id].x[1]=(id+1)*4096;frames[id].x[2]=(uintptr_t)&kernel_secret;
 }
 if(ROOT_CLONE && template_hash!=template_fingerprint())fail("template-modified");
 __asm__ volatile("msr mair_el1,%0; msr tcr_el1,%1; msr ttbr0_el1,%2; isb; tlbi vmalle1; dsb sy; isb"::"r"(UINT64_C(0xff00)),"r"(TCR),"r"(roots[0]):"memory");
 // MMU on, caches off: table stores are visible with DSB. Real TLB and
 // instruction fetch permissions are exercised without claiming cache tests.
 uint64_t sctlr=UINT64_C(0x30d00801);
 __asm__ volatile("msr sctlr_el1,%0; isb"::"r"(sctlr):"memory");
 text("READY restricted-domain-probe\n");
 if(ROOT_CLONE)text("PASS private-roots clones=2 template=unchanged\n");
 ProbeBufferInit();
 enter_guest(&frames[0]);
}
