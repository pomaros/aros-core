/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1.
   MVHS framing is transport-independent; this binding uses Exec vmbridge IO. */
#include "client.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/semaphores.h>

/* UP registry lifetime: every holder/waiter owns a reference. Both name and
 * semaphore outlive an individual command segment. Not an SMP contract. */
struct VMExchangeGate {
    struct SignalSemaphore semaphore;
    ULONG references;
    char name[sizeof("PomAROS.VMExchange.session")];
};
static BOOL acquire(struct VMExchangeClient *c, BOOL wait)
{
    struct VMExchangeGate *candidate=AllocVec(sizeof(*candidate),MEMF_PUBLIC|MEMF_CLEAR);
    if(!candidate)return FALSE;
    InitSemaphore(&candidate->semaphore);
    strcpy(candidate->name,"PomAROS.VMExchange.session");
    candidate->semaphore.ss_Link.ln_Name=candidate->name;
    Forbid();
    c->gate=(APTR)FindSemaphore((CONST_STRPTR)candidate->name);
    if(!c->gate) {
        c->gate=candidate;candidate=NULL;AddSemaphore(&c->gate->semaphore);
    }
    ++c->gate->references;
    /* Recursive acquisition cannot open the exclusive device a second time. */
    BOOL recursive=c->gate->semaphore.ss_Owner==FindTask(NULL);
    Permit();
    if(candidate)FreeVec(candidate);
    if(recursive)return FALSE;
    if(wait) { ObtainSemaphore(&c->gate->semaphore);c->gateHeld=TRUE; }
    else c->gateHeld=AttemptSemaphore(&c->gate->semaphore);
    return c->gateHeld;
}
static void release(struct VMExchangeClient *c)
{
    if(!c->gate)return;
    if(c->gateHeld)ReleaseSemaphore(&c->gate->semaphore);
    Forbid();
    BOOL last=!--c->gate->references;
    if(last)RemSemaphore(&c->gate->semaphore);
    Permit();
    if(last)FreeVec(c->gate);
    c->gate=NULL;c->gateHeld=FALSE;
}
static ULONG le32(const UBYTE *p)
{ return (ULONG)p[0]|(ULONG)p[1]<<8|(ULONG)p[2]<<16|(ULONG)p[3]<<24; }
static void set32(UBYTE *p,ULONG n)
{ for(unsigned i=0;i<4;++i)p[i]=n>>(8*i); }
static BOOL transfer(struct VMExchangeClient *c,void *data,ULONG bytes,UWORD command)
{
    UBYTE *p=data;
    while(bytes) {
        c->io->io_Command=command;c->io->io_Data=p;c->io->io_Length=bytes;
        SendIO((struct IORequest *)c->io);
        unsigned tick=0;
        while(!CheckIO((struct IORequest *)c->io) && tick++<500)Delay(1);
        if(!CheckIO((struct IORequest *)c->io)) {
            AbortIO((struct IORequest *)c->io);WaitIO((struct IORequest *)c->io);return FALSE;
        }
        if(WaitIO((struct IORequest *)c->io) || !c->io->io_Actual || c->io->io_Actual>bytes)return FALSE;
        bytes-=c->io->io_Actual;p+=c->io->io_Actual;
    }
    return TRUE;
}
static BOOL openSession(struct VMExchangeClient *c, BOOL wait)
{
    memset(c,0,sizeof(*c));
    if(!acquire(c,wait)) { release(c);return FALSE; }
    c->port=CreateMsgPort();
    if(c->port)c->io=(APTR)CreateIORequest(c->port,sizeof(*c->io));
    if(c->io && !OpenDevice((CONST_STRPTR)"vmbridge.device",4051,(struct IORequest *)c->io,0))c->opened=TRUE;
    if(!c->opened)VMExchangeClose(c);return c->opened;
}
BOOL VMExchangeOpen(struct VMExchangeClient *c) { return openSession(c,TRUE); }
BOOL VMExchangeTryOpen(struct VMExchangeClient *c) { return openSession(c,FALSE); }
void VMExchangeClose(struct VMExchangeClient *c)
{
    if(c->opened)CloseDevice((struct IORequest *)c->io);
    if(c->io)DeleteIORequest((struct IORequest *)c->io);
    if(c->port)DeleteMsgPort(c->port);
    c->io=NULL;c->port=NULL;c->opened=FALSE;
    release(c);
}
BOOL VMExchangeRequest(struct VMExchangeClient *c,UWORD op,const void *data,ULONG bytes,void *response,ULONG capacity,ULONG *received)
{
    if(received)*received=0;
    if(!c || !c->opened || c->failed || bytes>65536 || (bytes && !data) || (capacity && !response) || c->id==0xffffffff)return FALSE;
    UBYTE header[20]={ 'M','V','H','S',1,0,0,0 };
    header[6]=op;header[7]=op>>8;set32(header+8,++c->id);set32(header+12,bytes);
    c->status=0;
    if(!transfer(c,header,sizeof(header),CMD_WRITE) || !transfer(c,(APTR)data,bytes,CMD_WRITE) ||
       !transfer(c,header,sizeof(header),CMD_READ))goto fail;
    ULONG length=le32(header+12);
    if(memcmp(header,"MVHS\1\0",6) || header[6]!=(UBYTE)op || header[7]!=(UBYTE)(op>>8) ||
       le32(header+8)!=c->id || length>65536 || length>capacity)goto fail;
    c->status=le32(header+16);
    if(!transfer(c,response,length,CMD_READ))goto fail;
    if(received)*received=length;return c->status==0;
fail:c->status=0xffffffff;c->failed=TRUE;return FALSE;
}
