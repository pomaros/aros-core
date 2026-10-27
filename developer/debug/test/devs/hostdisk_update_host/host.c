/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
typedef uint32_t ULONG;typedef uint16_t UWORD;typedef int32_t LONG;typedef uint64_t UQUAD;
typedef void *APTR;typedef char *STRPTR;typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define UNIT_READONLY 1
#define IOF_QUICK 1
#define IOERR_NOCMD -3
#define IOERR_BADLENGTH -4
#define TDERR_NotSpecified 20
#define TDERR_WriteProt 28
#define TDERR_DiskChanged 29
#define TDERR_SeekError 30
#define TDERR_DriveInUse 34
#define INVALID_HANDLE_VALUE -1
#define NT_MESSAGE 5
#define NSDEVTYPE_TRACKDISK 5
#define DRIVE_NEWSTYLE 0x4e535459
#define AROS_LIBFUNC_INIT
#define D(x)
#define DCMD(x)
#define DREAD(x)
#define DWRITE(x)
#define bug(...) ((void)0)
#define AROS_HOST_BARRIER atomic_signal_fence(memory_order_seq_cst);
enum {CMD_READ=2,CMD_WRITE,CMD_UPDATE,CMD_CLEAR,CMD_FLUSH,TD_MOTOR,TD_SEEK,TD_FORMAT,TD_CHANGENUM,TD_CHANGESTATE,TD_PROTSTATUS,TD_ADDCHANGEINT,TD_REMCHANGEINT,TD_GETGEOMETRY,TD_READ64,TD_WRITE64,TD_SEEK64,TD_FORMAT64,TD_GETDRIVETYPE,NSCMD_DEVICEQUERY,NSCMD_TD_READ64,NSCMD_TD_WRITE64,NSCMD_TD_SEEK64,NSCMD_TD_FORMAT64,TD_EJECT};
struct Node {int ln_Type;};struct Message {struct Node mn_Node;struct Message *next;pthread_mutex_t mutex;pthread_cond_t cond;atomic_int done;};
struct MsgPort {pthread_mutex_t mutex;pthread_cond_t cond;struct Message *head,*tail;int stop;};
struct unit;
struct IORequest {struct Message io_Message;struct unit *io_Unit;UWORD io_Command;unsigned io_Flags;LONG io_Error;ULONG io_Actual,io_Length,io_Offset;APTR io_Data;};
struct IOExtTD {struct IORequest iotd_Req;};
struct HostInterface {int(*ioctl)(int,int,...);int(*open)(char*,int,...);int(*close)(int);ssize_t(*read)(int,void*,size_t);ssize_t(*write)(int,const void*,size_t);off_t(*lseek)(int,off_t,int);int*(*__error)(void);int(*fsync)(int);};
struct HostDiskBase {struct HostInterface *iface;ULONG(*update)(struct unit*);};
struct unit {struct HostDiskBase *hdskBase;struct MsgPort *port;int file;char *filename;unsigned flags,changecount;int changeints;};
struct NSDeviceQueryResult {ULONG DevQueryFormat,SizeAvailable,DeviceType,DeviceSubType;UWORD *SupportedCommands;};
struct DriveGeometry {ULONG dg_SectorSize,dg_TotalSectors,dg_CylSectors,dg_Cylinders,dg_Heads,dg_TrackSectors;};
struct hd_geometry {unsigned heads,sectors,cylinders;};
#define HDIO_GETGEO 41
#define BLKSSZGET 42
#define BLKGETSIZE 43
#define DKIOCGETBLOCKSIZE 44
#define DKIOCGETBLOCKCOUNT 45
#define HOST_VACALL3(fn,a,b,c) fn(a,b,c)
static pthread_mutex_t host_mutex=PTHREAD_MUTEX_INITIALIZER;static _Thread_local int host_held;
static void host_lock(void){assert(!host_held);pthread_mutex_lock(&host_mutex);host_held=1;}
static void host_unlock(void){assert(host_held);host_held=0;pthread_mutex_unlock(&host_mutex);}
#define HostLib_Lock() host_lock()
#define HostLib_Unlock() host_unlock()
#define LSeek(fd,low,high,whence) hdskBase->iface->lseek(fd,(off_t)((UQUAD)low|((UQUAD)high<<32)),whence)
static ULONG Host_Seek64(struct unit*,ULONG,ULONG);
static ULONG getgeometry(struct unit *u,struct DriveGeometry *g){(void)u;(void)g;abort();}
static void eject(struct unit *u,BOOL e){(void)u;(void)e;abort();}
#define Forbid() ((void)0)
#define Permit() ((void)0)
static void addchangeint(struct unit *u,struct IOExtTD *r){(void)u;(void)r;abort();}
static void remchangeint(struct unit *u,struct IOExtTD *r){(void)u;(void)r;abort();}
static void ReplyMsg(struct Message *m){struct IOExtTD *r=(void*)m;assert(!(r->iotd_Req.io_Flags&IOF_QUICK));pthread_mutex_lock(&m->mutex);assert(!atomic_load(&m->done));atomic_store(&m->done,1);pthread_cond_signal(&m->cond);pthread_mutex_unlock(&m->mutex);}
static void PutMsg(struct MsgPort *p,struct Message *m){struct IOExtTD *r=(void*)m;assert(!(r->iotd_Req.io_Flags&IOF_QUICK));pthread_mutex_lock(&p->mutex);if(p->tail)p->tail->next=m;else p->head=m;p->tail=m;pthread_cond_signal(&p->cond);pthread_mutex_unlock(&p->mutex);}
static int *fixture_cached_errno;
#include "actual.c"
static atomic_int block_write,write_entered,allow_write,block_sync,sync_entered,allow_sync;
static atomic_uint writes,syncs;
static int injected_errno,interrupted,positive_ret,write_zero,write_fail,verify_order,read_fail,short_write,open_calls,open_error,initial_errno,ioctl_error;
static char events[32];static unsigned event_count;
static void pause_short(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static void await(atomic_int *flag){for(unsigned i=0;i<3000&&!atomic_load(flag);i++)pause_short();assert(atomic_load(flag));}
static int *host_errno(void){assert(host_held);return &errno;}
static ssize_t(*libc_write)(int,const void*,size_t);static ssize_t(*libc_read)(int,void*,size_t);
static ssize_t real_write(int fd,const void *data,size_t n){assert(host_held);unsigned count=atomic_fetch_add(&writes,1);if(block_write&&!count){atomic_store(&write_entered,1);await(&allow_write);}if(write_zero)return 0;if(write_fail){errno=EIO;return -1;}assert(event_count<sizeof(events));events[event_count++]='W';return libc_write(fd,data,short_write&&n>2?2:n);}
static ssize_t real_read(int fd,void *data,size_t n){assert(host_held);if(read_fail){errno=EIO;return -1;}return libc_read(fd,data,n);}
static off_t real_seek(int fd,off_t pos,int mode){assert(host_held);return lseek(fd,pos,mode);}
static int real_ioctl(int fd,int request,...){(void)fd;assert(host_held);if(ioctl_error){errno=ioctl_error;return -1;}va_list ap;va_start(ap,request);void *value=va_arg(ap,void*);va_end(ap);switch(request){case HDIO_GETGEO:*(struct hd_geometry*)value=(struct hd_geometry){4,32,8};break;case BLKSSZGET:*(size_t*)value=512;break;case BLKGETSIZE:*(unsigned long*)value=1024;break;case DKIOCGETBLOCKSIZE:*(ULONG*)value=512;break;case DKIOCGETBLOCKCOUNT:*(UQUAD*)value=1024;break;default:abort();}return 0;}
struct geometry_call {int variant,file;struct HostDiskBase *base;struct DriveGeometry result;ULONG error;};
static void *geometry_thread(void *data){struct geometry_call *call=data;call->error=call->variant?linux_geometry(call->file,&call->result,call->base):darwin_geometry(call->file,&call->result,call->base);return NULL;}
static int real_open(char *path,int flags,...){assert(host_held);open_calls++;if(open_error&&flags==O_RDWR){errno=open_error;return -1;}return open(path,flags,0600);}
static int real_close(int fd){assert(host_held);return close(fd);}
static void *open_thread(void *data){struct unit *u=data;errno=initial_errno;return (void*)(uintptr_t)Host_Open(u);}
static int real_sync(int fd){assert(host_held);unsigned count=atomic_fetch_add(&syncs,1);if(interrupted){interrupted--;errno=EINTR;return -1;}if(positive_ret)return 1;if(injected_errno){errno=injected_errno<0?0:injected_errno;return -1;}
 if(block_sync&&!count){atomic_store(&sync_entered,1);await(&allow_sync);}
 assert(event_count<sizeof(events));events[event_count++]='B';if(verify_order){char data[8]={0};ssize_t n=pread(fd,data,sizeof(data),0);assert(n==(count?8:4));assert(!memcmp(data,count?"AAAABBBB":"AAAA",n));}
 return fsync(fd);}
static void *worker(void *data){struct unit *u=data;struct MsgPort *p=u->port;for(;;){pthread_mutex_lock(&p->mutex);while(!p->head&&!p->stop)pthread_cond_wait(&p->cond,&p->mutex);if(!p->head){pthread_mutex_unlock(&p->mutex);return NULL;}struct Message *m=p->head;p->head=m->next;if(!p->head)p->tail=NULL;pthread_mutex_unlock(&p->mutex);actual_dispatch(u,(void*)m);} }
static void request_init(struct IOExtTD *r,struct unit *u,UWORD cmd){memset(r,0,sizeof(*r));pthread_mutex_init(&r->iotd_Req.io_Message.mutex,NULL);pthread_cond_init(&r->iotd_Req.io_Message.cond,NULL);r->iotd_Req.io_Unit=u;r->iotd_Req.io_Command=cmd;r->iotd_Req.io_Flags=IOF_QUICK;r->iotd_Req.io_Actual=0x7654;r->iotd_Req.io_Error=77;}
static void request_wait(struct IOExtTD *r){struct Message *m=&r->iotd_Req.io_Message;pthread_mutex_lock(&m->mutex);while(!atomic_load(&m->done))pthread_cond_wait(&m->cond,&m->mutex);pthread_mutex_unlock(&m->mutex);}
static void request_destroy(struct IOExtTD *r){pthread_mutex_destroy(&r->iotd_Req.io_Message.mutex);pthread_cond_destroy(&r->iotd_Req.io_Message.cond);}
static void reset(void){injected_errno=interrupted=positive_ret=write_zero=write_fail=verify_order=read_fail=short_write=open_calls=open_error=initial_errno=ioctl_error=0;event_count=0;writes=syncs=0;block_write=write_entered=allow_write=block_sync=sync_entered=allow_sync=0;}
static void submit_sync(struct unit *u,LONG expected){struct IOExtTD r;request_init(&r,u,CMD_UPDATE);actual_beginio(&r,u->hdskBase);request_wait(&r);assert(r.iotd_Req.io_Error==expected&&r.iotd_Req.io_Actual==0&&!(r.iotd_Req.io_Flags&IOF_QUICK));request_destroy(&r);}
static void check_query(struct unit *u,BOOL expected){struct IOExtTD r;struct NSDeviceQueryResult q={0};request_init(&r,u,NSCMD_DEVICEQUERY);r.iotd_Req.io_Data=&q;actual_beginio(&r,u->hdskBase);assert(!r.iotd_Req.io_Error&&q.SizeAvailable==sizeof(q));BOOL found=FALSE;for(UWORD *v=q.SupportedCommands;*v;v++)if(*v==CMD_UPDATE)found=TRUE;assert(found==expected);request_destroy(&r);}
int main(int argc,char **argv){assert(argc==2);void *process=dlopen(NULL,RTLD_LAZY);assert(process);libc_write=dlsym(process,"write");libc_read=dlsym(process,"read");assert(libc_write&&libc_read);
 int fd=open(argv[1],O_CREAT|O_EXCL|O_RDWR,0600);assert(fd>=0);errno=0;fixture_cached_errno=&errno;struct HostInterface iface={.ioctl=real_ioctl,.open=real_open,.close=real_close,.read=real_read,.write=real_write,.lseek=real_seek,.__error=host_errno,.fsync=real_sync};struct HostDiskBase base={&iface,Host_Update};struct MsgPort port={.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};struct unit u={.hdskBase=&base,.port=&port,.file=fd};pthread_t task;assert(!pthread_create(&task,NULL,worker,&u));unsigned cases=0;
 reset();check_query(&u,TRUE);cases++;base.update=NULL;check_query(&u,FALSE);submit_sync(&u,IOERR_NOCMD);assert(!syncs);cases+=2;base.update=Host_Update;
 reset();block_write=block_sync=verify_order=1;struct IOExtTD requests[4];UWORD commands[]={CMD_WRITE,CMD_UPDATE,CMD_WRITE,CMD_UPDATE};for(int i=0;i<4;i++){request_init(&requests[i],&u,commands[i]);if(i%2==0){requests[i].iotd_Req.io_Data=i?"BBBB":"AAAA";requests[i].iotd_Req.io_Length=4;requests[i].iotd_Req.io_Offset=i?4:0;}actual_beginio(&requests[i],&base);}
 await(&write_entered);assert(!atomic_load(&requests[1].iotd_Req.io_Message.done)&&!syncs);atomic_store(&allow_write,1);await(&sync_entered);assert(!atomic_load(&requests[1].iotd_Req.io_Message.done)&&writes==1);atomic_store(&allow_sync,1);
 for(int i=0;i<4;i++){request_wait(&requests[i]);assert(!requests[i].iotd_Req.io_Error&&requests[i].iotd_Req.io_Actual==(i%2?0:4));request_destroy(&requests[i]);}assert(event_count==4&&!memcmp(events,"WBWB",4)&&syncs==2);cases++;
 reset();interrupted=2;submit_sync(&u,0);assert(syncs==3);cases++;
 int errors[]={EIO,EPERM,EROFS,EACCES,ENOSYS,EINVAL,ENOTSUP,EOPNOTSUPP,-1};for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++){reset();injected_errno=errors[i];LONG expected=i==1||i==2||i==3?TDERR_WriteProt:i>=4&&i<8?IOERR_NOCMD:TDERR_NotSpecified;submit_sync(&u,expected);assert(syncs==1);cases++;}
 reset();positive_ret=1;submit_sync(&u,TDERR_NotSpecified);cases++;
 reset();iface.fsync=NULL;submit_sync(&u,IOERR_NOCMD);assert(!syncs);iface.fsync=real_sync;cases++;
 reset();u.file=-1;submit_sync(&u,TDERR_DiskChanged);assert(!syncs);u.file=fd;cases++;
 reset();int ro=open(argv[1],O_RDONLY);assert(ro>=0);u.file=ro;u.flags=UNIT_READONLY;submit_sync(&u,0);assert(syncs==1);u.file=fd;u.flags=0;close(ro);cases++;
 for(int fault=0;fault<3;fault++){reset();u.flags=fault==0?UNIT_READONLY:0;write_zero=fault==1;write_fail=fault==2;struct IOExtTD r;request_init(&r,&u,CMD_WRITE);r.iotd_Req.io_Data="CCCC";r.iotd_Req.io_Length=4;r.iotd_Req.io_Offset=0;actual_beginio(&r,&base);request_wait(&r);assert(r.iotd_Req.io_Error==(fault==0?TDERR_WriteProt:fault==1?IOERR_BADLENGTH:TDERR_NotSpecified));request_destroy(&r);u.flags=0;cases++;}
 for(int failure=0;failure<2;failure++){reset();read_fail=failure;char buffer[4];struct IOExtTD r;request_init(&r,&u,CMD_READ);r.iotd_Req.io_Data=buffer;r.iotd_Req.io_Length=4;actual_beginio(&r,&base);request_wait(&r);assert(r.iotd_Req.io_Error==(failure?TDERR_NotSpecified:0));assert(r.iotd_Req.io_Actual==(failure?0:4));request_destroy(&r);cases++;}
 reset();short_write=1;struct IOExtTD partial;request_init(&partial,&u,CMD_WRITE);partial.iotd_Req.io_Data="DDDD";partial.iotd_Req.io_Length=4;actual_beginio(&partial,&base);request_wait(&partial);assert(!partial.iotd_Req.io_Error&&partial.iotd_Req.io_Actual==4&&writes==2);request_destroy(&partial);cases++;
 for(int scenario=0;scenario<6;scenario++){reset();initial_errno=EBUSY;const int fallback_errors[]={EROFS,EBUSY,EACCES,EPERM};open_error=scenario>=2?fallback_errors[scenario-2]:0;struct unit other={.hdskBase=&base,.file=-1,.filename=scenario==1?"/this/hostdisk/file/does/not/exist":argv[1]};pthread_t opener;assert(!pthread_create(&opener,NULL,open_thread,&other));void *result;pthread_join(opener,&result);assert((ULONG)(uintptr_t)result==(scenario==1?TDERR_NotSpecified:0));assert(open_calls==(scenario>=2?2:1));if(scenario!=1){assert(other.file>=0&&other.flags==(scenario>=2?UNIT_READONLY:0));Host_Close(&other);}cases++;}
 for(int variant=0;variant<2;variant++)for(int failure=0;failure<2;failure++){reset();ioctl_error=failure?EIO:0;struct geometry_call call={.variant=variant,.file=fd,.base=&base};pthread_t t;assert(!pthread_create(&t,NULL,geometry_thread,&call));pthread_join(t,NULL);assert(call.error==(failure?EIO:0));if(!failure)assert(call.result.dg_SectorSize==512&&call.result.dg_TotalSectors==1024);cases++;}
 reset();pthread_mutex_lock(&port.mutex);port.stop=1;pthread_cond_signal(&port.cond);pthread_mutex_unlock(&port.mutex);pthread_join(task,NULL);assert(!close(fd));assert(!dlclose(process));printf("PASS %u actual hostdisk FIFO/barrier/error cases with real fsync and host file\n",cases);return 0;}
