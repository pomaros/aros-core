/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/entropy.h>
#include <proto/debug.h>
#include <libraries/debug.h>
#include <dos/dosextens.h>
#include <resources/entropy.h>
#include <libraries/keyring.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "keyring_private.h"
#include LC_LIBDEFS_FILE

#define POLICY "SYS:Prefs/Keyring.allowlist"

static void wipe(void *p, size_t n) { volatile unsigned char *b=p; while(n--) *b++=0; }
static int process(void) { return FindTask(NULL)->tc_Node.ln_Type == NT_PROCESS; }
struct kr_quiet {
    struct Process *process;
    APTR previous;
};
static struct kr_quiet quiet_begin(void)
{
    struct kr_quiet scope={NULL,NULL};
    if(process()) {
        scope.process=(struct Process *)FindTask(NULL);
        scope.previous=scope.process->pr_WindowPtr;
        scope.process->pr_WindowPtr=(APTR)-1;
    }
    return scope;
}
static void quiet_end(struct kr_quiet scope)
{
    if(scope.process) scope.process->pr_WindowPtr=scope.previous;
}
/* One decrypted session is serialized across callers. The retained lock and
 * canonical paths bind each operation to its starting HOME, even if the assign
 * changes during an unlock prompt or write. Shared memory gives containment. */
static void reset_home(struct KeyringBase *base)
{
    kr_engine_lock(base->engine);
    base->prompted=0;
    wipe(base->home,sizeof(base->home)); wipe(base->vault,sizeof(base->vault));
    wipe(base->pending,sizeof(base->pending)); wipe(base->backup,sizeof(base->backup));
    /* Bare Exec tasks cannot issue DOS calls. Retain the old lock until the
     * next process entry; the plaintext cache and all usable paths are gone. */
    if(base->home_lock && process()) { UnLock(base->home_lock); base->home_lock=0; }
}
static int under_system(const char *home,const char *system)
{
    size_t i,n=strlen(system),h=strlen(home);
    if(!n || h<n) return 0;
    for(i=0;i<n;i++) {
        unsigned char a=home[i],b=system[i];
        if(a>='A' && a<='Z') a+='a'-'A';
        if(b>='A' && b<='Z') b+='a'-'A';
        if(a!=b) return 0;
    }
    return h==n || system[n-1]==':' || system[n-1]=='/' || home[n]=='/';
}
static int bind_home(struct KeyringBase *base)
{
    BPTR home=0,system=0; char canonical[1024],sys[1024]; struct FileInfoBlock fib;
    int rc=KR_ERR_CONTEXT;
    struct kr_quiet scope=quiet_begin();
    if(!process()) goto fail;
    home=Lock("HOME:",ACCESS_READ); if(!home) goto fail;
    system=Lock("SYS:",ACCESS_READ); if(!system) goto fail;
    rc=KR_ERR_IO;
    if(!Examine(home,&fib) || fib.fib_DirEntryType<=0 ||
       !Examine(system,&fib) || fib.fib_DirEntryType<=0 ||
       !NameFromLock(home,canonical,sizeof(canonical)) ||
       !NameFromLock(system,sys,sizeof(sys))) goto fail;
    UnLock(system); system=0;
    rc=KR_ERR_CONTEXT;
    if(under_system(canonical,sys)) goto fail;
    rc=KR_ERR_IO;
    if(strlen(canonical)+sizeof("Keyring.vault.old")+1>sizeof(base->vault)) goto fail;
    if(base->home_lock && base->home[0] && SameLock(home,base->home_lock)==LOCK_SAME &&
       !strcmp(canonical,base->home)) { UnLock(home); quiet_end(scope); return 0; }
    reset_home(base);
    base->home_lock=home; home=0;
    strcpy(base->home,canonical); strcpy(base->vault,canonical);
    if(!AddPart(base->vault,"Keyring.vault",sizeof(base->vault))) goto fail;
    snprintf(base->pending,sizeof(base->pending),"%s.new",base->vault);
    snprintf(base->backup,sizeof(base->backup),"%s.old",base->vault);
    quiet_end(scope); return 0;
fail:
    if(home) UnLock(home);
    if(system) UnLock(system);
    reset_home(base); quiet_end(scope); return rc;
}
static int random_bytes(void *ctx, unsigned char *out, size_t n)
{
    struct Library *EntropyBase=OpenResource(ENTROPYNAME);
    (void)ctx;
    if (!EntropyBase) return -1;
    while(n) { ULONG k=n>256?256:n; if(GetEntropy(out,k)!=(LONG)k) return -1; out+=k; n-=k; }
    return 0;
}
static int read_file(const char *name, unsigned char **bytes, size_t *size, size_t max)
{
    BPTR f; LONG n; size_t used=0; unsigned char *b;
    *bytes=NULL; *size=0;
    f=Open(name, MODE_OLDFILE);
    if(!f) return IoErr()==ERROR_OBJECT_NOT_FOUND?KR_NOTFOUND:KR_ERR_IO;
    b=malloc(max+1);
    if(!b) { Close(f); return KR_NOMEM; }
    while(used<=max && (n=Read(f,b+used,max+1-used))>0) used+=n;
    { int closed=Close(f); if(used>max || n<0 || !closed) { wipe(b,max+1); free(b); return KR_ERR_IO; } }
    *bytes=b; *size=used; return 0;
}
static int digest_file(const char *path, unsigned char digest[32])
{
    mbedtls_sha256_context sha; BPTR f; LONG n; unsigned char buf[4096]; int rc=KR_ERR_IO;
    f=Open(path,MODE_OLDFILE); if(!f) return rc;
    mbedtls_sha256_init(&sha);
    if(mbedtls_sha256_starts(&sha,0)) goto done;
    while((n=Read(f,buf,sizeof(buf)))>0) if(mbedtls_sha256_update(&sha,buf,n)) goto done;
    if(n==0 && !mbedtls_sha256_finish(&sha,digest)) rc=0;
done:
    if(!Close(f)) rc=KR_ERR_IO;
    mbedtls_sha256_free(&sha); wipe(buf,sizeof(buf)); return rc;
}
/* CLI attribution uses the executable's directory and command basename.
 * These task structures and disk bytes are cooperative containment metadata. */
