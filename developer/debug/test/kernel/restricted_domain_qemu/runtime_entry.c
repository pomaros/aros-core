/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Exercise the actual production vector assembly with the existing two-root
 * MMU workload. Only the C scheduler is a fixture, not the entry/return path.
 */
#include <aros/apple/runtime.h>
#define main mechanism_main
#define handle_exception mechanism_handle
#include "probe.c"
#undef main
#undef handle_exception
static struct AROSAppleRuntimeLocal local;
static unsigned nested;
static uint64_t read_root(void) { uint64_t x; __asm__ volatile("mrs %0,ttbr0_el1":"=r"(x)); return x; }
static uint64_t read_tls(void) { uint64_t x; __asm__ volatile("mrs %0,tpidr_el0":"=r"(x)); return x; }
static void trusted_root(void) {
 __asm__ volatile("dsb ish; msr ttbr0_el1,%0; isb; tlbi vmalle1; dsb ish; isb"::"r"(trusted_template):"memory");
}
extern void fixture_enter_guest(struct Frame *) __attribute__((noreturn));
void enter_guest(struct Frame *f) {
 local.trusted_ttbr0=trusted_template;
 local.kernel_tls=0xabc000;
 __asm__ volatile("msr tpidrro_el0,%0; msr tpidr_el0,%1; isb"::"r"(&local),"r"(UINT64_C(0xdef000)):"memory");
 fixture_enter_guest(f);
}
int AROSAppleRuntimeHandleException(struct AROSAppleRuntimeFrame *f) {
 if(read_root()!=trusted_template)fail("runtime-trusted-root");
 if(read_tls()!=local.kernel_tls)fail("runtime-kernel-tls");
 if(nested) {
  if(f->vector!=4 || f->entry_ttbr0!=trusted_template ||
     f->return_ttbr0!=trusted_template || f->task_tls!=local.kernel_tls ||
     (f->esr&0xffff)!=0x71)fail("runtime-nested-entry");
  return 1;
 }
 if(f->vector!=8 || f->entry_ttbr0!=roots[current] ||
    f->return_ttbr0!=roots[current])fail("runtime-entry-root");
 if(f->task_tls!=UINT64_C(0xdef000)+current)fail("runtime-task-tls");
 nested=1;
 __asm__ volatile("svc #0x71":::"memory");
 nested=0;
 if(read_root()!=trusted_template || read_tls()!=local.kernel_tls)fail("runtime-nested-return");
 struct Frame legacy;
 memcpy(&legacy,f,288);
 memcpy(legacy.q,f->simd,528);
 mechanism_handle(&legacy);
 /* The old fixture scheduler selects its root directly. Production dispatch
  * instead writes return_ttbr0 while continuing C under the trusted root. */
 trusted_root();
 memcpy(f,&legacy,288);
 memcpy(f->simd,legacy.q,528);
 f->return_ttbr0=roots[current];
 f->task_tls=UINT64_C(0xdef000)+current;
 return 1;
}
void main(void) { mechanism_main(); }
