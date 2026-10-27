/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <exec/tasks.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include <aros/debug.h>
#include <utility/hooks.h>
#include <proto/task.h>

int __nowbsupport=1, __nostdiowin=1, __nocommandline=1;
struct WBStartup *WBenchMsg;
static ULONG started, returned;
APTR TaskResBase;
static struct Task *owners[16];
static ULONG owner_done[16], observed;
static struct Hook stop_hook;
AROS_UFH3(static IPTR, owner_stopped,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(struct Task *, task, A2),
    AROS_UFHA(APTR, message, A1))
{
    AROS_USERFUNC_INIT
    (void)hook; (void)message;
    for(unsigned i=0;i<16;i++) {
        if(__atomic_load_n(&owners[i],__ATOMIC_ACQUIRE)==task &&
           !__atomic_exchange_n(&owner_done[i],1,__ATOMIC_ACQ_REL)) {
            __atomic_add_fetch(&observed,1,__ATOMIC_RELEASE);
            break;
        }
    }
    return 0;
    AROS_USERFUNC_EXIT
}
static void worker(void)
{
    __atomic_add_fetch(&started,1,__ATOMIC_RELEASE);
    /* Creation may schedule us before the parent has published its record.
     * Pending Exec signals survive a Signal-before-Wait ordering. */
    Wait(SIGBREAKF_CTRL_C);
    RemTask(NULL);
    __atomic_add_fetch(&returned,1,__ATOMIC_RELEASE);
}
static int delay(struct timerequest *request)
{
    request->tr_node.io_Command=TR_ADDREQUEST;
    request->tr_time.tv_secs=0;
    request->tr_time.tv_micro=10000;
    return DoIO((struct IORequest *)request)==0;
}
static int batch(struct timerequest *timer,unsigned count)
{
    ULONG ids[16]={0};unsigned created=0, attempt;int ok=0;
    ULONG target=__atomic_load_n(&started,__ATOMIC_ACQUIRE)+count;
    for(unsigned i=0;i<16;i++) {
        __atomic_store_n(&owners[i],NULL,__ATOMIC_RELEASE);
        __atomic_store_n(&owner_done[i],0,__ATOMIC_RELEASE);
    }
    for(unsigned i=0;i<count;i++) {
        /* Workers cannot exit until signalled after record publication. */
        struct Task *task=CreateTask((STRPTR)"Lifecycle.self-exit",0,worker,32768);
        if(!task) goto cleanup;
        ids[created++]=GetETaskID(task);
        __atomic_store_n(&owners[created-1],task,__ATOMIC_RELEASE);
        if(!ids[created-1]) goto cleanup;
    }
    for(unsigned i=0;i<created;i++)
        Signal(__atomic_load_n(&owners[i],__ATOMIC_ACQUIRE),SIGBREAKF_CTRL_C);
    for(attempt=0;attempt<200;attempt++) {
        if(!delay(timer)) goto cleanup;
        unsigned live=0;
        for(unsigned i=0;i<created;i++) if(FindTaskByPID(ids[i])) live++;
        if(!live && __atomic_load_n(&started,__ATOMIC_ACQUIRE)==target &&
           __atomic_load_n(&observed,__ATOMIC_ACQUIRE)==target) {
            ok=1;break;
        }
    }
cleanup:
    if(!ok) {
        /* A failed cohort has no proved stop boundary. Retain this command's
         * workers, callback and records for supervisor inspection; never
         * force-remove a possibly running worker or unload beneath it. */
        kprintf("[LIFECYCLE] FAIL unretired cohort created=%u observed=%lu\n",
            created,(unsigned long)__atomic_load_n(&observed,__ATOMIC_ACQUIRE));
        for(;;) Wait(0);
    }
    return ok;
}
int main(void)
{
    struct MsgPort *port=NULL;struct timerequest *timer=NULL;
    BOOL opened=FALSE;int result=RETURN_FAIL;
    BOOL registered=FALSE;
    IPTR before=0,after=0;
    kprintf("[LIFECYCLE] probe begin signalled self-removal\n");
    TaskResBase=OpenResource((CONST_STRPTR)"task.resource");
    if(!TaskResBase || ((struct Library *)TaskResBase)->lib_Version<2) goto done;
    stop_hook.h_Entry=(HOOKFUNC)owner_stopped;
#ifndef LIFECYCLE_NO_STOP_HOOK
    registered=TaskStopHookControl(&stop_hook,TRUE);
    if(!registered) goto done;
#endif
    port=CreateMsgPort();
    if(port) timer=(struct timerequest *)CreateIORequest(port,sizeof(*timer));
    if(!timer || OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_MICROHZ,(struct IORequest *)timer,0)) goto done;
    opened=TRUE;
    if(!batch(timer,16) || !batch(timer,16)) goto done;
    before=AvailMem(MEMF_ANY);
    for(unsigned i=0;i<8;i++) if(!batch(timer,16)) goto done;
    for(unsigned i=0;i<10;i++) if(!delay(timer)) goto done;
    after=AvailMem(MEMF_ANY);
    if(__atomic_load_n(&started,__ATOMIC_ACQUIRE)!=160 ||
       __atomic_load_n(&returned,__ATOMIC_ACQUIRE) ||
       __atomic_load_n(&observed,__ATOMIC_ACQUIRE)!=160) goto done;
    kprintf("[LIFECYCLE] PASS self-exits=160 returned=0 retired=160 observed=160 free-before=%llu free-after=%llu\n",
        (unsigned long long)before,(unsigned long long)after);
    result=RETURN_OK;
done:
    if(registered && !TaskStopHookControl(&stop_hook,FALSE)) {
        /* Refusal leaves a live callback. Keep its code resident until the
         * bounded harness captures this failure and stops this guest. */
        kprintf("[LIFECYCLE] FAIL stop-hook removal refused\n");
        for(;;) Wait(0);
    }
    if(result!=RETURN_OK) kprintf("[LIFECYCLE] FAIL started=%lu returned=%lu observed=%lu free-before=%llu free-after=%llu\n",
        (unsigned long)__atomic_load_n(&started,__ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&returned,__ATOMIC_ACQUIRE),
        (unsigned long)__atomic_load_n(&observed,__ATOMIC_ACQUIRE),(unsigned long long)before,(unsigned long long)after);
    if(opened) CloseDevice((struct IORequest *)timer);
    if(timer) DeleteIORequest((struct IORequest *)timer);
    if(port) DeleteMsgPort(port);
    return result;
}