static int caller(unsigned char owner[32], char *path, size_t cap)
{
    char command[512]; BPTR lock; unsigned char hash[32]; mbedtls_sha256_context sha; int rc=KR_OWNER;
    if(!process()) return rc;
    /* A program started from Workbench has no CLI: its process is named
     * after the program, in the program's directory. */
    if(!GetProgramName(command,sizeof(command)) || !command[0]) {
        const char *name=FindTask(NULL)->tc_Node.ln_Name;
        if(!name || !*name || strlen(name)>=sizeof(command)) return rc;
        strcpy(command,name);
    }
    lock=GetProgramDir(); if(!lock || !NameFromLock(lock,path,cap) || !AddPart(path,FilePart(command),cap)) return rc;
    lock=Lock(path,ACCESS_READ); if(!lock) return rc;
    if(!NameFromLock(lock,path,cap)) { UnLock(lock); return rc; } UnLock(lock);
    if(digest_file(path,hash)) return rc;
    mbedtls_sha256_init(&sha);
    if(!mbedtls_sha256_starts(&sha,0) && !mbedtls_sha256_update(&sha,(const unsigned char *)path,strlen(path)+1) &&
       !mbedtls_sha256_update(&sha,hash,sizeof(hash)) && !mbedtls_sha256_finish(&sha,owner)) rc=0;
    mbedtls_sha256_free(&sha); wipe(hash,sizeof(hash)); return rc;
}
/* The digest pins a deployed file. Exec registration pins a module pointer.
 * The loader does not provide an authenticated connection between those facts. */
