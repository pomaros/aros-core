/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <devices/clipboard.h>
#include <string.h>
#include "client.h"

#define MAX_CLIP (16UL*1024*1024)
#ifndef VM_SYNC_ENGINE_ONLY
int __nowbsupport=1;
struct WBStartup *WBenchMsg;
int __nostdiowin=1;
#endif

struct Clipboard {
    struct MsgPort *port;
    struct IOClipReq *io;
    BOOL opened;
};
static UQUAD getLE(const UBYTE *p,unsigned bytes)
{ UQUAD n=0;for(unsigned i=0;i<bytes;++i)n|=(UQUAD)p[i]<<(i*8);return n; }
static void putLE(UBYTE *p,UQUAD n,unsigned bytes)
{ for(unsigned i=0;i<bytes;++i)p[i]=n>>(i*8); }
static ULONG getBE(const UBYTE *p)
{ return (ULONG)p[0]<<24|(ULONG)p[1]<<16|(ULONG)p[2]<<8|p[3]; }
static BOOL form(const UBYTE *p,ULONG bytes)
{
    return bytes>=12 && bytes<=MAX_CLIP && !memcmp(p,"FORM",4) &&
        getBE(p+4)==bytes-8 && (!memcmp(p+8,"FTXT",4) || !memcmp(p+8,"ILBM",4));
}
static void closeClip(struct Clipboard *c)
{
    if(c->opened)CloseDevice((struct IORequest *)c->io);
    if(c->io)DeleteIORequest((struct IORequest *)c->io);
    if(c->port)DeleteMsgPort(c->port);
    memset(c,0,sizeof(*c));
}
static BOOL openClip(struct Clipboard *c)
{
    memset(c,0,sizeof(*c));c->port=CreateMsgPort();
    if(c->port)c->io=(APTR)CreateIORequest(c->port,sizeof(*c->io));
    if(c->io && !OpenDevice((CONST_STRPTR)"clipboard.device",0,(APTR)c->io,0))c->opened=TRUE;
    if(!c->opened)closeClip(c);
    return c->opened;
}
static LONG clipID(struct Clipboard *c)
{
    c->io->io_Command=CBD_CURRENTWRITEID;
    if(DoIO((APTR)c->io))return -1;
    return c->io->io_ClipID;
}
/* A read owns the unit until a read beyond EOF, including error/oversize paths.
 * Stock deferred CBD_POST may wait for its producer inside BeginIO; AbortIO
 * cannot cancel that device's wait. No transport session is held here. */
static UBYTE *readClip(struct Clipboard *c,ULONG *bytes,LONG *id)
{
    struct IOClipReq *io=c->io;UBYTE header[12],*data=NULL;
    *bytes=0;*id=clipID(c);
    io->io_ClipID=0;io->io_Offset=0;io->io_Data=(APTR)header;
    io->io_Length=12;io->io_Command=CMD_READ;
    if(DoIO((APTR)io) || io->io_Actual!=12 || memcmp(header,"FORM",4))goto done;
    ULONG body=getBE(header+4);
    if(body<4 || body>MAX_CLIP-8)goto done;
    *bytes=body+8;data=AllocVec(*bytes,MEMF_PUBLIC);
    if(!data)goto done;
    memcpy(data,header,12);
    while(io->io_Offset<*bytes) {
        ULONG remaining=*bytes-io->io_Offset;
        io->io_Data=(APTR)(data+io->io_Offset);io->io_Length=remaining;
        if(DoIO((APTR)io) || !io->io_Actual || io->io_Actual>remaining)goto invalid;
    }
    /* Reject trailing data rather than exporting only a prefix. */
    io->io_Data=(APTR)header;io->io_Length=1;
    if(DoIO((APTR)io) || io->io_Actual || !form(data,*bytes))goto invalid;
    goto done;
invalid:
    FreeVec(data);data=NULL;
done:
    if(io->io_ClipID!=-1) {
        io->io_Offset=0xffffffff;io->io_Data=NULL;io->io_Length=1;
        io->io_Command=CMD_READ;DoIO((APTR)io);
    }
    /* A producer may have changed the clip between the initial query and
     * read acquisition. Only acknowledge a snapshot with a stable write ID. */
    if(clipID(c)!=*id && data) { FreeVec(data);data=NULL; }
    if(!data)*bytes=0;
    return data;
}
static BOOL writeClip(struct Clipboard *c,const UBYTE *data,ULONG bytes,LONG *written)
{
    if(!form(data,bytes))return FALSE;
    struct IOClipReq *io=c->io;
    io->io_ClipID=0;io->io_Offset=0;io->io_Data=(APTR)data;
    io->io_Length=bytes;io->io_Command=CMD_WRITE;
    BOOL okay=!DoIO((APTR)io) && io->io_Actual==bytes;
    *written=io->io_ClipID;
    /* Release an incomplete write too. Stock clipboard.device cannot roll a
     * local disk failure back; network input is fully staged before writing. */
    if(io->io_ClipID!=-1) {
        io->io_Command=CMD_UPDATE;
        if(DoIO((APTR)io))okay=FALSE;
    }
    return okay;
}

/* Host wins a conflict already present at the start of a poll. A fresh guest
 * copy during download cancels that poll; neither revision is acknowledged.
 * Revisions survive transport reconnects and our own writes are not echoed. */
