/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define TF_ETASK 1
#define TS_RUN 2
#define TS_WAIT 3
#define TIMERNAME "timer.device"
#define UNIT_MICROHZ 0
struct IntETask {uint64_t iet_private1,iet_private2,iet_LastBusy,iet_LastUsageStamp;uint32_t iet_CpuUsage;struct {uint64_t tv_sec,tv_nsec;}iet_CpuTime,iet_StartTime;};
#include "task_accounting.h"
struct Task {unsigned tc_Flags,tc_State;struct {struct IntETask *tc_ETask;}tc_UnionETask;};
#define GetIntETask(t) ((t)->tc_UnionETask.tc_ETask)
struct TaskListEntry {struct TaskListEntry *next;struct Task *tle_Task;};
struct List {struct TaskListEntry *first;};
struct TaskResBase {struct List trb_TaskList,trb_NewTasks;};
#define ForeachNode(list,node) for((node)=(list)->first;(node);(node)=(node)->next)
struct Device {int dummy;};static struct Device timer;
struct IORequest {struct {unsigned mn_Length;}io_Message;struct Device *io_Device;};
struct timerequest {struct IORequest tr_node;};
#define timeval mock_timeval
struct timeval {uint32_t tv_secs,tv_micro;};
static struct {struct Task *ThisTask;} exec,*SysBase=&exec;
static unsigned masked,opens,closes,failOpen,retireOpen,retireClose;static uint64_t now,awake;
static struct TaskListEntry entry;static struct Task *allocated;
static void retire(void){assert(!masked);entry.tle_Task=NULL;if(allocated){free(allocated->tc_UnionETask.tc_ETask);free(allocated);allocated=NULL;}exec.ThisTask=NULL;}
static int OpenDevice(const char *name,unsigned unit,struct IORequest *r,unsigned flags){assert(!masked&&!strcmp(name,TIMERNAME)&&!unit&&!flags&&r->io_Message.mn_Length==sizeof(struct timerequest));++opens;if(retireOpen)retire();r->io_Device=&timer;return failOpen;}
static void CloseDevice(struct IORequest *r){assert(!masked&&r->io_Device==&timer);++closes;if(retireClose)retire();}
static void Disable(void){assert(!masked);masked=1;}
static void Enable(void){assert(masked);masked=0;}
static void mock_uptime(struct timeval *v,struct Device *base){assert(masked&&base==&timer);v->tv_secs=awake/1000000;v->tv_micro=awake%1000000;}
#define GetUpTime(v) mock_uptime(v,TimerBase)
#define AppleTaskCounter() (now)
#define AppleTaskFrequency() UINT64_C(1000000)
/* EXTRACT_QUERY */
static void setup(void){retire();allocated=calloc(1,sizeof(*allocated));assert(allocated);allocated->tc_UnionETask.tc_ETask=calloc(1,sizeof(struct IntETask));assert(allocated->tc_UnionETask.tc_ETask);allocated->tc_Flags=TF_ETASK;allocated->tc_State=TS_RUN;entry.tle_Task=allocated;exec.ThisTask=allocated;opens=closes=failOpen=retireOpen=retireClose=0;now=awake=0;}
int main(void){struct TaskResBase base={{&entry},{NULL}};struct AppleTaskSample sample;struct IntETask snapshot;
 setup();AppleTaskDispatch(GetIntETask(allocated),0);assert(apple_cpu_query(&base,allocated,&sample,&snapshot));now=awake=250000;assert(apple_cpu_query(&base,allocated,&sample,&snapshot));assert(sample.micros==250000&&sample.usage==UINT32_MAX&&!masked&&opens==closes);
 /* Retirement during OpenDevice must not dereference the old raw task. */
 setup();struct Task *old=allocated;retireOpen=1;assert(!apple_cpu_query(&base,old,&sample,&snapshot)&&!sample.usage&&!sample.seconds&&!sample.micros);
 /* Snapshot remains valid when CloseDevice permits task exit before return. */
 setup();AppleTaskDispatch(GetIntETask(allocated),0);GetIntETask(allocated)->iet_StartTime.tv_sec=42;now=awake=1234567;retireClose=1;apple_cpu_query(&base,allocated,&sample,&snapshot);assert(!allocated&&sample.seconds==1&&sample.micros==234567&&snapshot.iet_StartTime.tv_sec==42);
 setup();failOpen=1;assert(!apple_cpu_query(&base,allocated,&sample,&snapshot)&&!closes&&!masked);
 setup();assert(!apple_cpu_query(&base,(struct Task *)(uintptr_t)0x123,&sample,&snapshot));
 /* Switch clears the active interval even when Exec retains ThisTask. */
 setup();AppleTaskDispatch(GetIntETask(allocated),0);AppleTaskSwitch(GetIntETask(allocated),0,1000000);now=awake=500000;apple_cpu_query(&base,allocated,&sample,&snapshot);assert(!sample.seconds&&!sample.micros&&!sample.usage);
 retire();puts("apple-task-query-host PASS");}
