/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL), Version 1.1.
 * Owners retain TCP sockets while separately loaded AP workers use POSIX I/O.
 * Workers never open bsdsocket.library explicitly. External capture bounds
 * stalled OS calls; per-frame retries bound nonblocking transfer attempts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <devices/timer.h>
#include <dos/dostags.h>
#include <libraries/fd.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <proto/bsdsocket.h>
#include <proto/fd.h>
#include <aros/debug.h>
struct Library *SocketBase, *FDBase;
APTR KernelBase;
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while (0)
#define FRAME 256
#define ROUNDS 16
static unsigned expected_cpu,samples,wrong,first,last,rounds,bytes,bounds;
static void sample(void) {
    last=KrnGetCPUNumber();if(!samples)first=last;samples++;
    if(last!=expected_cpu)wrong++;
}
static int number(const char *text,unsigned max,unsigned *value) {
    char *end;unsigned long n;
    if(!*text||*text=='-')return 0;
    n=strtoul(text,&end,10);if(*end||n>max)return 0;
    *value=(unsigned)n;return 1;
}
static void pattern(unsigned char *buffer,unsigned round) {
    for(unsigned i=0;i<FRAME;i++)buffer[i]=(unsigned char)(round*31+i*17);
}
static int transfer(int fd,unsigned char *buffer,int writing) {
    unsigned offset=0;
    for(unsigned tries=0;tries<200&&offset<FRAME;tries++) {
        sample();
        ssize_t n=writing?write(fd,buffer+offset,FRAME-offset):read(fd,buffer+offset,FRAME-offset);
        sample();
        if(n<0) {
            if(errno!=EWOULDBLOCK&&errno!=EAGAIN&&errno!=EINTR)return 0;
            Delay(1);continue;
        }
        if(n==0||(size_t)n>FRAME-offset)return 0;
        offset+=(unsigned)n;bytes+=(unsigned)n;
    }
    return offset==FRAME;
}
static int worker(int fd,int server) {
    unsigned char buffer[FRAME],expected[FRAME];int on=1,available=-1,ok=0,error=0;
    const char *stage="fd-owner";
    FDBase=OpenLibrary("fd.library",0);
    if(!FDBase)goto done;
    const struct fd_hooks *hooks=FD_GetOwnerHooks(FD_OWNER_BSDSOCKET);
    if(fd<=2||FD_GetOwner(fd)!=FD_OWNER_BSDSOCKET||!FD_GetData(fd)||!hooks||
       hooks->fdh_Version!=FD_HOOKS_VERSION||!hooks->fdh_read||!hooks->fdh_write||!hooks->fdh_ioctl)goto done;
    stage="ioctl";sample();
    if(ioctl(fd,(int)FIONBIO,&on)||ioctl(fd,(int)FIONREAD,&available)||available<0)goto done;
    sample();stage="bounds";
    /* The buffer is deliberately small: rejection must happen before access. */
    const size_t invalid[]={(size_t)INT_MAX+1,(size_t)UINT32_MAX+1,SIZE_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        errno=0;if(read(fd,buffer,invalid[i])!=-1||errno!=EINVAL)goto done;bounds++;
        errno=0;if(write(fd,buffer,invalid[i])!=-1||errno!=EINVAL)goto done;bounds++;
        sample();
    }
    stage="exchange";
    for(unsigned round=0;round<ROUNDS;round++) {
        pattern(expected,round);
        if(!server){memcpy(buffer,expected,FRAME);if(!transfer(fd,buffer,1))goto done;}
        if(!transfer(fd,buffer,0)||memcmp(buffer,expected,FRAME))goto done;
        if(server&&!transfer(fd,buffer,1))goto done;
        rounds++;
    }
    ok=!wrong;
done:
    error=errno;sample();if(wrong)ok=0;
    REPORT("[SMP-POSIX] worker role=%s result=%s stage=%s fd=%d rounds=%u bytes=%u bounds=%u first=%u last=%u samples=%u wrong=%u error=%d\n",
        server?"server":"client",ok?"PASS":"FAIL",stage,fd,rounds,bytes,bounds,first,last,samples,wrong,ok?0:error);
    /* Descriptor remains owned by the waiting parent. */
    if(FDBase)CloseLibrary(FDBase);
    return ok?0:20;
}
static int ready(int fd,int writing) {
    fd_set set;struct timeval tv;int result;
    tv.tv_sec=2;tv.tv_usec=0;
    if(fd<0||fd>=FD_SETSIZE)return 0;
    FD_ZERO(&set);FD_SET(fd,&set);sample();
    result=WaitSelect(fd+1,writing?NULL:&set,writing?&set:NULL,NULL,&tv,NULL);
    sample();return result==1&&FD_ISSET(fd,&set);
}
static int owner(unsigned port,unsigned worker_cpu,int server) {
    struct sockaddr_in address;int fd=-1,peer=-1,on=1,error=0,length=sizeof(error),ok=0;
    LONG worker_rc=-1;char command[160];const char *stage="open";
    SocketBase=OpenLibrary("bsdsocket.library",4);if(!SocketBase)goto done;
    stage="socket";fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0||IoctlSocket(fd,FIONBIO,(void *)&on))goto done;
    memset(&address,0,sizeof(address));address.sin_family=AF_INET;
    address.sin_port=htons(port);address.sin_addr.s_addr=htonl(0x7f000001UL);
    if(server) {
        stage="listen";
        if(bind(fd,(struct sockaddr *)&address,sizeof(address))||listen(fd,1))goto done;
        REPORT("[SMP-POSIX] listening port=%u cpu=%u\n",port,expected_cpu);Flush(Output());
        stage="accept";if(!ready(fd,0))goto done;
        peer=accept(fd,NULL,NULL);if(peer<0||IoctlSocket(peer,FIONBIO,(void *)&on))goto done;
    } else {
        stage="connect";
        if(connect(fd,(struct sockaddr *)&address,sizeof(address))<0&&Errno()!=EINPROGRESS)goto done;
        if(!ready(fd,1)||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)||error)goto done;
        peer=fd;
    }
    stage="worker";
    snprintf(command,sizeof(command),"C:SMP-Run %u C:SMP-POSIX worker-%s %d %u",worker_cpu,server?"server":"client",peer,worker_cpu);
    REPORT("[SMP-POSIX] owner-start role=%s cpu=%u worker=%u fd=%d\n",server?"server":"client",expected_cpu,worker_cpu,peer);
    /* Synchronous Shell waits for SMP-Run, which retains and reaps its loaded
       child before returning. Socket lifetime extends past that return. */
    worker_rc=SystemTags(command,SYS_Asynch,FALSE,TAG_DONE);
    ok=worker_rc==0;
