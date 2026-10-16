/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
   Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <string.h>
#include <aros/debug.h>
#include "client.h"
static UQUAD get64(const UBYTE *p)
{ UQUAD n=0;for(unsigned i=0;i<8;++i)n|=(UQUAD)p[i]<<(8*i);return n; }
/* commandline.o also references WBenchMsg. Supply it here so that autoinit
   does not pull fromwb.o and wait for a WBStartup message in a non-CLI
   caller (for example RunCommand from a boot qualification process). */
int __nowbsupport=1;
struct WBStartup *WBenchMsg;
int __nostdiowin=1;
int main(int argc,char **argv)
{
    bug("[VMExchange] main argc=%d\n",argc);
    if(argc==2 && !strcmp(argv[1],"HELLO")) {
        struct VMExchangeClient c;UBYTE reply[64];ULONG got=0;
        bug("[VMExchange] hello connect\n");
        if(!VMExchangeOpen(&c))return 20;
        BOOL okay=VMExchangeRequest(&c,1,NULL,0,reply,sizeof(reply),&got) &&
            got==sizeof("PomAROS host services 1")-1 && !memcmp(reply,"PomAROS host services 1",got);
        bug("[VMExchange] hello response=%u\n",okay);
        VMExchangeClose(&c);return okay?0:20;
    }
    if(argc!=4 || (strcmp(argv[1],"PUT") && strcmp(argv[1],"GET"))) {
        PutStr((CONST_STRPTR)"Usage: VMExchange HELLO | PUT local-file host-name | GET host-name local-file\n");return 20;
    }
    BOOL put=!strcmp(argv[1],"PUT"),okay=FALSE,temporary_created=FALSE;
    const char *name=put?argv[3]:argv[2],*local=put?argv[2]:argv[3];
    if(!*name || strlen(name)>255 || strlen(local)>800)return 20;
    struct VMExchangeClient client;BPTR file=0;UBYTE *buffer=AllocVec(4096,MEMF_PUBLIC);
    char temporary[900]={0};ULONG got=0;UQUAD total=0;
    if(!buffer)return 20;
    if(!VMExchangeOpen(&client)) { PutStr((CONST_STRPTR)"VMExchange: host service unavailable\n");FreeVec(buffer);return 20; }
    if(put) {
        file=Open((CONST_STRPTR)local,MODE_OLDFILE);if(!file)goto done;
        if(!VMExchangeRequest(&client,2,name,strlen(name),NULL,0,&got))goto done;
        for(;;) {
            LONG n=Read(file,buffer,4096);if(n<0)goto done;if(!n)break;
            total+=n;if(total>64*1024*1024 || !VMExchangeRequest(&client,3,buffer,n,NULL,0,&got))goto done;
        }
        if(!VMExchangeRequest(&client,4,NULL,0,buffer,8,&got) || got!=8 || get64(buffer)!=total)goto done;
        okay=TRUE;
    } else {
        BPTR exists=Lock((CONST_STRPTR)local,ACCESS_READ);
        if(exists) { UnLock(exists);PutStr((CONST_STRPTR)"VMExchange: destination exists\n");goto done; }
        if(!VMExchangeRequest(&client,5,name,strlen(name),buffer,8,&got) || got!=8)goto done;
        UQUAD size=get64(buffer);if(size>64*1024*1024)goto done;
        /* Task address makes this transaction name distinct within this boot;
         * an existing temporary is never truncated. Rename publishes last. */
        __builtin_snprintf(temporary,sizeof(temporary),"%s.vmexchange-%lx",local,(unsigned long)FindTask(NULL));
        exists=Lock((CONST_STRPTR)temporary,ACCESS_READ);
        if(exists) { UnLock(exists);goto done; }
        file=Open((CONST_STRPTR)temporary,MODE_NEWFILE);if(!file)goto done;temporary_created=TRUE;
        while(total<size) {
            UBYTE request[4]={0,16,0,0};
            if(!VMExchangeRequest(&client,6,request,4,buffer,4096,&got) || !got || got>size-total)goto done;
            if(Write(file,buffer,got)!=(LONG)got)goto done;total+=got;
        }
        if(!VMExchangeRequest(&client,7,NULL,0,NULL,0,&got) || !Flush(file))goto done;
        LONG closed=Close(file);file=0;
        if(!closed)goto done;
        if(!Rename((CONST_STRPTR)temporary,(CONST_STRPTR)local))goto done;
        temporary_created=FALSE;okay=TRUE;
    }
done:
    if(file)Close(file);
    if(temporary_created)DeleteFile((CONST_STRPTR)temporary);
    if(!okay)Printf((CONST_STRPTR)"VMExchange failed (host status %lu)\n",client.status);
    else PutStr((CONST_STRPTR)"VMExchange complete\n");
    VMExchangeClose(&client);FreeVec(buffer);return okay?0:20;
}
