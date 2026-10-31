/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <aros/apple/restricted.h>
struct AROSAppleRuntimeFrame { uint64_t vector,spsr,esr; };
struct ExceptionContext { unsigned present; };
struct AROSAppleTaskExecution { uint64_t root; };
static struct ExceptionContext context;
static struct AROSAppleTaskExecution execution;
static unsigned locked,calls,switched,masks;
static int handled=1;
#define SC_SWITCH 42
static void *r_current(void) { return &context; }
static struct ExceptionContext *r_context(void *task) { assert(task==&context);return context.present?&context:NULL; }
static struct AROSAppleTaskExecution *AROSAppleTaskExecution(struct ExceptionContext *c) { assert(c==&context);return &execution; }
static void r_coordinate(void) { assert(!locked);locked=1; }
static void r_uncoordinate(void) { assert(locked);locked=0; }
static int r_trap_locked(struct AROSAppleRuntimeFrame *f) { (void)f;assert(locked);calls++;return handled; }
static void core_SysCall(unsigned op,struct AROSAppleRuntimeFrame *f) { (void)f;assert(!locked && op==SC_SWITCH);switched++; }
static void cpu_ApplyCurrentTaskMasks(uint64_t *s) { (void)s;assert(!locked);masks++; }
#include "actual.inc"
static void native(void)
{
 context.present=1;execution.root=0;handled=1;
 const unsigned services[]={R_PREPARE,0xa532,0xa533,0xa534,0xa535,0xa536,0xa537,R_CLAIM,R_PUBLISH,R_RELEASED,R_LIFETIME,
 R_DOMAIN_CLAIM,R_DOMAIN_PUBLISH,R_DOMAIN_PREPARE,R_DOMAIN_CLOSE,R_DOMAIN_RELEASED,R_DOMAIN_REAP,R_DOMAIN_ABORT,R_MEMORY,R_SHARED,R_TABLE_GROW,R_WAKE,R_THREAD_STATE,R_SUSPEND,R_RESUME};
 for(unsigned i=0;i<sizeof(services)/sizeof(services[0]);i++) {
  struct AROSAppleRuntimeFrame frame={8,0,(UINT64_C(0x15)<<26)|services[i]};calls=0;
  assert(AROSAppleRestrictedTrap(&frame)==1 && calls==1 && !locked);
 }
 const unsigned refused[]={AROS_RESTRICTED_THREAD_INFO,AROS_RESTRICTED_WAIT,AROS_RESTRICTED_WAKE,0xa600,0xffff};
 for(unsigned i=0;i<sizeof(refused)/sizeof(refused[0]);i++) {
  struct AROSAppleRuntimeFrame frame={8,0,(UINT64_C(0x15)<<26)|refused[i]};calls=0;
  assert(!AROSAppleRestrictedTrap(&frame) && !calls);
 }
}
static void boundaries(void)
{
 struct AROSAppleRuntimeFrame frame={8,0,(UINT64_C(0x15)<<26)|R_WAKE};calls=0;
 context.present=0;assert(!AROSAppleRestrictedTrap(&frame) && !calls);context.present=1;
 frame.vector=9;assert(!AROSAppleRestrictedTrap(&frame) && !calls);frame.vector=8;
 frame.spsr=5;assert(!AROSAppleRestrictedTrap(&frame) && !calls);frame.spsr=0;
 frame.esr=UINT64_C(0x24)<<26;assert(!AROSAppleRestrictedTrap(&frame) && !calls);
 execution.root=0x1000;assert(AROSAppleRestrictedTrap(&frame)==1 && calls==1);
 frame.esr=(UINT64_C(0x15)<<26)|AROS_RESTRICTED_WAIT;handled=2;switched=masks=0;
 assert(AROSAppleRestrictedTrap(&frame)==1 && switched==1 && masks==1 && !locked);
 handled=0;assert(!AROSAppleRestrictedTrap(&frame));
}
int main(void) { native();boundaries();puts("PASS native admission and unlocked scheduling"); }
