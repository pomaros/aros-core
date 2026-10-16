/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1.

   Resident SANA-II frontend over the shared virtio-net transport. Exec task,
   message and opener conventions follow the AROS APL tap/dwmac drivers.
   One worker owns the transport. BeginIO only queues. AbortIO never waits for
   a semaphore, invokes callbacks, or releases DMA. The current PCI IRQ
   provider and task-exit handshake are uniprocessor contracts, not SMP proof.
*/
#include <aros/debug.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <aros/asmcall.h>
#include <exec/errors.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <devices/newstyle.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/oop.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <hidd/pci.h>
#include <string.h>
#include "device.h"
#include "net_transport.h"
#include LC_LIBDEFS_FILE
#undef UtilityBase

static void finish(struct NetBase *base,struct IOSana2Req *req,struct List *queue)
{
    Disable();
    if(base->active_abort)req->ios2_Req.io_Error=IOERR_ABORTED;
    base->active=NULL;base->active_abort=FALSE;
    if(queue && !req->ios2_Req.io_Error)AddTail(queue,(struct Node *)req);
    else ReplyMsg(&req->ios2_Req.io_Message);
    Enable();
}
static void abort_request(struct IOSana2Req *req)
{
    req->ios2_Req.io_Error=IOERR_ABORTED;
    if(req->ios2_Req.io_Command!=NSCMD_DEVICEQUERY)
        req->ios2_WireError=S2WERR_GENERIC_ERROR;
    ReplyMsg(&req->ios2_Req.io_Message);
}
/* Caller masks interrupts. The worker alone enters or leaves active state. */
static void cancel_list(struct List *list,struct NetOpener *opener)
{
    struct Node *node=list->lh_Head;
    while(node->ln_Succ) {
        struct Node *next=node->ln_Succ;struct IOSana2Req *req=(APTR)node;
        if(!opener || req->ios2_BufferManagement==opener) { Remove(node);abort_request(req); }
        node=next;
    }
}
static void cancel_owner(struct NetBase *base,struct NetOpener *opener)
{
    Disable();
    cancel_list(&base->input->mp_MsgList,opener);
    cancel_list(&base->reads,opener);cancel_list(&base->writes,opener);cancel_list(&base->events,opener);
    if(base->tx_request && (!opener || base->tx_request->ios2_BufferManagement==opener)) {
        struct IOSana2Req *req=base->tx_request;base->tx_request=NULL;abort_request(req);
    }
    Enable();
}
static void event(struct NetBase *base,ULONG mask)
{
    Disable();
    struct Node *node=base->events.lh_Head;
    while(node->ln_Succ) {
        struct Node *next=node->ln_Succ;struct IOSana2Req *req=(APTR)node;
        if(req->ios2_WireError&mask) {
            Remove(node);req->ios2_WireError&=mask;ReplyMsg(&req->ios2_Req.io_Message);
        }
        node=next;
    }
    Enable();
}
static BOOL valid_opener(struct NetBase *base,APTR cookie)
{
    struct NetOpener *o;
    ForeachNode(&base->openers,o)if(o==cookie)return TRUE;
    return FALSE;
}
static const UWORD commands[]={CMD_READ,CMD_WRITE,CMD_FLUSH,S2_DEVICEQUERY,
    S2_GETSTATIONADDRESS,S2_CONFIGINTERFACE,S2_ONLINE,S2_OFFLINE,S2_BROADCAST,
    S2_GETGLOBALSTATS,S2_GETSPECIALSTATS,S2_ONEVENT,NSCMD_DEVICEQUERY,0};
