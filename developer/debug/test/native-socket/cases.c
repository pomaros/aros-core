/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL), Version 1.1. */
static LONG call(ULONG op,LONG fd,LONG a,LONG b,LONG c){
 struct aros_socket_request r={.version=AROS_SOCKET_ABI,.operation=op,.fd=fd,.arg0=a,.arg1=b,.arg2=c};
 assert(native_call(&r,NULL)==0);return r.result;
}
static void *creator(void *unused){(void)unused;return (void *)(intptr_t)call(AROS_SOCKET_CREATE,0,2,SOCK_STREAM,0);}
static void *reader(void *arg){
 char bytes[8];LONG error;
 SIPTR n=__fdhook((int)(intptr_t)arg,__FDH_READ,(IPTR)bytes,sizeof(bytes),0,&error);
 assert(n==8&&error==0&&bytes[0]==0x42);return NULL;
}
static void *cloner(void *arg){
 LONG fd=(intptr_t)arg;
 for(int i=0;i<128;i++){
  LONG copy=call(AROS_SOCKET_CLONE,fd,3,0,0);
  call(AROS_SOCKET_CLOSE,copy,0,0,0);
 }
 return NULL;
}
static void init_lock(struct SignalSemaphore *s){
 pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));
 assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
 assert(!pthread_mutex_init(&s->mutex,&attr));pthread_mutexattr_destroy(&attr);
}
static QUAD provider_seek(APTR data, LONG fd, QUAD offset, LONG whence, LONG *error) {
 assert(data && fd>=3 && whence==2);
 *error=0;
 return *(QUAD *)data + offset;
}
static int releases;
static void release_buffer(APTR payload) { assert(payload == &releases); ++releases; }

