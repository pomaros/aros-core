/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1.
   Exec lifetime/PCI ownership reuse the APL virtionet frontend. Single owner,
   single open stream to host CID 2, unit = host port (nonzero). No SMP claim. */
#include <aros/debug.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <aros/asmcall.h>
#include <exec/errors.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/alib.h>
#include <hidd/pci.h>
#include <utility/hooks.h>
#include <string.h>
#include "device.h"
#include "vsock_transport.h"
#include LC_LIBDEFS_FILE
static void finish(struct BridgeBase *b,struct IOStdReq *r,struct List *queue)
{
    Disable();
    /* Once bytes moved, report that partial completion: an aborted result
     * could make callers discard consumed input or resend accepted output. */
    if(b->active_abort && !r->io_Actual)r->io_Error=IOERR_ABORTED;
    b->active=NULL;b->active_abort=FALSE;
    if(queue && !r->io_Error)AddTail(queue,(struct Node *)r);
    else ReplyMsg(&r->io_Message);
    Enable();
}
static void cancel_list(struct List *list)
{
    struct IOStdReq *r;
    while((r=(APTR)RemHead(list))) { r->io_Error=IOERR_ABORTED;ReplyMsg(&r->io_Message); }
}
static void cancel_all(struct BridgeBase *b)
{
    Disable();if(b->input)cancel_list(&b->input->mp_MsgList);
    cancel_list(&b->reads);cancel_list(&b->writes);Enable();
}
static void service(struct BridgeBase *b,struct IOStdReq *r)
{
    struct List *queue=NULL;r->io_Actual=0;
    if(b->failed || b->stream.failed)r->io_Error=IOERR_OPENFAIL;
    else if(r->io_Command==CMD_UPDATE) {
        LONG ready=VirtioVsockPollTX(b->socket);
        if(ready<0)r->io_Error=IOERR_OPENFAIL;
        else if(b->stream.tx_count || !ready)queue=&b->writes;
    }
    else if(r->io_Command!=CMD_READ && r->io_Command!=CMD_WRITE)r->io_Error=IOERR_NOCMD;
    else if(r->io_Length && !r->io_Data)r->io_Error=IOERR_BADADDRESS;
    else if(r->io_Length) {
        if(r->io_Command==CMD_READ) {
            r->io_Actual=VsockStreamRead(&b->stream,r->io_Data,r->io_Length);
            if(!r->io_Actual && b->stream.state!=VS_CLOSED)queue=&b->reads;
        } else if(b->stream.state!=VS_OPEN || (b->stream.peer_shutdown&1))r->io_Error=IOERR_OPENFAIL;
        else {
            r->io_Actual=VsockStreamWrite(&b->stream,r->io_Data,r->io_Length);
            if(!r->io_Actual)queue=&b->writes;
        }
    }
    finish(b,r,queue);
}
static void service_queue(struct BridgeBase *b,struct List *list)
{
    /* Service only the requests present on entry: blocked requests requeue. */
    unsigned count=0;struct Node *n;
    Disable();for(n=list->lh_Head;n->ln_Succ && count<32;n=n->ln_Succ)++count;Enable();
    while(count--) {
        Disable();struct IOStdReq *r=(APTR)RemHead(list);
        if(r) { b->active=r;b->active_abort=FALSE; }
        Enable();if(!r)break;service(b,r);
    }
}
static int emit(void *context,const uint8_t *p,size_t n)
{ struct BridgeBase *b=context;return VirtioVsockSend(b->socket,p,n); }
static BOOL pump(struct BridgeBase *b)
{
    for(unsigned i=0;i<8 && !b->reject_pending;++i) {
        LONG n=VirtioVsockReceive(b->socket,b->packet,sizeof(b->packet));
        if(n<0)return FALSE;if(!n)break;
        int result=VsockStreamFeed(&b->stream,b->packet,n);
        if(result<0) {
            /* Single stream device: retain packet for a bounded RST response.
             * No dynamic flow table or unbounded allocation. */
            struct VirtioVsockHeader h;memcpy(&h,b->packet,sizeof(h));
            if(h.op==3)continue;
            UQUAD cid=h.src_cid;ULONG port=h.src_port;
            h.src_cid=VirtioVsockCID(b->socket);h.dst_cid=cid;
            h.src_port=h.dst_port;h.dst_port=port;h.op=3;h.len=0;h.flags=0;h.buf_alloc=0;h.fwd_cnt=0;
            memcpy(b->reject,&h,sizeof(h));b->reject_pending=TRUE;
        } else if(!result)return FALSE;
    }
    service_queue(b,&b->reads);service_queue(b,&b->writes);
    if(b->reject_pending) {
        LONG result=VirtioVsockSend(b->socket,b->reject,sizeof(b->reject));
        if(result<0)return FALSE;if(!result)return TRUE;
        b->reject_pending=FALSE;
    }
    return VsockStreamPump(&b->stream,emit,b)>=0 && !b->stream.failed;
}
static AROS_INTH1(bridge_irq,struct BridgeBase *,b)
{
    AROS_INTFUNC_INIT
    (void)__ufi_intmask;(void)__ufi_custom;(void)__ufi_code;(void)SysBase;
    (void)VirtioVsockInterruptStatus(b->socket);++b->irq_hits;
    Signal(b->worker,b->irq_mask);return TRUE;
    AROS_INTFUNC_EXIT
}
AROS_UFH3(static IPTR,discover,AROS_UFHA(struct Hook *,hook,A0),AROS_UFHA(OOP_Object *,object,A2),AROS_UFHA(APTR,message,A1))
{
    AROS_USERFUNC_INIT
    struct BridgeBase *b=hook->h_Data;struct Library *OOPBase=b->oop;IPTR vendor=0,product=0;(void)message;
    OOP_GetAttr(object,b->pci_attr+aoHidd_PCIDevice_VendorID,&vendor);
    OOP_GetAttr(object,b->pci_attr+aoHidd_PCIDevice_ProductID,&product);
    if(!b->pci_device && vendor==0x1af4 && product==0x1053) {
        struct pHidd_PCIDevice_Obtain take={OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,moHidd_PCIDevice_Obtain),(CONST_STRPTR)"vmbridge.device"};
        if(!OOP_DoMethod(object,(OOP_Msg)&take))b->pci_device=object;
    }
    return 0;AROS_USERFUNC_EXIT
}
static BOOL bind(struct BridgeBase *b)
{
    struct Library *OOPBase=b->oop;
    b->pci_attr=OOP_ObtainAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    b->pci=OOP_NewObject(NULL,(CONST_STRPTR)CLID_Hidd_PCI,NULL);
    if(!b->pci_attr || !b->pci)return FALSE;
    struct Hook hook={0};hook.h_Entry=(HOOKFUNC)discover;hook.h_Data=b;
    struct pHidd_PCI_EnumDevices e={OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCI,moHidd_PCI_EnumDevices),&hook,NULL};
    OOP_DoMethod(b->pci,(OOP_Msg)&e);
    return b->pci_device && VirtioVsockOpen(b->oop,b->pci_device,&b->socket);
}
static void unbind(struct BridgeBase *b)
{
    struct Library *OOPBase=b->oop;
    if(b->pci_device) {
        struct pHidd_PCIDevice_Release r={OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,moHidd_PCIDevice_Release)};
        OOP_DoMethod(b->pci_device,(OOP_Msg)&r);b->pci_device=NULL;
    }
    if(b->pci) { OOP_DisposeObject(b->pci);b->pci=NULL; }
    if(b->pci_attr) { OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);b->pci_attr=0; }
}
static void worker(struct BridgeBase *b,struct BridgeControl *startup)
{
    struct MsgPort *input=CreateMsgPort(),*control=CreateMsgPort(),*tp=CreateMsgPort();
    struct timerequest *timer=tp?(APTR)CreateIORequest(tp,sizeof(*timer)):NULL;
    BOOL timeropen=FALSE,pending=FALSE;BYTE irqbit=AllocSignal(-1);unsigned seconds=0,close_deadline=0;
    struct BridgeControl *ending=startup;
    b->worker=FindTask(NULL);b->failed=FALSE;b->closing=FALSE;b->irq_hits=0;b->reject_pending=FALSE;
    if(timer && !OpenDevice((CONST_STRPTR)"timer.device",UNIT_VBLANK,(struct IORequest *)timer,0))timeropen=TRUE;
    if(!input || !control || !timeropen || irqbit<0 || !bind(b))goto stop;
    b->irq_mask=1UL<<irqbit;b->interrupt.is_Node.ln_Type=NT_INTERRUPT;
    b->interrupt.is_Node.ln_Name="vmbridge.device";b->interrupt.is_Data=b;b->interrupt.is_Code=(APTR)bridge_irq;
    if(!VirtioVsockEnableInterrupt(b->socket,&b->interrupt))goto stop;
    if(!b->next_port || !VsockStreamInit(&b->stream,VirtioVsockCID(b->socket),b->next_port++,2,b->port))goto stop;
    b->input=input;b->control=control;
    for(;;) {
        if(!b->failed && !pump(b)) { b->failed=TRUE;cancel_all(b); }
        if(close_deadline && (b->failed || b->stream.state==VS_CLOSED || seconds>=close_deadline)) {
            if(seconds>=close_deadline && b->stream.state!=VS_CLOSED)
                bug("[VMBridge] close drain deadline reached\n");
            goto stop;
        }
        if(ending==startup) {
            if(b->failed || seconds>=5)goto stop;
            if(b->stream.state==VS_OPEN) { startup->okay=TRUE;ReplyMsg(&startup->message);ending=NULL; }
        }
        if(!pending) {
            timer->tr_node.io_Command=TR_ADDREQUEST;timer->tr_time.tv_secs=1;timer->tr_time.tv_micro=0;
            SendIO((struct IORequest *)timer);pending=TRUE;
        }
        Wait((1UL<<input->mp_SigBit)|(1UL<<control->mp_SigBit)|(1UL<<tp->mp_SigBit)|b->irq_mask);
        if(pending && CheckIO((struct IORequest *)timer)) { WaitIO((struct IORequest *)timer);pending=FALSE;++seconds; }
        struct BridgeControl *close=(APTR)GetMsg(control);
        if(close) {
            ending=close;cancel_all(b);close_deadline=seconds+3;
            VsockStreamShutdown(&b->stream);
            continue;
        }
        for(unsigned n=0;n<32;++n) {
            Disable();struct IOStdReq *r=(APTR)GetMsg(input);
            if(r) { b->active=r;b->active_abort=FALSE; }
            Enable();if(!r)break;service(b,r);
        }
        if(input->mp_MsgList.lh_Head->ln_Succ)Signal(b->worker,1UL<<input->mp_SigBit);
    }
stop:
    cancel_all(b);
    if(pending) { AbortIO((struct IORequest *)timer);WaitIO((struct IORequest *)timer); }
    if(timeropen)CloseDevice((struct IORequest *)timer);
    if(timer)DeleteIORequest((struct IORequest *)timer);if(tp)DeleteMsgPort(tp);
    if(!VirtioVsockClose(b->socket)) {
        b->failed=TRUE;if(ending)ReplyMsg(&ending->message);
        for(;;)Wait(0); /* pin DMA, callback, PCI, task and module */
    }
    b->socket=NULL;unbind(b);if(irqbit>=0)FreeSignal(irqbit);
    Disable();b->input=b->control=NULL;Enable();
    if(input)DeleteMsgPort(input);if(control)DeleteMsgPort(control);
    bug("[VMBridge] closed IRQ completions=%lu\n",(unsigned long)b->irq_hits);
    Forbid();b->worker=NULL;if(ending)ReplyMsg(&ending->message);RemTask(NULL);
}
static int bridge_init(LIBBASETYPEPTR b)
{
    b->next_port=49152;InitSemaphore(&b->lifecycle);NEWLIST(&b->reads);NEWLIST(&b->writes);
    b->oop=OpenLibrary((CONST_STRPTR)"oop.library",0);return b->oop!=NULL;
}
static int bridge_expunge(LIBBASETYPEPTR b)
{ if(b->worker)return FALSE;if(b->oop) { CloseLibrary(b->oop);b->oop=NULL; }return TRUE; }
static int bridge_open(LIBBASETYPEPTR b,struct IOStdReq *r,ULONG unit,ULONG flags)
{
    if(r->io_Message.mn_Length<sizeof(*r) || !unit || flags)return FALSE;
    BOOL okay=FALSE;r->io_Unit=NULL;ObtainSemaphore(&b->lifecycle);
    if(b->worker || b->opened)goto done;
    struct MsgPort *reply=CreateMsgPort();struct BridgeControl startup={0};
    if(!reply)goto done;b->port=unit;
    startup.message.mn_ReplyPort=reply;startup.message.mn_Length=sizeof(startup);
    if(NewCreateTask(TASKTAG_NAME,"VM host stream",TASKTAG_PC,worker,TASKTAG_PRI,5,
                     TASKTAG_STACKSIZE,65536,TASKTAG_ARG1,b,TASKTAG_ARG2,&startup,TAG_END)) {
        WaitPort(reply);GetMsg(reply);okay=startup.okay;
    }
    DeleteMsgPort(reply);
    if(okay) { b->opened=TRUE;r->io_Unit=&b->unit;r->io_Error=0; }
done:ReleaseSemaphore(&b->lifecycle);return okay;
}
static int bridge_close(LIBBASETYPEPTR b,struct IOStdReq *r)
{
    ObtainSemaphore(&b->lifecycle);
    if(b->opened && r->io_Unit==&b->unit) {
        struct MsgPort reply={0};struct BridgeControl close={0};
        reply.mp_Node.ln_Type=NT_MSGPORT;reply.mp_Flags=PA_SIGNAL;reply.mp_SigBit=SIGB_SINGLE;
        reply.mp_SigTask=FindTask(NULL);NEWLIST(&reply.mp_MsgList);SetSignal(0,SIGF_SINGLE);
        close.message.mn_ReplyPort=&reply;close.message.mn_Length=sizeof(close);
        b->closing=TRUE;PutMsg(b->control,&close.message);WaitPort(&reply);GetMsg(&reply);
        b->opened=FALSE;r->io_Unit=NULL;
    }
    ReleaseSemaphore(&b->lifecycle);return TRUE;
}
AROS_LH1(void,BridgeBeginIO,AROS_LHA(struct IOStdReq *,r,A1),LIBBASETYPEPTR,b,5,vmbridge_device)
{
    AROS_LIBFUNC_INIT
    r->io_Error=0;r->io_Actual=0;
    if(!b->input || !b->opened || b->closing || r->io_Unit!=&b->unit) {
        r->io_Error=IOERR_OPENFAIL;if(!(r->io_Flags&IOF_QUICK))ReplyMsg(&r->io_Message);
    } else { r->io_Flags&=~IOF_QUICK;PutMsg(b->input,&r->io_Message); }
    AROS_LIBFUNC_EXIT
}
AROS_LH1(LONG,BridgeAbortIO,AROS_LHA(struct IOStdReq *,r,A1),LIBBASETYPEPTR,b,6,vmbridge_device)
{
    AROS_LIBFUNC_INIT
    Disable();
    if(b->active==r)b->active_abort=TRUE;
    else {
        struct List *lists[]={b->input?&b->input->mp_MsgList:NULL,&b->reads,&b->writes};
        for(unsigned i=0;i<3;++i)if(lists[i]) {
            struct Node *n;for(n=lists[i]->lh_Head;n->ln_Succ;n=n->ln_Succ)if(n==(struct Node *)r) {
                Remove(n);r->io_Error=IOERR_ABORTED;ReplyMsg(&r->io_Message);goto done;
            }
        }
    }
done:Enable();return 0;AROS_LIBFUNC_EXIT
}
ADD2INITLIB(bridge_init,0)
ADD2EXPUNGELIB(bridge_expunge,0)
ADD2OPENDEV(bridge_open,0)
ADD2CLOSEDEV(bridge_close,0)
