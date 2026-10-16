/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <task_intern.h>
#include "../kernel/task_accounting.h"
/* Registry nodes survive LockTaskList, but their tasks do not: RemTask clears
 * tle_Task before freeing the task. Validate the address under UP Disable before
 * dereferencing it. No raw task pointer is cached between calls. */
static struct IntETask *apple_cpu_query(struct TaskResBase *base,struct Task *task,struct AppleTaskSample *sample,struct IntETask *snapshot)
{
    struct IntETask *et=NULL;
    sample->seconds=sample->micros=sample->usage=0;
    struct timerequest request={0};struct timeval awake;
    struct Device *TimerBase;
    struct TaskListEntry *entry;
    BOOL found=FALSE;
    request.tr_node.io_Message.mn_Length=sizeof(request);
    if(OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest *)&request,0))return NULL;
    TimerBase=request.tr_node.io_Device;
    Disable();
    ForeachNode(&base->trb_TaskList,entry)if(entry->tle_Task==task){found=TRUE;break;}
    if(!found)ForeachNode(&base->trb_NewTasks,entry)if(entry->tle_Task==task){found=TRUE;break;}
    if(found && task && (task->tc_Flags&TF_ETASK) && task->tc_UnionETask.tc_ETask) {
        et=GetIntETask(task);
        snapshot->iet_StartTime=et->iet_StartTime;
        GetUpTime(&awake);
        AppleTaskSample(et,AppleTaskCounter(),AppleTaskFrequency(),
            (uint64_t)awake.tv_secs*1000000+awake.tv_micro,
            task==SysBase->ThisTask && task->tc_State==TS_RUN,sample);
    }
    et=et?snapshot:NULL;
    Enable();
    CloseDevice((struct IORequest *)&request);
    return et;
}
#define TASK_CPU_QUERY_PREPARE struct AppleTaskSample cpu_sample; struct IntETask cpu_snapshot={0}; \
    struct IntETask *task_et=apple_cpu_query(TaskResBase,task,&cpu_sample,&cpu_snapshot)
#define TASK_CPU_QUERY_TIME(out) do { if(out){(out)->tv_secs=cpu_sample.seconds;(out)->tv_micro=cpu_sample.micros;} } while(0)
#define TASK_CPU_QUERY_USAGE(out) do { if(out)*(out)=cpu_sample.usage; } while(0)
/* Apple has no generic register implementation; do not touch a retired task. */
#define TASK_QUERY_SKIP_TAG(tag) ((tag)!=TaskTag_CPUNumber && (tag)!=TaskTag_CPUAffinity && (tag)!=TaskTag_CPUTime && (tag)!=TaskTag_CPUUsage && (tag)!=TaskTag_StartTime)
#include "../../../rom/task/QueryTaskTagList.c"