done:
    if(SocketBase)error=Errno();
    if(peer>=0&&peer!=fd)CloseSocket(peer);
    if(fd>=0)CloseSocket(fd);
    sample();if(wrong)ok=0;
    REPORT("[SMP-POSIX] owner role=%s result=%s stage=%s worker=%u worker_rc=%ld first=%u last=%u samples=%u wrong=%u error=%d\n",
        server?"server":"client",ok?"PASS":"FAIL",stage,worker_cpu,(long)worker_rc,first,last,samples,wrong,ok?0:error);
    if(SocketBase)CloseLibrary(SocketBase);
    return ok?0:20;
}
int main(int argc,char **argv) {
    unsigned value,worker_cpu;int server;
    KernelBase=OpenResource("kernel.resource");if(!KernelBase)return 20;
    if(argc==4&&(!strcmp(argv[1],"worker-server")||!strcmp(argv[1],"worker-client"))&&
       number(argv[2],INT_MAX,&value)&&number(argv[3],7,&expected_cpu)) {
        sample();return worker((int)value,!strcmp(argv[1],"worker-server"));
    }
    if(argc!=5||(strcmp(argv[1],"server")&&strcmp(argv[1],"client"))||
       !number(argv[2],65535,&value)||value<1024||!number(argv[3],7,&expected_cpu)||
       !number(argv[4],7,&worker_cpu)||worker_cpu==expected_cpu) {
        REPORT("SMP-POSIX: server|client PORT OWNER_CPU WORKER_CPU\n");return 20;
    }
    server=!strcmp(argv[1],"server");sample();return owner(value,worker_cpu,server);
}