static BOOL syncOnce(struct Clipboard *clip,UQUAD *revision,LONG *localID)
{
    LONG observed=clipID(clip),snapshotID=observed;
    if(observed<0)return FALSE;
    ULONG localBytes=0;
    UBYTE *local=NULL,*remote=NULL;
    if(*revision!=(UQUAD)-1 && observed!=*localID)
        local=readClip(clip,&localBytes,&snapshotID);
    struct VMExchangeClient c;
    if(!VMExchangeTryOpen(&c)) { if(local)FreeVec(local);return FALSE; }
    UBYTE request[12],reply[12];ULONG got=0;BOOL okay=FALSE;
    putLE(request,*revision,8);
    if(!VMExchangeRequest(&c,8,request,8,reply,12,&got) || got!=12)goto done;
    UQUAD hostRevision=getLE(reply,8);ULONG bytes=getLE(reply+8,4);
    if(bytes) {
        if(bytes<12 || bytes>MAX_CLIP)goto done;
        remote=AllocVec(bytes,MEMF_PUBLIC);if(!remote)goto done;
        ULONG offset=0;
        while(offset<bytes) {
            ULONG n=bytes-offset;if(n>65536)n=65536;
            putLE(request,n,4);
            if(!VMExchangeRequest(&c,9,request,4,remote+offset,n,&got) || !got)goto done;
            offset+=got;
        }
        if(clipID(clip)!=observed || !form(remote,bytes))goto done;
        LONG written;
        if(!writeClip(clip,remote,bytes,&written))goto done;
        *localID=written;*revision=hostRevision;okay=TRUE;
    } else {
        if(hostRevision!=*revision)goto done;
        if(observed==*localID) { okay=TRUE;goto done; }
        if(!local || snapshotID!=observed || clipID(clip)!=observed)goto done;
        putLE(request,*revision,8);putLE(request+8,localBytes,4);
        if(!VMExchangeRequest(&c,10,request,12,NULL,0,&got) || got)goto done;
        for(ULONG offset=0;offset<localBytes;) {
            ULONG n=localBytes-offset;if(n>65536)n=65536;
            if(!VMExchangeRequest(&c,11,local+offset,n,NULL,0,&got) || got)goto done;
            offset+=n;
        }
        if(!VMExchangeRequest(&c,12,NULL,0,reply,8,&got) || got!=8)goto done;
        *revision=getLE(reply,8);*localID=observed;okay=TRUE;
    }
done:
    VMExchangeClose(&c);
    if(local)FreeVec(local);if(remote)FreeVec(remote);
    return okay;
}
#ifndef VM_SYNC_ENGINE_ONLY
/* The configured device remains inhibited after success, including lost ACKs.
 * Later application writes cannot cross the acknowledged persistence boundary.
 * This covers that device's filesystem buffers, not unsaved application data. */
static BOOL shutdownPoll(CONST_STRPTR volume,BOOL *quiesced)
{
    struct VMExchangeClient c;
    if(!VMExchangeTryOpen(&c))return *quiesced;
    UBYTE reply[8],request[12];ULONG got=0;
    if(VMExchangeRequest(&c,13,NULL,0,reply,8,&got) && got==8) {
        UQUAD token=getLE(reply,8);
        if(token) {
            if(!*quiesced) {
                *quiesced=Inhibit(volume,DOSTRUE)!=0;
                const char *message=*quiesced?"[VMSync] volume quiesced PASS\n":"[VMSync] volume flush failed\n";
                while(*message)RawPutChar(*message++);
            }
            putLE(request,token,8);putLE(request+8,*quiesced?0:1,4);
            VMExchangeRequest(&c,14,request,12,NULL,0,&got);
        }
    }
    VMExchangeClose(&c);
    return *quiesced;
}
int main(int argc,char **argv)
{
    BOOL once=argc==2 && !strcmp(argv[1],"ONCE");
    CONST_STRPTR volume=NULL;
    if(argc==2 && !strncmp(argv[1],"VOLUME=",7)) {
        const char *name=argv[1]+7;size_t n=strlen(name);
        if(n>1 && n<=31 && name[n-1]==':' && !strchr(name,'/') && !strchr(name,'\\')) {
            BOOL valid=TRUE;
            for(size_t i=0;i<n-1;++i)if(name[i]==':' || (UBYTE)name[i]<=32)valid=FALSE;
            if(valid)volume=(CONST_STRPTR)name;
        }
    }
    if(argc!=1 && !once && !volume) { PutStr((CONST_STRPTR)"Usage: VMSync [ONCE | VOLUME=device:] (Ctrl-C stops synchronization)\n");return 20; }
    struct SignalSemaphore singleton;
    memset(&singleton,0,sizeof(singleton));InitSemaphore(&singleton);
    singleton.ss_Link.ln_Name=(STRPTR)"PomAROS.VMSync";
    Forbid();
    BOOL exists=FindSemaphore((CONST_STRPTR)singleton.ss_Link.ln_Name)!=NULL;
    if(!exists)AddSemaphore(&singleton);
    Permit();
    if(exists) { PutStr((CONST_STRPTR)"VMSync is already running\n");return 20; }
    struct Clipboard clip;BOOL opened=openClip(&clip),okay=opened;
    if(okay) {
        UQUAD revision=(UQUAD)-1;LONG localID=clipID(&clip);BOOL quiesced=FALSE;
        PutStr((CONST_STRPTR)"VMSync: clipboard synchronization active\n");
        do {
            if(volume && shutdownPoll(volume,&quiesced))okay=TRUE;
            else okay=syncOnce(&clip,&revision,&localID);
            if(once)break;
            if(SetSignal(0,SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C)break;
            Delay(50);
        } while(!(SetSignal(0,SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C));
        closeClip(&clip);
    } else PutStr((CONST_STRPTR)"VMSync: cannot open clipboard.device\n");
    Forbid();RemSemaphore(&singleton);Permit();
    return !opened || (once && !okay)?20:0;
}

#endif