struct service_path { const char *name,*path; ULONG service; int device; };
static const struct service_path services[]={
 {"bwfm.device","SYS:Devs/Networks/bwfm.device",KR_SERVICE_WIFI,1},
 {"WiFiEditor.mcc","SYS:Classes/Zune/WiFiEditor.mcc",KR_SERVICE_WIFI,0},
 {"NetworkEditor.mcc","SYS:Classes/Zune/NetworkEditor.mcc",KR_SERVICE_WIFI,0},
 {"bluetooth.library","SYS:Libs/bluetooth.library",KR_SERVICE_BLUETOOTH,0},
 {"BluetoothEditor.mcc","SYS:Classes/Zune/BluetoothEditor.mcc",KR_SERVICE_BLUETOOTH,0},
 {NULL,"SYS:Prefs/WiFi",KR_SERVICE_WIFI,0},
 {NULL,"SYS:Prefs/Network",KR_SERVICE_WIFI,0},
 {NULL,"SYS:C/WirelessManager",KR_SERVICE_WIFI,0}
};
static int module_callsite(APTR pc, const char *expected)
{
    struct Library *DebugBase=OpenLibrary("debug.library",0);
    char *name=NULL; int ok=0;
    struct TagItem tags[]={{DL_ModuleName,(IPTR)&name},{TAG_DONE,0}};
    if(!DebugBase) return 0;
    if(DecodeLocationA(pc,tags) && name && !strcmp(FilePart(name),expected)) ok=1;
    CloseLibrary(DebugBase); return ok;
}
static int allowed_digest(const char *path)
{
    unsigned char digest[32],*policy; char hex[65]; size_t n,i,start=0; int ok=0;
    if(digest_file(path,digest) || read_file(POLICY,&policy,&n,16384)) return 0;
    for(i=0;i<32;i++) sprintf(hex+2*i,"%02x",digest[i]);
    for(i=0;i<=n;i++) if(i==n || policy[i]=='\n') {
        size_t len=i-start;
        if(len==65+strlen(path) && !memcmp(policy+start,hex,64) && policy[start+64]==' ' && !memcmp(policy+start+65,path,strlen(path))) ok=1;
        start=i+1;
    }
    kr_engine_dispose(policy,n); wipe(digest,sizeof(digest)); return ok;
}
/* Seal, write beside, read back, then replace: a power cut leaves the old
 * vault or its backup. The committed engine replaces *slot. */
static int commit_to(struct kr_engine **slot, struct kr_engine *candidate,
    const char *vault, const char *pending, const char *backup)
{
    unsigned char *sealed=NULL,*check=NULL; size_t n=0,cn=0; BPTR f,old; int rc=kr_engine_seal(candidate,&sealed,&n),had=0;
    if(rc) return rc;
    f=Open(pending,MODE_NEWFILE); if(!f) { rc=KR_ERR_IO; goto done; }
    rc=Write(f,sealed,n)==(LONG)n?0:KR_ERR_IO;
    if(!Flush(f)) rc=KR_ERR_IO;
    if(!Close(f)) rc=KR_ERR_IO;
    if(rc || read_file(pending,&check,&cn,KR_VAULT_MAX) || cn!=n || memcmp(check,sealed,n)) { rc=KR_ERR_IO; goto done; }
    old=Lock(vault,ACCESS_READ);
    if(old) { UnLock(old); had=1; if(!DeleteFile(backup) && IoErr()!=ERROR_OBJECT_NOT_FOUND) { rc=KR_ERR_IO; goto done; }
        if(!Rename(vault,backup)) { rc=KR_ERR_IO; goto done; } }
    else if(IoErr()!=ERROR_OBJECT_NOT_FOUND) { rc=KR_ERR_IO; goto done; }
    if(!Rename(pending,vault)) { if(had) Rename(backup,vault); rc=KR_ERR_IO; goto done; }
    kr_engine_free(*slot); *slot=candidate;
    /* A leftover sealed backup is a recoverable committed transaction. */
    if(had) DeleteFile(backup);
done:
    DeleteFile(pending); kr_engine_dispose(sealed,n); kr_engine_dispose(check,cn); return rc;
}
static int commit(struct KeyringBase *base, struct kr_engine *candidate)
{
    return commit_to(&base->engine,candidate,base->vault,base->pending,base->backup);
}
static int unlock(struct KeyringBase *base)
{
    unsigned char pass[1024],*sealed=NULL; size_t pn=0,n=0; int rc,recovery=0; struct kr_engine *candidate;
    if(!kr_engine_is_locked(base->engine)) return 0;
    if(!process()) return KR_ERR_CONTEXT;
    rc=kr_prompt_passphrase(pass,sizeof(pass),&pn);
    if(rc || !pn || pn>sizeof(pass)) { wipe(pass,sizeof(pass)); return KR_ERR_INPUT; }
    rc=read_file(base->vault,&sealed,&n,KR_VAULT_MAX);
    if(rc==KR_NOTFOUND) {
        /* Recovery of interrupted replace; an absent vault is fresh only if
         * both committed paths are absent. Corruption always fails closed. */
        rc=read_file(base->backup,&sealed,&n,KR_VAULT_MAX);
        if(rc==0) recovery=1;
        if(rc==KR_NOTFOUND) rc=0;
    }
    candidate=kr_engine_new(random_bytes,NULL);
    if(!candidate) rc=KR_NOMEM;
    if(!rc) rc=kr_engine_unlock(candidate,pass,pn,sealed,n);
    if(!rc && recovery && !Rename(base->backup,base->vault)) rc=KR_ERR_IO;
    wipe(pass,sizeof(pass)); kr_engine_dispose(sealed,n);
    if(!rc) {
        if(!n) { rc=commit(base,candidate); if(!rc) candidate=NULL; }
        else { kr_engine_free(base->engine); base->engine=candidate; candidate=NULL; }
    }
    kr_engine_free(candidate); return rc;
}
/* The machine store lives on the data volume, beside the users' homes and
 * off SYS: (D-127). Its key is 32 random bytes made on first use; the
 * allowlisted service contexts are what limit who reads it. */
