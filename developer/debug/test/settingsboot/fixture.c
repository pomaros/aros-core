/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
   Distributed under the AROS Public License, version 1.1. */
#include "mock.h"
#include <filesystems/afsplus_ext_packet.h>
#include <filesystems/afsplus_aros.h>
#include <filesystems/filesystem_v2.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>

struct Library boot_dos = {50,84};
struct Library *DOSBase = &boot_dos;
static LONG dos_error;
static int force_collision, fail_state_open;
static int operation, clone_support, fail_write, locks, clones, read_only;
static struct MsgPort disk_port = {1}, ram_port = {0};
static char last_count[32];
int SettingsBootMain(void);
LONG IoErr(void) { return dos_error; }
LONG SetIoErr(LONG e) { LONG old = dos_error; dos_error = e; return old; }
static void mapped(const char *path, char *output)
{
    const char *rest = strchr(path, ':');
    assert(rest);
    snprintf(output, 2048, "%.*s/%s", (int)(rest - path), path, rest + 1);
}
static BOOL result(int n) { if (n) SetIoErr(errno == ENOENT ? ERROR_OBJECT_NOT_FOUND : 202); else SetIoErr(0); return !n; }
BPTR Lock(CONST_STRPTR path, LONG mode)
{
    char real[2048]; struct stat st; struct FileLock *lock;
    (void)mode; mapped(path, real);
    if (stat(real, &st)) { result(-1); return 0; }
    lock = calloc(1, sizeof(*lock)); assert(lock);
    lock->path = strdup(path); assert(lock->path);
    lock->fl_Task = !strncmp(path, "RAM:", 4) || !strncmp(path, "ENV:", 4) ? &ram_port : &disk_port;
    locks++; SetIoErr(0); return (BPTR)lock;
}
void UnLock(BPTR value)
{
    struct FileLock *lock = (void *)value;
    if (!lock) return;
    if (lock->iterator) closedir(lock->iterator);
    free(lock->path); free(lock); locks--;
}
BPTR CreateDir(CONST_STRPTR path) { char real[2048]; mapped(path, real); if (!result(mkdir(real, 0700))) return 0; return Lock(path, 0); }
BOOL DeleteFile(CONST_STRPTR path)
{
    char real[2048]; struct stat st; mapped(path, real);
    if (stat(real, &st)) return result(-1);
    return result(S_ISDIR(st.st_mode) ? rmdir(real) : unlink(real));
}
BOOL Rename(CONST_STRPTR from, CONST_STRPTR to) { char a[2048], b[2048]; mapped(from, a); mapped(to, b); if (!access(b, F_OK)) { SetIoErr(203); return 0; } return result(rename(a,b)); }
BOOL ReplaceObject(CONST_STRPTR from, CONST_STRPTR to) { char a[2048], b[2048]; mapped(from,a); mapped(to,b); return result(rename(a,b)); }
BPTR Open(CONST_STRPTR path, LONG mode) { char real[2048]; FILE *f; mapped(path,real); if (mode && fail_state_open && !strcmp(path,"RAM:Settings-Boot-State")) { SetIoErr(ERROR_NO_FREE_STORE); return 0; } f=fopen(real,mode ? "wb" : "rb"); if (!f) result(-1); return (BPTR)f; }
BOOL Flush(BPTR file) { return result(fflush((FILE *)file)); }
BOOL Close(BPTR file) { return result(fclose((FILE *)file)); }
LONG Read(BPTR file, void *buffer, LONG size) { size_t n=fread(buffer,1,size,(FILE *)file); if (!n && ferror((FILE *)file)) { SetIoErr(202); return -1; } return (LONG)n; }
LONG Write(BPTR file, const void *buffer, LONG size) { if (fail_write) { SetIoErr(221); return -1; } size_t n=fwrite(buffer,1,size,(FILE *)file); if (n!=(size_t)size) SetIoErr(221); return (LONG)n; }
void *AllocVec(size_t n, ULONG flags) { (void)flags; return malloc(n); }
void FreeVec(void *p) { free(p); }
void *AllocDosObject(ULONG kind, void *tags) { (void)kind; (void)tags; return calloc(1,sizeof(struct FileInfoBlock)); }
void FreeDosObject(ULONG kind, void *p) { (void)kind; free(p); }
BOOL Examine(BPTR value, struct FileInfoBlock *fib)
{
    struct FileLock *lock=(void *)value; char real[2048];
    mapped(lock->path,real); lock->iterator=opendir(real); fib->fib_DirEntryType=ST_USERDIR;
    return lock->iterator != NULL;
}
BOOL ExNext(BPTR value, struct FileInfoBlock *fib)
{
    struct FileLock *lock=(void *)value; struct dirent *entry; char path[2048],real[2048]; struct stat st;
    snprintf(path,sizeof(path),"%s%szz-collision.boot-new",lock->path,lock->path[strlen(lock->path)-1]==':' ? "" : "/");
    mapped(path,real);
    if (!access(real,F_OK) && force_collision && (!strcmp(lock->path,"ENVARC:") || !strcmp(lock->path,"RAM:Settings-At-Boot")) && lock->collision_cursor < 2)
    {
        strcpy(fib->fib_FileName,lock->collision_cursor++ == 0 ? "zz-collision.boot-new" : "zz-collision");
        fib->fib_DirEntryType=ST_FILE; return TRUE;
    }
    while ((entry=readdir(lock->iterator)))
    {
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..") || (force_collision && !strncmp(entry->d_name,"zz-collision",12))) continue;
        strcpy(fib->fib_FileName,entry->d_name);
        snprintf(path,sizeof(path),"%s%s%s",lock->path,lock->path[strlen(lock->path)-1]==':' ? "" : "/",entry->d_name);
        mapped(path,real); assert(!lstat(real,&st));
        fib->fib_DirEntryType=S_ISDIR(st.st_mode) ? ST_USERDIR : S_ISREG(st.st_mode) ? ST_FILE : 3;
        return TRUE;
    }
    SetIoErr(ERROR_NO_MORE_ENTRIES); return FALSE;
}
LONG DoPkt(struct MsgPort *port, LONG type, SIPTR a, SIPTR b, SIPTR c, SIPTR d, SIPTR e)
{
    struct AfsplusExtRequest *request=(void *)a;
    (void)port; (void)b; (void)c; (void)d; (void)e; assert(type==ACTION_AFSPLUS_EXT);
    if (!clone_support) { SetIoErr(209); return FALSE; }
    if (request->operation==AFSPLUS_EXT_CAPABILITIES) { ((struct AfsplusArosCapabilities *)request->buffer)->capabilities = FSV2_CAP_CLONE_FILE; ((struct AfsplusArosCapabilities *)request->buffer)->mount_mode = read_only ? AFSPLUS_AROS_MOUNT_READ_ONLY : AFSPLUS_AROS_MOUNT_READ_WRITE; return TRUE; }
    if (request->operation==AFSPLUS_EXT_CLONE_FILE)
    {
        struct FileLock *source=(void *)(uintptr_t)request->object[0], *target=(void *)(uintptr_t)request->object[1];
        char path[2048]; UBYTE buffer[16384]; LONG n; BPTR in,out;
        snprintf(path,sizeof(path),"%s/%.*s",target->path,(int)request->name_length[1],request->name1);
        in=Open(source->path,MODE_OLDFILE); out=Open(path,MODE_NEWFILE); assert(in && out);
        while ((n=Read(in,buffer,sizeof(buffer)))>0) assert(Write(out,buffer,n)==n);
        assert(Close(in) && Close(out)); clones++; return TRUE;
    }
    assert(0); return FALSE;
}
struct RDArgs *ReadArgs(CONST_STRPTR pattern, IPTR *args, void *unused) { (void)pattern; (void)unused; args[operation]=1; args[2]=1; return calloc(1,sizeof(struct RDArgs)); }
void FreeArgs(struct RDArgs *a) { free(a); }
LONG SetVar(CONST_STRPTR name, CONST_STRPTR value, LONG n, ULONG flags) { (void)n; (void)flags; if (!strcmp(name,"SettingsBootRestored")) strcpy(last_count,value); return 1; }
LONG Printf(CONST_STRPTR pattern, ...) { (void)pattern; return 1; }
static void put(const char *path, const char *text) { FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(text,1,strlen(text),f)==strlen(text)); assert(!fclose(f)); }
static void expect(const char *path, const char *text) { char b[128]; FILE *f=fopen(path,"rb"); assert(f); size_t n=fread(b,1,sizeof(b)-1,f); b[n]=0; assert(!strcmp(text,b)); assert(!fclose(f)); }
int main(void)
{
    assert(!mkdir("RAM",0700)); assert(!mkdir("ENV",0700)); assert(!mkdir("ENVARC",0700)); assert(!mkdir("SYS",0700)); assert(!mkdir("SYS/Prefs",0700));
    boot_dos.lib_Revision=83;
    assert(SettingsBootMain()==RETURN_FAIL && IoErr()==ERROR_INVALID_RESIDENT_LIBRARY);
    assert(access("RAM/Settings-Boot-State",F_OK) && access("RAM/Settings-Boot-Busy",F_OK));
    boot_dos.lib_Version=49;boot_dos.lib_Revision=99;
    assert(SettingsBootMain()==RETURN_FAIL && IoErr()==ERROR_INVALID_RESIDENT_LIBRARY);
    boot_dos.lib_Version=50;boot_dos.lib_Revision=84;
    assert(!mkdir("ENVARC/Sys",0700)); assert(!mkdir("ENVARC/Zune",0700)); assert(!mkdir("ENVARC/Network",0700));
    put("ENVARC/Sys/font","font-at-boot"); put("ENVARC/Sys/screenmode","screen-at-boot"); put("ENVARC/Zune/global","zune-at-boot"); put("ENVARC/Network/net","net-at-boot"); put("ENVARC/empty","");
    FILE *large=fopen("ENVARC/large","wb"); assert(large); for (int i=0;i<300000;i++) assert(fputc(i%251,large)!=EOF); assert(!fclose(large));
    assert(!mkdir("RAM/Settings-Boot-Busy",0700));
    operation=0; assert(SettingsBootMain()==RETURN_ERROR); assert(locks==0); assert(access("RAM/Settings-Boot-State",F_OK));
    assert(!rmdir("RAM/Settings-Boot-Busy"));
    operation=1; assert(SettingsBootMain()==RETURN_ERROR); assert(locks==0);
    operation=0; assert(SettingsBootMain()==RETURN_OK); assert(locks==0); assert(clones==0);
    assert(SettingsBootMain()==RETURN_ERROR && IoErr()==ERROR_OBJECT_EXISTS && locks==0);
    put("ENV/empty","changed"); operation=1; assert(SettingsBootMain()==RETURN_OK); assert(locks==0); assert(!strcmp(last_count,"6")); expect("ENV/Sys/font","font-at-boot"); expect("ENV/empty","");
    struct stat st; assert(!stat("ENV/large",&st) && st.st_size==300000);
    /* Failed writes preserve the old destination and report failure. */
    put("ENV/empty","preserved"); fail_write=1; assert(SettingsBootMain()==RETURN_ERROR); assert(locks==0); expect("ENV/empty","preserved");
    /* Incomplete capture cannot be used as a boot state. */
    assert(DeleteFile("RAM:Settings-Boot-State"));
    operation=0; assert(SettingsBootMain()==RETURN_ERROR); assert(locks==0); assert(access("RAM/Settings-Boot-State",F_OK)); operation=1; assert(SettingsBootMain()==RETURN_ERROR);
    fail_write=0; clone_support=1; operation=0; assert(SettingsBootMain()==RETURN_OK); assert(clones==6 && locks==0); expect("RAM/Settings-Boot-State","SYS:Prefs/Settings-At-Boot");
    operation=1; assert(SettingsBootMain()==RETURN_OK); assert(locks==0);
    assert(DeleteFile("RAM:Settings-Boot-State"));
    read_only=1; operation=0; assert(SettingsBootMain()==RETURN_OK && clones==6 && locks==0);
    expect("RAM/Settings-Boot-State","RAM:Settings-At-Boot");
    operation=1; assert(SettingsBootMain()==RETURN_OK && locks==0);
    assert(DeleteFile("RAM:Settings-Boot-State"));
    put("ENVARC/zz-collision.boot-new","suffix-value"); put("ENVARC/zz-collision","plain-value");
    assert(!mkdir("ENVARC/~#0",0700));
    put("ENV/~#1","preserved-neighbor");
    clone_support=0;force_collision=1;operation=0;
    assert(SettingsBootMain()==RETURN_OK && locks==0);
    expect("RAM/Settings-At-Boot/zz-collision.boot-new","suffix-value");
    expect("RAM/Settings-At-Boot/zz-collision","plain-value");
    assert(access("RAM/Settings-At-Boot/~#1",F_OK));
    operation=1;assert(SettingsBootMain()==RETURN_OK && locks==0);
    expect("ENV/zz-collision.boot-new","suffix-value");expect("ENV/zz-collision","plain-value");
    expect("ENV/~#1","preserved-neighbor");assert(access("ENV/~#2",F_OK));
    assert(DeleteFile("RAM:Settings-Boot-State"));
    fail_state_open=1;operation=0;assert(SettingsBootMain()==RETURN_ERROR && locks==0);
    assert(access("RAM/Settings-Boot-State",F_OK));fail_state_open=0;
    puts("settingsboot-host PASS files=6 large=300000 empty=restored missing=refused failed-write=preserved incomplete=refused clones=6 recapture=refused busy=refused readonly=copy oldDOS=refused staging-alias=preserved stage-neighbor=preserved marker-open-failure=refused locks=0");
    return 0;
}
