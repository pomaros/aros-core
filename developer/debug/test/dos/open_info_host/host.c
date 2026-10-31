/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int32_t LONG;typedef uint32_t ULONG;typedef intptr_t SIPTR;
typedef uintptr_t BPTR;typedef unsigned char UBYTE;typedef const char *CONST_STRPTR;typedef char *BSTR;
#define MODE_READWRITE 1004
#define MODE_OLDFILE 1005
#define MODE_NEWFILE 1006
#define ACTION_FINDINPUT 1005
#define ACTION_FINDOUTPUT 1006
#define ACTION_FINDOUTPUT_INFO 1038
#define ACTION_END 1007
#define OPENINFO_CREATED 1
#define OPENINFO_TRUNCATED 2
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_OBJECT_IN_USE 202
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_WRITE_PROTECTED 223
#define ERROR_NO_FREE_STORE 103
#define ERROR_BAD_NUMBER 115
#define ERROR_NOT_IMPLEMENTED 236
#define ERROR_ACTION_NOT_KNOWN 209
#define SHARED_LOCK -2
#define EXCLUSIVE_LOCK -1
#define ATTR_DIRECTORY 16
#define ATTR_ARCHIVE 32
#define ATTR_READ_ONLY 1
#define TRUE 1
#define FALSE 0
#define D(x)
#define RESET_HANDLE(x) ((void)(x))
#define MKBADDR(x) ((BPTR)(x))
struct MsgPort {int id;};struct DosLibrary {int id;};
struct FileHandle {struct MsgPort *fh_Type;BPTR fh_Arg1;};
static struct MsgPort target,override;
static LONG answer,packet_error,expected_action;static unsigned sends,ends;static int no_memory,change_port;
static BSTR C2BSTR(CONST_STRPTR name){return no_memory?NULL:(char *)name;}
#define FREEC2BSTR(x) ((void)(x))
static LONG dopacket3(struct DosLibrary *d,SIPTR *error,struct MsgPort *port,ULONG action,BPTR fh,BPTR lock,BSTR name)
{assert(port==&target && action==(ULONG)expected_action && lock==42 && !strcmp(name,"file"));++sends;*error=packet_error;struct FileHandle *h=(void *)fh;assert(!h->fh_Type);h->fh_Arg1=73;if(change_port)h->fh_Type=&override;return answer;}
static LONG dopacket1(struct DosLibrary *d,void *e,struct MsgPort *port,ULONG action,BPTR h)
{assert(port==(change_port?&override:&target) && action==ACTION_END && h==73);++ends;return -1;}
struct Super {ULONG rootdir_cluster;};
struct IOHandle {struct Super *sb;ULONG first_cluster;};
struct GlobalLock {ULONG attr,dir_cluster,dir_entry,first_cluster,size;};
struct ExtFileLock {struct IOHandle ioh;struct GlobalLock *gl;int do_notify;};
struct DirHandle {struct IOHandle ioh;};
struct DirEntry {struct {struct {ULONG first_cluster_lo,first_cluster_hi,file_size,attr;} entry;} e;ULONG cluster,index;};
struct Globals {struct Super *sb;};
static struct Super super;static struct GlobalLock gl;static struct ExtFileLock backing;
static LONG lookup_error,storage_error;static unsigned mutations,frees;static LONG lockmode;
static LONG CopyLock(struct ExtFileLock *l,struct ExtFileLock **out,struct Globals *g){*out=l;return 0;}
static LONG LockFileByName(struct ExtFileLock *p,UBYTE *n,ULONG len,LONG mode,struct ExtFileLock **out,struct Globals *g)
{lockmode=mode;if(lookup_error)return lookup_error;*out=&backing;return 0;}
static void FreeLock(struct ExtFileLock *l,struct Globals *g){assert(l==&backing);++frees;}
static void InitDirHandle(struct Super *s,ULONG c,struct DirHandle *d,int flag,struct Globals *g){d->ioh.sb=s;d->ioh.first_cluster=c;}
static void ReleaseDirHandle(struct DirHandle *d,struct Globals *g){}
static LONG GetDirEntry(struct DirHandle *d,ULONG i,struct DirEntry *e,struct Globals *g){memset(e,0,sizeof(*e));return storage_error;}
static LONG UpdateDirEntry(struct DirEntry *e,struct Globals *g){assert(!e->e.entry.file_size && !e->e.entry.first_cluster_lo && !e->e.entry.first_cluster_hi);++mutations;return 0;}
static LONG FreeClusterChain(struct Super *s,ULONG c){return 0;}
static LONG MoveToSubdir(struct DirHandle *d,UBYTE **n,ULONG *len,struct Globals *g){return storage_error;}
static LONG CreateDirEntry(struct DirHandle *d,UBYTE *n,ULONG len,ULONG attr,int z,struct DirEntry *e,struct Globals *g){memset(e,0,sizeof(*e));++mutations;return 0;}
static LONG LockFile(ULONG c,ULONG i,LONG mode,struct ExtFileLock **l,struct Globals *g){assert(mode==EXCLUSIVE_LOCK);*l=&backing;return 0;}
#include "actual.c"
int main(void)
{
 struct FileHandle fh={0};ULONG info=99;expected_action=ACTION_FINDOUTPUT_INFO;
 for(answer=1;answer<=2;++answer){assert(!fs_OpenWithInfo(&fh,&target,42,"file",&info,NULL));assert(info==(ULONG)answer && fh.fh_Type==&target);}
 answer=0;packet_error=ERROR_OBJECT_IN_USE;info=99;assert(fs_OpenWithInfo(&fh,&target,42,"file",&info,NULL)==packet_error && info==99);
 packet_error=ERROR_ACTION_NOT_KNOWN;assert(fs_OpenWithInfo(&fh,&target,42,"file",&info,NULL)==packet_error && info==99);
 answer=-1;change_port=1;assert(fs_OpenWithInfo(&fh,&target,42,"file",&info,NULL)==ERROR_BAD_NUMBER && ends==1 && info==99);
 unsigned before=sends;no_memory=1;assert(fs_OpenWithInfo(&fh,&target,42,"file",&info,NULL)==ERROR_NO_FREE_STORE && sends==before);no_memory=0;
 expected_action=MODE_NEWFILE;assert(!fs_Open(&fh,&target,42,MODE_NEWFILE,"file",NULL) && ends==1 && fh.fh_Type==&override);
 struct Globals globals={&super};struct ExtFileLock *out=NULL;backing.gl=&gl;backing.ioh.sb=&super;gl.size=17;
 lookup_error=ERROR_OBJECT_NOT_FOUND;assert(!OpOpenFile(NULL,(UBYTE *)"file",4,ACTION_FINDOUTPUT,&out,&info,&globals));assert(info==OPENINFO_CREATED && out==&backing && lockmode==EXCLUSIVE_LOCK && mutations==1);
 lookup_error=0;assert(!OpOpenFile(NULL,(UBYTE *)"file",4,ACTION_FINDOUTPUT,&out,&info,&globals));assert(info==OPENINFO_TRUNCATED && !gl.size && mutations==2);
 lookup_error=ERROR_OBJECT_IN_USE;info=99;assert(OpOpenFile(NULL,(UBYTE *)"file",4,ACTION_FINDOUTPUT,&out,&info,&globals)==lookup_error && !info && mutations==2);
 lookup_error=0;gl.attr=ATTR_DIRECTORY;assert(OpOpenFile(NULL,(UBYTE *)"file",4,ACTION_FINDOUTPUT,&out,&info,&globals)==ERROR_OBJECT_WRONG_TYPE && !info && mutations==2);
 gl.attr=ATTR_READ_ONLY;assert(OpOpenFile(NULL,(UBYTE *)"file",4,ACTION_FINDOUTPUT,&out,&info,&globals)==ERROR_WRITE_PROTECTED && !info && mutations==2);
 gl.attr=0;storage_error=77;assert(OpOpenFile(NULL,(UBYTE *)"file",4,ACTION_FINDOUTPUT,&out,&info,&globals)==77 && !info && mutations==2 && frees==3);
 lookup_error=ERROR_OBJECT_NOT_FOUND;assert(OpOpenFile(NULL,(UBYTE *)"file",4,ACTION_FINDOUTPUT,&out,&info,&globals)==77 && !info && mutations==2);
 puts("PASS actual DOS outcome transport and FAT create/truncate/error branches; storage/IPC mocked, no native execution");
}