#define MACHINE_DIR     "USERS:.Machine"
#define MACHINE_KEY     MACHINE_DIR "/Keyring.key"
#define MACHINE_VAULT   MACHINE_DIR "/Keyring.vault"
#define MACHINE_PENDING MACHINE_VAULT ".new"
#define MACHINE_BACKUP  MACHINE_VAULT ".old"
static int machine_key(unsigned char key[32])
{
    unsigned char *bytes=NULL; size_t n=0; BPTR f, dir; int rc;
    /* The drawer first: inside a missing one, DOS answers "directory not
     * found" for the key, not "object not found". */
    dir=Lock(MACHINE_DIR,ACCESS_READ);
    if(!dir) { if(IoErr()!=ERROR_OBJECT_NOT_FOUND || !(dir=CreateDir(MACHINE_DIR))) return KR_ERR_IO; }
    UnLock(dir);
    rc=read_file(MACHINE_KEY,&bytes,&n,64);
    if(!rc) { rc=n==32?0:KR_ERR_IO; if(!rc) memcpy(key,bytes,32); kr_engine_dispose(bytes,n); return rc; }
    if(rc!=KR_NOTFOUND) return rc;
    if(random_bytes(NULL,key,32)) return KR_ERR_ENTROPY;
    f=Open(MACHINE_KEY,MODE_NEWFILE); if(!f) return KR_ERR_IO;
    rc=Write(f,key,32)==32?0:KR_ERR_IO;
    if(!Flush(f)) rc=KR_ERR_IO;
    if(!Close(f)) rc=KR_ERR_IO;
    if(!rc && (read_file(MACHINE_KEY,&bytes,&n,64) || n!=32 || memcmp(bytes,key,32))) rc=KR_ERR_IO;
    kr_engine_dispose(bytes,n);
    if(rc) DeleteFile(MACHINE_KEY);
    return rc;
}
static int ensure_machine(struct KeyringBase *base)
{
    unsigned char key[32],*sealed=NULL; size_t n=0; int rc,recovery=0; struct kr_engine *candidate;
    if(!kr_engine_is_locked(base->machine)) return 0;
    if(!process()) return KR_ERR_CONTEXT;
    rc=machine_key(key); if(rc) { wipe(key,sizeof(key)); return rc; }
    rc=read_file(MACHINE_VAULT,&sealed,&n,KR_VAULT_MAX);
    if(rc==KR_NOTFOUND) {
        rc=read_file(MACHINE_BACKUP,&sealed,&n,KR_VAULT_MAX);
        if(rc==0) recovery=1;
        if(rc==KR_NOTFOUND) rc=0;
    }
    candidate=kr_engine_new(random_bytes,NULL);
    if(!candidate) rc=KR_NOMEM;
    if(!rc) rc=kr_engine_unlock(candidate,key,sizeof(key),sealed,n);
    if(!rc && recovery && !Rename(MACHINE_BACKUP,MACHINE_VAULT)) rc=KR_ERR_IO;
    wipe(key,sizeof(key)); kr_engine_dispose(sealed,n);
    if(!rc) {
        if(!n) { rc=commit_to(&base->machine,candidate,MACHINE_VAULT,MACHINE_PENDING,MACHINE_BACKUP); if(!rc) candidate=NULL; }
        else { kr_engine_free(base->machine); base->machine=candidate; candidate=NULL; }
    }
    kr_engine_free(candidate); return rc;
}
static int ensure(struct KeyringBase *base)
{
    if(!kr_engine_is_locked(base->engine)) return 0;
    if(base->prompted) return KR_LOCKED;
    base->prompted=1; return unlock(base);
}
static int valid_name(const unsigned char *name)
{
    size_t n;
    if(!name) return 0;
    for(n=0;n<=KR_NAME_MAX;n++) if(!name[n]) return n>0;
    return 0;
}
static int context_owner(struct KeyringBase *base, APTR token, unsigned char out[32])
{
    struct kr_context *c;
    for(c=base->contexts;c;c=c->next) if(c==(struct kr_context *)token) { memcpy(out,c->owner,32); return 0; }
    return KR_ERR_CONTEXT;
}
static int operation(struct KeyringBase *base, APTR token, int service, int op, const char *name, void *bytes, ULONG *size, ULONG index)
{
    unsigned char owner[32]; char path[1024]; size_t n=size?*size:0; int rc; struct kr_engine *candidate=NULL;
    struct kr_quiet scope=quiet_begin();
    ObtainSemaphore(&base->mutex);
    if(service) {
        /* Services read and write the machine store: no HOME:, no prompt. */
        rc=context_owner(base,token,owner);
        if(!rc) rc=ensure_machine(base);
    } else {
        rc=bind_home(base);
        if(!rc) rc=caller(owner,path,sizeof(path));
        if(!rc) rc=ensure(base);
    }
    if(rc) goto done;
    if(op==0 || op==2) {
        candidate=kr_engine_clone(service?base->machine:base->engine); if(!candidate) { rc=KR_NOMEM; goto done; }
        rc=op==0?kr_engine_store(candidate,owner,name,bytes,n):kr_engine_delete(candidate,owner,name);
        if(!rc) {
            rc=service?commit_to(&base->machine,candidate,MACHINE_VAULT,MACHINE_PENDING,MACHINE_BACKUP):commit(base,candidate);
            if(!rc) candidate=NULL;
        }
    } else if(op==1) { if(!size) { rc=KR_BOUNDS; goto done; } rc=kr_engine_fetch(service?base->machine:base->engine,owner,name,bytes,&n); *size=n; }
    else { if(!size) { rc=KR_BOUNDS; goto done; } rc=kr_engine_enumerate(service?base->machine:base->engine,owner,index,bytes,&n); *size=n; }
done:
    kr_engine_free(candidate); wipe(owner,sizeof(owner)); ReleaseSemaphore(&base->mutex); quiet_end(scope); return rc;
}
static int init(struct KeyringBase *base)
{
    InitSemaphore(&base->mutex); base->contexts=NULL; base->prompted=0;
    base->home_lock=0; base->home[0]=base->vault[0]=base->pending[0]=base->backup[0]=0;
    base->engine=kr_engine_new(random_bytes,NULL);
    base->machine=kr_engine_new(random_bytes,NULL);
    return base->engine!=NULL && base->machine!=NULL;
}
ADD2INITLIB(init,0)