static void test_exported_buffer(void) {
 static const char provider=0, other=0;
 struct fd_buffer buffer={&provider,&releases,INT64_C(0x200000000),FALSE,release_buffer};
 struct fd_lease lease={0};
 APTR payload=NULL;
 LONG fd=-1,error=0;
 registry.fd_OwnerHooks[FD_OWNER_BUFFER]=&fd_buffer_hooks;
 assert(FD_ExportBuffer(&buffer,FD_CLOEXEC,&fd,&registry)==0 && fd>=3);
 LONG lowest=__fdhook(fd,__FDH_FCNTL,F_DUPFD_CLOEXEC,0,0,&error);
 assert(lowest>=3 && !error && lowest!=fd);
 assert(__fdhook(lowest,__FDH_FCNTL,F_GETFD,0,0,&error)==FD_CLOEXEC);
 assert(__fdhook(lowest,__FDH_CLOSE,0,0,0,&error)==0 && !releases);
 assert(FD_AcquireBuffer(fd,&other,&lease,&payload,&registry)==EINVAL && !payload && !lease.private_record);
 LONG second=__fdhook(fd,__FDH_FCNTL,F_DUPFD,24,0,&error);
 assert(second>=24 && !error);
 assert(__fdhook(second,__FDH_FCNTL,F_GETFL,0,0,&error)==O_RDONLY);
 assert(__fdhook(fd,__FDH_FCNTL,F_GETFD,0,0,&error)==FD_CLOEXEC);
 assert(__fdhook(second,__FDH_FCNTL,F_GETFD,0,0,&error)==0);
 assert(__fdhook(fd,__FDH_CLOSE,0,0,0,&error)==0 && !releases);
 QUAD end=0;
 assert(!__fdhook(second,__FDH_SEEK,(IPTR)&end,SEEK_END,0,&error) && end==buffer.size);
 assert(FD_AcquireBuffer(second,&provider,&lease,&payload,&registry)==0 && payload==buffer.payload);
 assert(!__fdhook(second,__FDH_CLOSE,0,0,0,&error) && !releases);
 assert(!FD_Release(&lease) && releases==1);
 assert(!atomic_load(&fd_library_refs));
}
int main(void){
 init_lock(&registry.fd_Lock);init_lock(&syscall_semaphore);init_lock(&baselist_semaphore);
 registry.fd_LibNode.lib_Version=51;
 test_exported_buffer();
 assert(native_socket_setup());
 /* A reservation without a provider has no admitted callable hook table. */
 int orphan=1;LONG orphan_fd,error;
 assert(FD_Alloc(3,4,&orphan,&orphan_fd)==0);
 assert(__fdhook(orphan_fd,__FDH_READ,0,0,0,&error)==-1&&error==EBADF);
 assert(FD_Free(orphan_fd,4,&registry)==0);
 struct fd_hooks seek_hooks={.fdh_Version=FD_HOOKS_VERSION,.fdh_lseek=provider_seek};
 QUAD size=INT64_C(0x200000000), offset=-8;
 LONG seek_fd;
 assert(!FD_SetOwnerHooks(4,&seek_hooks));
 assert(!FD_Alloc(3,4,&size,&seek_fd));
 assert(!__fdhook(seek_fd,__FDH_SEEK,(IPTR)&offset,2,0,&error) && !error);
 assert(offset==size-8);
 assert(!FD_Free(seek_fd,4,&registry));
 assert(!FD_SetOwnerHooks(4,NULL));
 struct SocketBase base={.thisTask=FindTask(NULL)};
 struct aros_socket_request invalid={.version=99};
 assert(NativeSocket(NULL,sizeof(invalid),&base)==EINVAL);
 assert(NativeSocket(&invalid,sizeof(invalid),&base)==EINVAL);
 pthread_t thread;void *value;
 assert(!pthread_create(&thread,NULL,creator,NULL));assert(!pthread_join(thread,&value));
 LONG fd=(intptr_t)value;char buffer[16];
 assert(fd>=3&&native_objects==1&&master.lib_OpenCnt==11&&!contexts);
 assert(__fdhook(fd,__FDH_READ,(IPTR)buffer,16,0,&error)==16&&error==0);
 /* Execute the actual ioctl wrapper too: no borrowed provider precheck. */
 int available=-1, nonblocking=1;
 assert(fixture_ioctl(fd,(int)FIONREAD,&available)==0&&available==0);
 assert(fixture_ioctl(fd,(int)FIONBIO,&nonblocking)==0);
 assert(fixture_ioctl(-1,(int)FIONREAD,&available)==-1&&errno==EBADF);

 QUAD position = 0;
 assert(__fdhook(fd,__FDH_SEEK,(IPTR)&position,0,0,&error)==-1 && error==ESPIPE);
 assert(__fdhook(fd,__FDH_WRITE,(IPTR)buffer,16,0,&error)==16&&error==0);
 assert(__fdhook(fd,__FDH_FCNTL,F_SETFD,FD_CLOEXEC,0,&error)==0);
 assert(__fdhook(fd,__FDH_FCNTL,F_GETFD,0,0,&error)==FD_CLOEXEC);
 LONG cloexec=__fdhook(fd,__FDH_FCNTL,F_DUPFD_CLOEXEC,35,0,&error);
 assert(cloexec>=35 && !error);
 assert(__fdhook(cloexec,__FDH_FCNTL,F_GETFD,0,0,&error)==FD_CLOEXEC);
 assert(__fdhook(cloexec,__FDH_CLOSE,0,0,0,&error)==0);
 assert(__fdhook(fd,__FDH_FCNTL,F_DUPFD_CLOEXEC,(IPTR)-1,0,&error)==-1 && error==EINVAL);
 LONG clone=__fdhook(fd,__FDH_FCNTL,F_DUPFD,20,0,&error);
 assert(clone>=20&&error==0&&native_objects==1);
 assert(__fdhook(clone,__FDH_FCNTL,F_GETFD,0,0,&error)==0);
 call(AROS_SOCKET_CLOSE,fd,0,0,0);
 assert(__fdhook(clone,__FDH_READ,(IPTR)buffer,16,0,&error)==16);
 assert(__fdhook(clone,__FDH_FCNTL,F_SETFL,O_NONBLOCK,0,&error)==0);
 assert(__fdhook(clone,__FDH_FCNTL,F_GETFL,0,0,&error)&O_NONBLOCK);
 struct aros_socket_request r={.version=AROS_SOCKET_ABI,.fd=clone,.operation=AROS_SOCKET_POLL,.arg0=2,.arg1=1250};
 assert(native_call(&r,NULL)==0&&r.result==2&&!contexts);
 r.operation=AROS_SOCKET_RECV;r.buffer=buffer;r.length=(IPTR)INT_MAX+1;
 assert(native_call(&r,NULL)==EINVAL&&!contexts);
 atomic_store(&fail_context,1);r.length=16;
 assert(native_call(&r,NULL)==ENOMEM&&!contexts);
 struct linger linger={1,3};
 r.operation=AROS_SOCKET_SETOPT;r.arg0=SOL_SOCKET;r.arg1=SO_LINGER;r.buffer=&linger;r.length=sizeof(linger);
 assert(native_call(&r,NULL)==ENOPROTOOPT);
 assert(__fdhook(clone,__FDH_CLOSE,0,0,0,&error)==0&&error==0);
 assert(!native_objects&&master.lib_OpenCnt==10&&!contexts);
 /* A blocked call retains the old reservation/object while its exact integer
    is retired and reused. Neither old admission nor finalization can touch the
    replacement. The creator is already gone. */
 fd=call(AROS_SOCKET_CREATE,0,2,SOCK_STREAM,0);
 atomic_store(&pause_recv,1);
 int before=atomic_load(&destroyed_count);
 assert(!pthread_create(&thread,NULL,reader,(void *)(intptr_t)fd));
 while(!atomic_load(&recv_entered)){}
 call(AROS_SOCKET_CLOSE,fd,0,0,0);
 assert(atomic_load(&destroyed_count)==before&&!native_socket_withdraw());
 LONG replacement=call(AROS_SOCKET_CREATE,0,2,SOCK_DGRAM,0);
 assert(replacement==fd);
 atomic_store(&release_recv,1);assert(!pthread_join(thread,NULL));
 assert(atomic_load(&destroyed_count)==before+1&&FD_GetOwner(replacement)==FD_OWNER_NATIVE_SOCKET);
 assert(__fdhook(replacement,__FDH_WRITE,(IPTR)buffer,16,0,&error)==16);
 /* Atomic allocation-backed duplication is stress-tested against actual
    registry reservation code rather than a guessed free descriptor number. */
 for(int i=0;i<1000;i++){
  LONG copy=call(AROS_SOCKET_CLONE,replacement,3,0,0);
  assert(copy!=replacement);
  call(AROS_SOCKET_CLOSE,copy,0,0,0);
 }
 pthread_t cloners[4];
 for(int i=0;i<4;i++)assert(!pthread_create(&cloners[i],NULL,cloner,(void *)(intptr_t)replacement));
 for(int i=0;i<4;i++)assert(!pthread_join(cloners[i],NULL));
 assert(__fdhook(replacement,__FDH_CLOSE,0,0,0,&error)==0);
 assert(!native_objects&&!contexts&&!fd_library_refs&&master.lib_OpenCnt==10);
 /* Failure paths consume an unregistered socket and roll back allocations. */
 for(int failure=1;failure<=2;failure++){
  atomic_store(&fail_at,failure);
  r=(struct aros_socket_request){.version=1,.operation=AROS_SOCKET_CREATE,.arg0=2,.arg1=SOCK_STREAM};
  assert(native_call(&r,NULL)==ENOMEM&&!native_objects&&!contexts);
 }
 master.lib_OpenCnt=UINT16_MAX;
 r=(struct aros_socket_request){.version=1,.operation=AROS_SOCKET_CREATE,.arg0=2,.arg1=SOCK_STREAM};
 assert(native_call(&r,NULL)==EMFILE&&!native_objects&&!contexts);
 master.lib_OpenCnt=10;
 assert(native_socket_withdraw());
 assert(atomic_load(&created_count)==atomic_load(&destroyed_count));
 FreeVec(registry.fd_Table);
 assert(!atomic_load(&allocations));
 printf("PASS native owned socket admission/lifetime: creator exit, blocked read + close/reuse, 1512 clones (4 concurrent workers), failures; transport modeled\n");
 return 0;
}