static struct List *handle(struct NetBase *base,struct IOSana2Req *req)
{
    UWORD cmd=req->ios2_Req.io_Command;
    if(cmd==NSCMD_DEVICEQUERY) {
        struct IOStdReq *io=(APTR)req;struct NSDeviceQueryResult *query=io->io_Data;
        if(!query)io->io_Error=IOERR_BADADDRESS;
        else if(io->io_Length<sizeof(*query) || query->DevQueryFormat || query->SizeAvailable)io->io_Error=IOERR_BADLENGTH;
        else { query->SizeAvailable=sizeof(*query);query->DeviceType=NSDEVTYPE_SANA2;
            query->DeviceSubType=0;query->SupportedCommands=(UWORD *)commands;io->io_Actual=sizeof(*query); }
        return NULL;
    }
    ULONG requested=req->ios2_WireError;req->ios2_WireError=0;
    if(!valid_opener(base,req->ios2_BufferManagement)) { req->ios2_Req.io_Error=S2ERR_BAD_ARGUMENT;return NULL; }
    struct NetOpener *opener=req->ios2_BufferManagement;
    switch(cmd) {
    case S2_DEVICEQUERY: {
        struct Sana2DeviceQuery *q=req->ios2_StatData;
        if(!q || q->SizeAvailable<2*sizeof(ULONG)) { req->ios2_Req.io_Error=S2ERR_BAD_ARGUMENT;break; }
        ULONG available=q->SizeAvailable,bytes=available<sizeof(*q)?available:sizeof(*q);
        struct Sana2DeviceQuery result={0};result.SizeAvailable=available;result.SizeSupplied=bytes;
        result.AddrFieldSize=48;result.MTU=base->mtu;result.BPS=1000000000;
        result.HardwareType=S2WireType_Ethernet;memcpy(q,&result,bytes);break;
    }
    case S2_GETSTATIONADDRESS:
        memcpy(req->ios2_SrcAddr,base->mac,6);memcpy(req->ios2_DstAddr,base->mac,6);break;
    case S2_CONFIGINTERFACE:
        if(base->configured) { req->ios2_Req.io_Error=S2ERR_BAD_STATE;req->ios2_WireError=S2WERR_IS_CONFIGURED; }
        else if(memcmp(req->ios2_SrcAddr,base->mac,6)) { req->ios2_Req.io_Error=S2ERR_BAD_ADDRESS;req->ios2_WireError=S2WERR_SRC_ADDRESS; }
        else { base->configured=TRUE;++base->stats.Reconfigurations; }
        break;
    case S2_ONLINE:
        if(!base->configured || base->failed) { req->ios2_Req.io_Error=S2ERR_OUTOFSERVICE;req->ios2_WireError=S2WERR_NOT_CONFIGURED; }
        else if(base->online) { req->ios2_Req.io_Error=S2ERR_BAD_STATE;req->ios2_WireError=S2WERR_UNIT_ONLINE; }
        else { base->online=TRUE;event(base,S2EVENT_ONLINE); }
        break;
    case S2_OFFLINE:
        base->online=FALSE;event(base,S2EVENT_OFFLINE);cancel_owner(base,NULL);break;
    case CMD_FLUSH:cancel_owner(base,NULL);break;
    case CMD_READ:
        if(!base->online)goto offline;
        if(!opener->rx || !req->ios2_Data)goto bad_buffer;
        return &base->reads;
    case S2_BROADCAST:memset(req->ios2_DstAddr,255,6); /* fall through */
    case CMD_WRITE:
        if(!base->online)goto offline;
        if(!opener->tx || !req->ios2_Data)goto bad_buffer;
        if(req->ios2_DataLength>base->mtu+((req->ios2_Req.io_Flags&SANA2IOF_RAW)?14:0) ||
           ((req->ios2_Req.io_Flags&SANA2IOF_RAW) && req->ios2_DataLength<14)) {
            req->ios2_Req.io_Error=S2ERR_MTU_EXCEEDED;break;
        }
        return &base->writes;
    case S2_GETGLOBALSTATS:
        if(!req->ios2_StatData)goto bad_buffer;
        memcpy(req->ios2_StatData,&base->stats,sizeof(base->stats));break;
    case S2_GETSPECIALSTATS:
        if(!req->ios2_StatData)goto bad_buffer;
        ((struct Sana2SpecialStatHeader *)req->ios2_StatData)->RecordCountSupplied=0;break;
    case S2_ONEVENT:
        if(!requested || (requested&~(S2EVENT_ERROR|S2EVENT_TX|S2EVENT_RX|S2EVENT_ONLINE|S2EVENT_OFFLINE|S2EVENT_BUFF|S2EVENT_HARDWARE|S2EVENT_SOFTWARE))) {
            req->ios2_Req.io_Error=S2ERR_NOT_SUPPORTED;req->ios2_WireError=S2WERR_BAD_EVENT;break;
        }
        req->ios2_WireError=requested;
        if(requested&(base->online?S2EVENT_ONLINE:S2EVENT_OFFLINE))req->ios2_WireError=base->online?S2EVENT_ONLINE:S2EVENT_OFFLINE;
        else return &base->events;
        break;
    default:req->ios2_Req.io_Error=S2ERR_NOT_SUPPORTED;
    }
    return NULL;
offline:req->ios2_Req.io_Error=S2ERR_OUTOFSERVICE;req->ios2_WireError=S2WERR_UNIT_OFFLINE;return NULL;
bad_buffer:req->ios2_Req.io_Error=S2ERR_BAD_ARGUMENT;req->ios2_WireError=S2WERR_BUFF_ERROR;return NULL;
}
static void receive_frame(struct NetBase *base,ULONG bytes)
{
    struct UtilityBase *UtilityBase=(APTR)base->utility;
    if(!base->online)return;
    UBYTE *frame=base->rx_frame;ULONG type=((ULONG)frame[12]<<8)|frame[13];
    ++base->stats.PacketsReceived;BOOL delivered=FALSE;
    struct NetOpener *opener;
    ForeachNode(&base->openers,opener) {
        struct IOSana2Req *req=NULL;struct Node *node;
        Disable();
        for(node=base->reads.lh_Head;node->ln_Succ;node=node->ln_Succ) {
            struct IOSana2Req *candidate=(APTR)node;
            if(candidate->ios2_BufferManagement==opener && candidate->ios2_PacketType==type) { req=candidate;Remove(node);break; }
        }
        if(req) { base->active=req;base->active_abort=FALSE; }
        Enable();if(!req)continue;
        ULONG offset=(req->ios2_Req.io_Flags&SANA2IOF_RAW)?0:14;
        memcpy(req->ios2_SrcAddr,frame+6,6);memcpy(req->ios2_DstAddr,frame,6);
        req->ios2_DataLength=bytes-offset;
        req->ios2_Req.io_Flags&=~(SANA2IOF_BCAST|SANA2IOF_MCAST);
        if(!memcmp(frame,"\xff\xff\xff\xff\xff\xff",6))req->ios2_Req.io_Flags|=SANA2IOF_BCAST;
        else if(frame[0]&1)req->ios2_Req.io_Flags|=SANA2IOF_MCAST;
        if(opener->filter && !CallHookPkt(opener->filter,req,frame+offset)) { finish(base,req,&base->reads);continue; }
        if(!opener->rx(req->ios2_Data,frame+offset,bytes-offset)) {
            req->ios2_Req.io_Error=S2ERR_NO_RESOURCES;req->ios2_WireError=S2WERR_BUFF_ERROR;
        } else delivered=TRUE;
        finish(base,req,NULL);
    }
    if(!delivered)++base->stats.UnknownTypesReceived;
    event(base,S2EVENT_RX);
}
static void pump(struct NetBase *base)
{
    LONG tx=VirtioNetPollTX(base->net);
    if(tx<0)goto fault;
    if(tx && base->tx_pending) {
        Disable();base->tx_pending=FALSE;++base->stats.PacketsSent;
        struct IOSana2Req *req=base->tx_request;base->tx_request=NULL;
        if(req)ReplyMsg(&req->ios2_Req.io_Message);
        Enable();event(base,S2EVENT_TX);
    }
    if(tx && base->online) {
        Disable();struct IOSana2Req *req=(APTR)RemHead(&base->writes);
        if(req) { base->active=req;base->active_abort=FALSE; }
        Enable();
        if(req) {
            struct NetOpener *opener=req->ios2_BufferManagement;
            ULONG offset=(req->ios2_Req.io_Flags&SANA2IOF_RAW)?0:14;
            if(offset) { memcpy(base->tx_frame,req->ios2_DstAddr,6);memcpy(base->tx_frame+6,base->mac,6);
                base->tx_frame[12]=req->ios2_PacketType>>8;base->tx_frame[13]=req->ios2_PacketType; }
            BOOL copied=opener->tx(base->tx_frame+offset,req->ios2_Data,req->ios2_DataLength);
            Disable();
            LONG sent=(!copied || base->active_abort)?-1:VirtioNetSend(base->net,base->tx_frame,req->ios2_DataLength+offset);
            if(sent==1) { base->tx_pending=TRUE;base->tx_request=req;base->active=NULL; }
            else { req->ios2_Req.io_Error=copied?S2ERR_TX_FAILURE:S2ERR_NO_RESOURCES;finish(base,req,NULL); }
            Enable();
        }
    }
    for(unsigned n=0;n<32;++n) {
        LONG bytes=VirtioNetReceive(base->net,base->rx_frame,sizeof(base->rx_frame));
        if(bytes<0)goto fault;
        if(!bytes)return;
        receive_frame(base,bytes);
    }
    Signal(base->worker,base->irq_mask);return;
fault:
    base->failed=TRUE;base->online=FALSE;event(base,S2EVENT_ERROR|S2EVENT_HARDWARE|S2EVENT_OFFLINE);
    cancel_owner(base,NULL);
}
static AROS_INTH1(net_irq,struct NetBase *,base)
{
    AROS_INTFUNC_INIT
    (void)__ufi_intmask;(void)__ufi_custom;(void)__ufi_code;(void)SysBase;
    (void)VirtioNetInterruptStatus(base->net);
    Signal(base->worker,base->irq_mask);return TRUE;
    AROS_INTFUNC_EXIT
}
AROS_UFH3(static IPTR, discover,
    AROS_UFHA(struct Hook *,hook,A0),AROS_UFHA(OOP_Object *,object,A2),AROS_UFHA(APTR,message,A1))
{
    AROS_USERFUNC_INIT
    struct NetBase *base=hook->h_Data;struct Library *OOPBase=base->oop;
    IPTR vendor=0,product=0;(void)message;
    OOP_GetAttr(object,base->pci_attr+aoHidd_PCIDevice_VendorID,&vendor);
    OOP_GetAttr(object,base->pci_attr+aoHidd_PCIDevice_ProductID,&product);
    if(!base->pci_device && vendor==0x1af4 && product==0x1041) {
        struct pHidd_PCIDevice_Obtain take={OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,moHidd_PCIDevice_Obtain),(CONST_STRPTR)"virtionet.device"};
        if(!OOP_DoMethod(object,(OOP_Msg)&take))base->pci_device=object;
    }
    return 0;
    AROS_USERFUNC_EXIT
}
static BOOL bind(struct NetBase *base)
{
    struct Library *OOPBase=base->oop;
    base->pci_attr=OOP_ObtainAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);
    base->pci=OOP_NewObject(NULL,(CONST_STRPTR)CLID_Hidd_PCI,NULL);
    if(!base->pci_attr || !base->pci)return FALSE;
    struct Hook hook={0};hook.h_Entry=(HOOKFUNC)discover;hook.h_Data=base;
    struct pHidd_PCI_EnumDevices enumerate={OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCI,moHidd_PCI_EnumDevices),&hook,NULL};
    OOP_DoMethod(base->pci,(OOP_Msg)&enumerate);
    return base->pci_device && VirtioNetOpen(base->oop,base->pci_device,&base->net);
}
static void unbind(struct NetBase *base)
{
    struct Library *OOPBase=base->oop;
    if(base->pci_device) {
        struct pHidd_PCIDevice_Release release={OOP_GetMethodID((CONST_STRPTR)IID_Hidd_PCIDevice,moHidd_PCIDevice_Release)};
        OOP_DoMethod(base->pci_device,(OOP_Msg)&release);base->pci_device=NULL;
    }
    if(base->pci) { OOP_DisposeObject(base->pci);base->pci=NULL; }
    if(base->pci_attr) { OOP_ReleaseAttrBase((CONST_STRPTR)IID_Hidd_PCIDevice);base->pci_attr=0; }
}
static void worker(struct NetBase *base,struct NetControl *startup)
{
    struct MsgPort *input=CreateMsgPort(),*control=CreateMsgPort(),*timerport=CreateMsgPort();
    struct timerequest *timer=timerport?(APTR)CreateIORequest(timerport,sizeof(*timer)):NULL;
    BOOL timeropen=FALSE,timerpending=FALSE;BYTE irqbit=AllocSignal(-1);
    struct NetControl *ending=startup;
    base->worker=FindTask(NULL);base->failed=FALSE;base->configured=FALSE;base->online=FALSE;
    if(timer && !OpenDevice((CONST_STRPTR)"timer.device",UNIT_VBLANK,(struct IORequest *)timer,0))timeropen=TRUE;
    if(!input || !control || !timeropen || irqbit<0 || !bind(base))goto stop;
    base->irq_mask=1UL<<irqbit;base->interrupt.is_Node.ln_Type=NT_INTERRUPT;
    base->interrupt.is_Node.ln_Name="virtionet.device";base->interrupt.is_Data=base;base->interrupt.is_Code=(APTR)net_irq;
    if(!VirtioNetEnableInterrupt(base->net,&base->interrupt))goto stop;
    VirtioNetMAC(base->net,base->mac);base->mtu=VirtioNetMTU(base->net);
    base->input=input;base->control=control;
    startup->okay=TRUE;ReplyMsg(&startup->message);ending=NULL;
    for(;;) {
        if(!timerpending) {
            timer->tr_node.io_Command=TR_ADDREQUEST;timer->tr_time.tv_secs=1;timer->tr_time.tv_micro=0;
            SendIO((struct IORequest *)timer);timerpending=TRUE;
        }
        Wait((1UL<<input->mp_SigBit)|(1UL<<control->mp_SigBit)|(1UL<<timerport->mp_SigBit)|base->irq_mask);
        if(timerpending && CheckIO((struct IORequest *)timer)) { WaitIO((struct IORequest *)timer);timerpending=FALSE; }
        struct NetControl *cmd;
        while((cmd=(APTR)GetMsg(control))) {
            cancel_owner(base,cmd->opener);Remove((struct Node *)cmd->opener);
            if(cmd->stop) { ending=cmd;goto stop; }
            cmd->okay=TRUE;ReplyMsg(&cmd->message);
        }
        for(unsigned n=0;n<32;++n) {
            Disable();struct IOSana2Req *req=(APTR)GetMsg(input);
            if(req) { base->active=req;base->active_abort=FALSE; }
            Enable();if(!req)break;
            struct List *queue=handle(base,req);finish(base,req,queue);
        }
        if(!base->failed)pump(base);
        if(input->mp_MsgList.lh_Head->ln_Succ)Signal(base->worker,1UL<<input->mp_SigBit);
    }
stop:
    if(timerpending) { AbortIO((struct IORequest *)timer);WaitIO((struct IORequest *)timer); }
    if(timeropen)CloseDevice((struct IORequest *)timer);
    if(timer)DeleteIORequest((struct IORequest *)timer);
    if(timerport)DeleteMsgPort(timerport);
    if(!VirtioNetClose(base->net)) {
        base->failed=TRUE;
        if(ending) { ending->okay=FALSE;ReplyMsg(&ending->message); }
        /* Keep task, signal, callback, PCI provider and module pinned. */
        for(;;)Wait(0);
    }
    base->net=NULL;unbind(base);
    if(irqbit>=0)FreeSignal(irqbit);
    if(input)DeleteMsgPort(input);
    if(control)DeleteMsgPort(control);
    base->input=base->control=NULL;base->tx_request=NULL;base->tx_pending=FALSE;base->failed=FALSE;
    /* UP: caller cannot resume and expunge this code until RemTask exits. */
    Forbid();base->worker=NULL;
    if(ending)ReplyMsg(&ending->message);
    RemTask(NULL);
}
static int net_init(LIBBASETYPEPTR base)
{
    InitSemaphore(&base->lifecycle);NEWLIST(&base->openers);
    NEWLIST(&base->reads);NEWLIST(&base->writes);NEWLIST(&base->events);
    base->oop=OpenLibrary((CONST_STRPTR)"oop.library",0);
    base->utility=OpenLibrary((CONST_STRPTR)"utility.library",0);
    if(base->oop && base->utility)return TRUE;
    if(base->utility) { CloseLibrary(base->utility);base->utility=NULL; }
    if(base->oop) { CloseLibrary(base->oop);base->oop=NULL; }
    return FALSE;
}
static int net_expunge(LIBBASETYPEPTR base)
{
    if(base->worker)return FALSE;
    if(base->utility) { CloseLibrary(base->utility);base->utility=NULL; }
    if(base->oop) { CloseLibrary(base->oop);base->oop=NULL; }
    return TRUE;
}
static int net_open(LIBBASETYPEPTR base,struct IOSana2Req *req,ULONG unit,ULONG flags)
{
    if(req->ios2_Req.io_Message.mn_Length<sizeof(*req) || unit || flags)return FALSE;
    struct UtilityBase *UtilityBase=(APTR)base->utility;
    struct TagItem *tags=req->ios2_BufferManagement;
    req->ios2_BufferManagement=NULL;req->ios2_Req.io_Unit=NULL;
    struct NetOpener *opener=AllocVec(sizeof(*opener),MEMF_CLEAR|MEMF_PUBLIC);
    if(!opener)return FALSE;
    opener->rx=(APTR)GetTagData(S2_CopyToBuff,0,tags);
    opener->rx=(APTR)GetTagData(S2_CopyToBuff16,(IPTR)opener->rx,tags);
    opener->rx=(APTR)GetTagData(S2_CopyToBuff32,(IPTR)opener->rx,tags);
    opener->tx=(APTR)GetTagData(S2_CopyFromBuff,0,tags);
    opener->tx=(APTR)GetTagData(S2_CopyFromBuff16,(IPTR)opener->tx,tags);
    opener->tx=(APTR)GetTagData(S2_CopyFromBuff32,(IPTR)opener->tx,tags);
    opener->filter=(APTR)GetTagData(S2_PacketFilter,0,tags);
    BOOL okay=FALSE;ObtainSemaphore(&base->lifecycle);
    if(base->failed)goto done;
    if(!base->worker) {
        struct MsgPort *port=CreateMsgPort();struct NetControl startup={0};
        if(!port)goto done;
        startup.message.mn_ReplyPort=port;startup.message.mn_Length=sizeof(startup);
        if(NewCreateTask(TASKTAG_NAME,"virtionet IO",TASKTAG_PC,worker,TASKTAG_PRI,5,
                         TASKTAG_STACKSIZE,65536,TASKTAG_ARG1,base,TASKTAG_ARG2,&startup,TAG_END)) {
            WaitPort(port);GetMsg(port);okay=startup.okay;
        }
        DeleteMsgPort(port);if(!okay)goto done;
    }
    Disable();AddTail((struct List *)&base->openers,(struct Node *)opener);Enable();
    ++base->open_count;req->ios2_BufferManagement=opener;req->ios2_Req.io_Unit=&base->unit;
    req->ios2_Req.io_Error=0;okay=TRUE;
done:
    ReleaseSemaphore(&base->lifecycle);if(!okay)FreeVec(opener);return okay;
}
static int net_close(LIBBASETYPEPTR base,struct IOSana2Req *req)
{
    struct NetOpener *opener=req->ios2_BufferManagement;
    if(!opener)return TRUE;
    ObtainSemaphore(&base->lifecycle);
    /* Use the closing task's signal bit with an embedded reply port: Close
     * must still be able to retire the opener under allocation pressure. */
    struct MsgPort reply={0};struct NetControl cmd={0};
    reply.mp_Node.ln_Type=NT_MSGPORT;reply.mp_Flags=PA_SIGNAL;reply.mp_SigBit=SIGB_SINGLE;
    reply.mp_SigTask=FindTask(NULL);NEWLIST(&reply.mp_MsgList);SetSignal(0,SIGF_SINGLE);
    cmd.message.mn_ReplyPort=&reply;cmd.message.mn_Length=sizeof(cmd);cmd.opener=opener;cmd.stop=base->open_count==1;
    PutMsg(base->control,&cmd.message);WaitPort(&reply);GetMsg(&reply);
    --base->open_count;FreeVec(opener);req->ios2_BufferManagement=NULL;req->ios2_Req.io_Unit=NULL;
    ReleaseSemaphore(&base->lifecycle);return TRUE;
}
AROS_LH1(void,NetBeginIO,AROS_LHA(struct IOSana2Req *,req,A1),LIBBASETYPEPTR,base,5,virtionet_device)
{
    AROS_LIBFUNC_INIT
    req->ios2_Req.io_Error=0;
    if(!base->input || req->ios2_Req.io_Unit!=&base->unit) {
        req->ios2_Req.io_Error=IOERR_OPENFAIL;
        if(!(req->ios2_Req.io_Flags&IOF_QUICK))ReplyMsg(&req->ios2_Req.io_Message);
    } else { req->ios2_Req.io_Flags&=~IOF_QUICK;PutMsg(base->input,&req->ios2_Req.io_Message); }
    AROS_LIBFUNC_EXIT
}
AROS_LH1(LONG,NetAbortIO,AROS_LHA(struct IOSana2Req *,req,A1),LIBBASETYPEPTR,base,6,virtionet_device)
{
    AROS_LIBFUNC_INIT
    Disable();
    if(base->active==req)base->active_abort=TRUE;
    else if(base->tx_request==req) { base->tx_request=NULL;abort_request(req); }
    else {
        struct List *lists[]={base->input?&base->input->mp_MsgList:NULL,&base->reads,&base->writes,&base->events};
        for(unsigned i=0;i<4;++i)if(lists[i]) {
            struct Node *n;
            for(n=lists[i]->lh_Head;n->ln_Succ;n=n->ln_Succ)if(n==(struct Node *)req) { Remove(n);abort_request(req);goto done; }
        }
    }
done:Enable();return 0;
    AROS_LIBFUNC_EXIT
}
ADD2INITLIB(net_init,0)
ADD2EXPUNGELIB(net_expunge,0)
ADD2OPENDEV(net_open,0)
ADD2CLOSEDEV(net_close,0)