AROS_LH3(LONG, KR_Store,
    AROS_LHA(CONST_STRPTR, name, A0),
    AROS_LHA(CONST_APTR, bytes, A1),
    AROS_LHA(ULONG, size, D0),
    struct KeyringBase *, base, 5, Keyring)
{
    AROS_LIBFUNC_INIT
    return operation(base,NULL,0,0,name,(void *)bytes,&size,0);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, KR_Fetch,
    AROS_LHA(CONST_STRPTR, name, A0),
    AROS_LHA(APTR, bytes, A1),
    AROS_LHA(ULONG *, size, A2),
    struct KeyringBase *, base, 6, Keyring)
{
    AROS_LIBFUNC_INIT
    return operation(base,NULL,0,1,name,bytes,size,0);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, KR_Delete,
    AROS_LHA(CONST_STRPTR, name, A0),
    struct KeyringBase *, base, 7, Keyring)
{
    AROS_LIBFUNC_INIT
    return operation(base,NULL,0,2,name,NULL,NULL,0);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, KR_Enumerate,
    AROS_LHA(ULONG, index, D0),
    AROS_LHA(STRPTR, name, A0),
    AROS_LHA(ULONG *, size, A1),
    struct KeyringBase *, base, 8, Keyring)
{
    AROS_LIBFUNC_INIT
    return operation(base,NULL,0,3,NULL,name,size,index);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, KR_Unlock,
    struct KeyringBase *, base, 9, Keyring)
{
    AROS_LIBFUNC_INIT
    int rc; struct kr_quiet scope=quiet_begin(); ObtainSemaphore(&base->mutex); rc=bind_home(base); if(!rc) { base->prompted=1; rc=unlock(base); } ReleaseSemaphore(&base->mutex); quiet_end(scope); return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, KR_Lock,
    struct KeyringBase *, base, 10, Keyring)
{
    AROS_LIBFUNC_INIT
    struct kr_quiet scope=quiet_begin(); ObtainSemaphore(&base->mutex); bind_home(base); kr_engine_lock(base->engine); base->prompted=1; ReleaseSemaphore(&base->mutex); quiet_end(scope);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(BOOL, KR_IsLocked,
    struct KeyringBase *, base, 11, Keyring)
{
    AROS_LIBFUNC_INIT
    BOOL locked; ObtainSemaphore(&base->mutex); locked=bind_home(base)!=0 || kr_engine_is_locked(base->engine); ReleaseSemaphore(&base->mutex); return locked;
    AROS_LIBFUNC_EXIT
}

AROS_LH2(APTR, KR_ServiceOpen,
    AROS_LHA(APTR, module, A0),
    AROS_LHA(ULONG, service, D0),
    struct KeyringBase *, base, 12, Keyring)
{
    AROS_LIBFUNC_INIT
    struct kr_context *c=NULL,*cursor; size_t i,count=0; unsigned char unused[32]; char actual[1024];
    struct kr_quiet scope=quiet_begin();
    ObtainSemaphore(&base->mutex);
    for(cursor=base->contexts;cursor;cursor=cursor->next) ++count;
    if(count>=64) goto service_done;
    for(i=0;i<sizeof(services)/sizeof(services[0]);i++) {
        const struct service_path *s=&services[i]; int registered=0;
        if(s->service!=service) continue;
        if(s->name && module) {
            Forbid();
            registered=(APTR)FindName(s->device?&SysBase->DeviceList:&SysBase->LibList,s->name)==module;
            Permit();
        } else if(!s->name && !module && !caller(unused,actual,sizeof(actual))) {
            BPTR a=Lock(actual,ACCESS_READ),b=Lock(s->path,ACCESS_READ);
            registered=a && b && SameLock(a,b)==LOCK_SAME;
            if(a) UnLock(a); if(b) UnLock(b);
        }
        if(registered && s->name && !module_callsite(__builtin_return_address(0),s->name)) registered=0;
        if(!registered || !allowed_digest(s->path)) continue;
        c=calloc(1,sizeof(*c)); if(!c) break;
        { const char *ns=service==KR_SERVICE_WIFI?"AROS:keyring:service:wifi:v1":"AROS:keyring:service:bluetooth:v1";
          if(mbedtls_sha256((const unsigned char *)ns,strlen(ns),c->owner,0)) { free(c); c=NULL; break; } }
        c->module=module; c->next=base->contexts; base->contexts=c; break;
    }
service_done:
    wipe(unused,sizeof(unused)); ReleaseSemaphore(&base->mutex); quiet_end(scope); return c;
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, KR_ServiceStore,
    AROS_LHA(APTR, context, A0),
    AROS_LHA(CONST_STRPTR, name, A1),
    AROS_LHA(CONST_APTR, bytes, A2),
    AROS_LHA(ULONG, size, D0),
    struct KeyringBase *, base, 13, Keyring)
{
    AROS_LIBFUNC_INIT
    return operation(base,context,1,0,name,(void *)bytes,&size,0);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, KR_ServiceFetch,
    AROS_LHA(APTR, context, A0),
    AROS_LHA(CONST_STRPTR, name, A1),
    AROS_LHA(APTR, bytes, A2),
    AROS_LHA(ULONG *, size, A3),
    struct KeyringBase *, base, 14, Keyring)
{
    AROS_LIBFUNC_INIT
    return operation(base,context,1,1,name,bytes,size,0);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, KR_ServiceDelete,
    AROS_LHA(APTR, context, A0),
    AROS_LHA(CONST_STRPTR, name, A1),
    struct KeyringBase *, base, 15, Keyring)
{
    AROS_LIBFUNC_INIT
    return operation(base,context,1,2,name,NULL,NULL,0);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, KR_ServiceClose,
    AROS_LHA(APTR, context, A0),
    struct KeyringBase *, base, 16, Keyring)
{
    AROS_LIBFUNC_INIT
    struct kr_context **link,*c;
    struct kr_quiet scope=quiet_begin();
    ObtainSemaphore(&base->mutex);
    bind_home(base);
    for(link=&base->contexts;(c=*link);link=&c->next) if(c==(struct kr_context *)context) {
        *link=c->next; wipe(c,sizeof(*c)); free(c); break;
    }
    ReleaseSemaphore(&base->mutex); quiet_end(scope);
    AROS_LIBFUNC_EXIT
}

AROS_LH5(LONG, KR_ServiceRekey,
    AROS_LHA(APTR, context, A0),
    AROS_LHA(CONST_STRPTR, oldname, A1),
    AROS_LHA(CONST_STRPTR, newname, A2),
    AROS_LHA(CONST_APTR, bytes, A3),
    AROS_LHA(ULONG, size, D0),
    struct KeyringBase *, base, 17, Keyring)
{
    AROS_LIBFUNC_INIT
    unsigned char owner[32]; struct kr_engine *candidate=NULL; int rc;
    struct kr_quiet scope=quiet_begin();
    ObtainSemaphore(&base->mutex);
    rc=bind_home(base);
    if(!rc) rc=context_owner(base,context,owner);
    if(!rc) rc=ensure(base);
    if(!rc && (!valid_name(oldname) || !valid_name(newname))) rc=KR_BOUNDS;
    if(!rc) { candidate=kr_engine_clone(base->engine); if(!candidate) rc=KR_NOMEM; }
    /* Remove on the isolated candidate first so a rename also fits a full
     * vault. Failed store/seal/I/O leaves the committed engine untouched. */
    if(!rc && strcmp((const char *)oldname,(const char *)newname)) {
        rc=kr_engine_delete(candidate,owner,(const char *)oldname);
        if(rc==KR_NOTFOUND) rc=0;
    }
    if(!rc) rc=kr_engine_store(candidate,owner,(const char *)newname,bytes,size);
    if(!rc) { rc=commit(base,candidate); if(!rc) candidate=NULL; }
    kr_engine_free(candidate); wipe(owner,sizeof(owner)); ReleaseSemaphore(&base->mutex); quiet_end(scope); return rc;
    AROS_LIBFUNC_EXIT
}
