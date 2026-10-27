/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef intptr_t SIPTR;
typedef void *APTR;
typedef int32_t LONG;
#define NT_TASK 1
#define NT_PROCESS 13
#define NT_MSGPORT 4
#define DOSFALSE 0
#define FALSE 0
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_NO_FREE_STORE 103
#define AN_AsyncPkt 1
#define AROS_LIBFUNC_INIT
#define D(x)
#define bug(...) ((void)0)
struct Node { unsigned ln_Type; char *ln_Name; };
struct Task { struct Node tc_Node; };
struct MsgPort { struct Node mp_Node; };
struct Process { struct Task pr_Task; struct MsgPort pr_MsgPort; SIPTR pr_Result2; };
struct DosPacket { LONG dp_Type; SIPTR dp_Arg1,dp_Arg2,dp_Arg3,dp_Arg4,dp_Arg5,dp_Arg6,dp_Arg7,dp_Res1,dp_Res2; };
static struct Task *self;
static struct MsgPort target, reply;
static struct DosPacket *sent;
static int fail_packet,fail_port,bad_memory,wrong_reply;
static unsigned allocated,freed,ports,deleted,sends,waits,nils;
static SIPTR answer,secondary;
static int zero_args;
#define __is_process(task) (((struct Task *)(task))->tc_Node.ln_Type == NT_PROCESS)
#define ASSERT_VALID_TASK(task) assert((task) && ((task)->tc_Node.ln_Type==NT_TASK || (task)->tc_Node.ln_Type==NT_PROCESS))
#define ASSERT_VALID_PROCESS(proc) assert(__is_process(proc))
static struct Task *FindTask(void *unused){(void)unused;return self;}
static int TypeOfMem(void *p){assert(p==&target);return !bad_memory;}
static struct DosPacket *allocdospacket(void){if(fail_packet)return NULL;allocated++;return calloc(1,sizeof(struct DosPacket));}
static void freedospacket(struct DosPacket *p){freed++;free(p);}
static struct MsgPort *CreateMsgPort(void){if(fail_port)return NULL;ports++;return &reply;}
static void DeleteMsgPort(struct MsgPort *p){assert(p==&reply);deleted++;}
static SIPTR handleNIL(LONG action,SIPTR a,SIPTR b,SIPTR c){assert(action==77 && a==1 && b==2 && c==3);nils++;return 91;}
static void internal_SendPkt(struct DosPacket *dp,struct MsgPort *to,struct MsgPort *back)
{
 assert(to==&target);assert(back==(__is_process(self)?&((struct Process *)self)->pr_MsgPort:&reply));
 assert(dp->dp_Type==77);
 if(zero_args) assert(!dp->dp_Arg1 && !dp->dp_Arg2 && !dp->dp_Arg3 && !dp->dp_Arg4 && !dp->dp_Arg5 && !dp->dp_Arg6 && !dp->dp_Arg7);
 else assert(dp->dp_Arg1==1 && dp->dp_Arg2==2 && dp->dp_Arg3==3 && dp->dp_Arg4==4 && dp->dp_Arg5==5 && dp->dp_Arg6==6 && dp->dp_Arg7==7);
 assert(dp->dp_Res1==0 && dp->dp_Res2==0);
 sends++;sent=dp;dp->dp_Res1=answer;dp->dp_Res2=secondary;
}
static struct DosPacket *internal_WaitPkt(struct MsgPort *p){(void)p;waits++;return wrong_reply?NULL:sent;}
static void Alert(int value){assert(value==AN_AsyncPkt);abort();}
#include "actual.c"
static void reset(void)
{
 fail_packet=fail_port=bad_memory=wrong_reply=zero_args=0;
 allocated=freed=ports=deleted=sends=waits=nils=0;
 target.mp_Node.ln_Type=NT_MSGPORT;answer=41;secondary=205;
}
static SIPTR call(SIPTR *res2,struct MsgPort *port){return dopacket(res2,port,77,1,2,3,4,5,6,7);}
int main(void)
{
 /* A bare Task allocation ends exactly where a Process extension would
  * begin: ASan must detect any accidental pr_Result2 write. */
 struct Task *task=calloc(1,sizeof(*task));assert(task);task->tc_Node.ln_Type=NT_TASK;
 struct Process process={.pr_Task.tc_Node.ln_Type=NT_PROCESS};
 unsigned cases=0;
 for(unsigned kind=0;kind<2;kind++){
  self=kind?&process.pr_Task:task;
  reset();SIPTR res2=-1;assert(call(&res2,&target)==41 && res2==205);
  assert(allocated==1 && freed==1 && sends==1 && waits==1 && ports==!kind && deleted==!kind);
  if(kind)assert(process.pr_Result2==205);cases++;
  reset();answer=0;secondary=103;res2=-1;
  assert(call(&res2,&target)==0 && res2==103);if(kind)assert(process.pr_Result2==103);cases++;
  reset();res2=-1;fail_packet=1;process.pr_Result2=-1;
  assert(!call(&res2,&target) && res2==103 && !allocated && !sends && !ports);
  if(kind)assert(process.pr_Result2==103);cases++;
  reset();res2=-1;bad_memory=1;process.pr_Result2=-1;
  assert(!call(&res2,&target) && res2==212 && !allocated && !sends);
  if(kind)assert(process.pr_Result2==212);cases++;
  reset();res2=-1;target.mp_Node.ln_Type=NT_TASK;
  assert(!call(&res2,&target) && res2==212 && !allocated && !sends);cases++;
  reset();res2=-1;assert(call(&res2,NULL)==91 && res2==0 && nils==1 && !allocated);cases++;
  reset();assert(call(NULL,&target)==41);if(kind)assert(process.pr_Result2==205);cases++;
 }
 self=task;reset();fail_port=1;SIPTR res2=-1;
 assert(!call(&res2,&target) && res2==103 && allocated==1 && freed==1 && !sends && !ports && !deleted);cases++;
 self=task;reset();SIPTR args[7]={1,2,3,4,5,6,7};res2=-1;
 assert(actual_result(&target,77,args,&res2)==41 && res2==205);cases++;
 reset();zero_args=1;res2=-1;
 assert(actual_result(&target,77,NULL,&res2)==41 && res2==205);cases++;
 free(task);printf("PASS: actual dopacket transport, %u Task/Process result, error, allocation and cleanup cases\n",cases);
 return 0;
}
