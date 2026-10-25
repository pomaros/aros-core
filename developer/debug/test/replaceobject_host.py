#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Run RAM's actual atomic publication helper against failure-injected objects."""
from pathlib import Path
import subprocess
import tempfile

core = Path(__file__).resolve().parents[3]
source = (core / 'rom/filesys/ram/commands.c').read_text(encoding='latin1')
start = source.index('static BOOL RenameObject(')
end = source.index('/****i* ram.handler/CmdRenameDisk', start)
actual = source[start:end]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef int32_t LONG;
typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define ST_FILE -3
#define ST_USERDIR 2
#define ERROR_OBJECT_EXISTS 203
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_DISK_WRITE_PROTECTED 214
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_OBJECT_IN_USE 202
#define ERROR_DELETE_PROTECTED 222
#define FIBF_DELETE 1
#define RAM_ACCESS_WRITE 1
#define RAM_ACCESS_DELETE 2
struct Node { int ln_Pri; char *ln_Name; };
struct List { int unused; };
struct Object { struct Node node; struct Object *parent; void *lock; int protection; struct List elements; };
struct Lock { int unused; };
struct Handler { int locked; };
static struct Object directory, old_file, new_file;
static int err, fail_alloc, fail_security, target_exists, deleted, allocations, removed;
static int IoErr(void) { return err; }
static void SetIoErr(int n) { err=n; }
static struct Object *GetHardObject(struct Handler *h,struct Lock *l,char *name,struct Object **parent) {
 (void)h;(void)l;
 if(parent) *parent=&directory;
 if(!strcmp(name,"temporary")) return &new_file;
 if(!strcmp(name,"target")&&target_exists) return &old_file;
 err=ERROR_OBJECT_NOT_FOUND;return NULL;
}
static int ramSecCheckProperty(struct Handler *h,struct Object *o) { (void)h;(void)o;return fail_security; }
static int ramSecCheckObject(struct Handler *h,struct Object *o,int op) { (void)h;(void)o;(void)op;return fail_security; }
static struct Object *GetRealObject(struct Object *o) { return o; }
static char *FilePart(char *s) { return s; }
static int SetName(struct Handler *h,struct Object *o,char *name) {
 (void)h;allocations++;if(fail_alloc){err=103;return 0;}o->node.ln_Name=name;return 1;
}
static void NotifyAll(struct Handler *h,struct Object *o,int n) { (void)h;(void)o;(void)n; }
static void UnmatchNotifyRequests(struct Handler *h,struct Object *o) { (void)h;(void)o; }
static void MatchNotifyRequests(struct Handler *h) { (void)h; }
static void AdjustExaminations(struct Handler *h,struct Object *o) { (void)h;(void)o; }
static void DeleteObject(struct Handler *h,struct Object *o) { (void)h;assert(o==&old_file);target_exists=0;deleted++; }
static void Remove(struct Node *o) { (void)o;removed++; }
static void AddTail(struct List *l,struct Node *o) { (void)l;(void)o; }
static void reset(void) {
 memset(&directory,0,sizeof(directory));memset(&old_file,0,sizeof(old_file));memset(&new_file,0,sizeof(new_file));
 old_file.node.ln_Pri=new_file.node.ln_Pri=ST_FILE;old_file.node.ln_Name="target";new_file.node.ln_Name="temporary";
 old_file.parent=new_file.parent=&directory;
 err=fail_alloc=fail_security=deleted=allocations=removed=0;target_exists=1;
}
'''
suffix = r'''
int main(void) {
 struct Handler h={0};
 reset();assert(!CmdRenameObject(&h,NULL,"temporary",NULL,"target"));assert(err==ERROR_OBJECT_EXISTS&&deleted==0&&allocations==0);
 reset();assert(CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(deleted==1&&allocations==1);assert(!strcmp(new_file.node.ln_Name,"target"));
 reset();target_exists=0;assert(CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(deleted==0);
 reset();fail_alloc=1;assert(!CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(err==103&&deleted==0&&target_exists);assert(!strcmp(new_file.node.ln_Name,"temporary"));
 reset();old_file.lock=&old_file;assert(!CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(err==ERROR_OBJECT_IN_USE&&deleted==0&&allocations==0);
 reset();old_file.protection=FIBF_DELETE;assert(!CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(err==ERROR_DELETE_PROTECTED&&deleted==0&&allocations==0);
 reset();old_file.node.ln_Pri=ST_USERDIR;assert(!CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(err==ERROR_OBJECT_WRONG_TYPE&&deleted==0);
 reset();new_file.node.ln_Pri=ST_USERDIR;assert(!CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(err==ERROR_OBJECT_WRONG_TYPE&&deleted==0);
 reset();h.locked=1;assert(!CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(err==ERROR_DISK_WRITE_PROTECTED&&deleted==0);h.locked=0;
 reset();fail_security=224;assert(!CmdReplaceObject(&h,NULL,"temporary",NULL,"target"));assert(err==224&&deleted==0&&allocations==0);
 reset();assert(CmdReplaceObject(&h,NULL,"target",NULL,"target"));assert(deleted==0);
 puts("replaceobject RAM production helper: PASS scenarios=11 deletion_windows=0 failure_target_loss=0");
}
'''
with tempfile.TemporaryDirectory(prefix='aros-replace-host-') as temporary:
    work = Path(temporary)
    (work/'test.c').write_text(prefix + actual + suffix)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)

store=(core/'workbench/libs/settings/settings_store.c').read_text()
start=store.index('LONG settings_image_write_atomic(')
end=store.index('\nBOOL settings_layer_path', start)
writer=store[start:end]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef const char *CONST_STRPTR;
typedef uintptr_t BPTR;
#define BNULL 0
#define SETTINGS_FULL_PATH 256
#define SETTING_ERROR_BUFFER 1
#define SETTING_ERROR_IO 2
#define SETTING_OK 0
#define MODE_NEWFILE 1006
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_DIR_NOT_FOUND 204
#define ERROR_OBJECT_EXISTS 203
struct SettingsBase { int sb_Lock; };
static int nonce_semaphore;
static void ObtainSemaphore(int *s) { (void)s;assert(!nonce_semaphore);nonce_semaphore++; }
static void ReleaseSemaphore(int *s) { (void)s;assert(nonce_semaphore==1);nonce_semaphore--; }
struct SettingsImage { char *si_Data; unsigned si_Length; };
static int error,fail_write,fail_flush,fail_close,fail_replace,fail_dir,fail_open,fail_dir_cleanup,foreign,replacements,locks,files,stage_exists;
static char target[8],temporary[8],neighbor[8],stage_name[256],final_name[256];
static LONG IoErr(void) { return error; }
static void SetIoErr(LONG value) { error=value; }
static LONG settings_code_from_dos(LONG value) { (void)value;return SETTING_ERROR_IO; }
static void make_parent_dirs(struct SettingsBase *base,const char *path) { (void)base;(void)path; }
static char *FilePart(const char *path) { const char *p=path,*leaf=path;for(;*p;p++)if(*p=='/'||*p==':')leaf=p+1;return (char*)leaf; }
static BPTR CreateDir(const char *path) {
 assert(!nonce_semaphore && strcmp(path,final_name));
 if(fail_dir){error=103;return 0;}if(foreign){foreign--;error=203;return 0;}
 assert(!stage_exists);strcpy(stage_name,path);stage_exists=1;locks++;return 2;
}
static void UnLock(BPTR lock) { assert(lock==2&&locks==1);locks--; }
static BPTR Open(const char *path,int mode) {
 assert(mode==MODE_NEWFILE);if(fail_open){error=103;return 0;}
 if(!strcmp(path,"prefs.new")){memset(neighbor,0,sizeof neighbor);files++;return 5;}
 char expected[256];snprintf(expected,sizeof expected,"%s/0",stage_name);assert(stage_exists&&locks==1&&!strcmp(path,expected));memset(temporary,0,sizeof temporary);files++;return 1;
}
static LONG Write(BPTR file,const void *bytes,unsigned n) { assert(files==1);if(fail_write){error=103;return -1;}memcpy(file==5?neighbor:temporary,bytes,n);return n; }
static int Flush(BPTR file) { (void)file;assert(files==1);if(fail_flush){error=103;return 0;}return 1; }
static int Close(BPTR file) { (void)file;assert(files==1);files--;if(fail_close){error=103;return 0;}return 1; }
static int DeleteFile(const char *path) {
 if(!strcmp(path,stage_name)){assert(locks==0);if(fail_dir_cleanup){error=222;return 0;}stage_exists=0;return 1;}
 if(!strcmp(path,"prefs.new")){neighbor[0]=0;return 1;}
 char expected[256];snprintf(expected,sizeof expected,"%s/0",stage_name);assert(!strcmp(path,expected));temporary[0]=0;return 1;
}
static int ReplaceObject(const char *from,const char *to) {
 assert(!strcmp(to,final_name));assert(!strcmp(target,"old"));replacements++;
 if(fail_replace){error=209;return 0;}
 if(!strcmp(from,"prefs.new")){memcpy(target,neighbor,sizeof target);neighbor[0]=0;}
 else{memcpy(target,temporary,sizeof target);temporary[0]=0;}
 return 1;
}
static void reset(const char *name) { error=fail_write=fail_flush=fail_close=fail_replace=fail_dir=fail_open=fail_dir_cleanup=foreign=replacements=locks=files=stage_exists=0;strcpy(target,"old");strcpy(neighbor,"keep");strcpy(final_name,name);stage_name[0]=temporary[0]=0; }
static void retained(void) { assert(!strcmp(target,"old")&&!strcmp(neighbor,"keep")&&locks==0&&files==0); }
'''
suffix=r'''
int main(int argc,char **argv) {
 (void)argv;struct SettingsBase base={0};struct SettingsImage image={"new",3};
 if(argc>1){reset("prefs");assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_OK);assert(!strcmp(neighbor,"keep"));return 0;}
 reset(".0.0000000001");assert(settings_image_write_atomic(&base,final_name,&image)==SETTING_OK);assert(!strcmp(target,"new")&&!strcmp(neighbor,"keep")&&locks==0&&!stage_exists);
 reset("prefs");assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_OK);assert(!strcmp(target,"new")&&!strcmp(neighbor,"keep")&&replacements==1&&locks==0&&!stage_exists);
 reset("prefs");fail_write=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_ERROR_IO);retained();assert(error==103&&!stage_exists);
 reset("prefs");fail_flush=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_ERROR_IO);retained();assert(error==103&&!stage_exists);
 reset("prefs");fail_close=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_ERROR_IO);retained();assert(error==103&&!stage_exists);
 reset("prefs");fail_replace=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_ERROR_IO);retained();assert(error==209&&!stage_exists);
 reset("prefs");fail_dir=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_ERROR_IO);retained();assert(error==103&&!stage_exists);
 reset("prefs");fail_open=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_ERROR_IO);retained();assert(error==103&&!stage_exists);
 reset("prefs");foreign=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_OK);assert(!strcmp(target,"new")&&!strcmp(neighbor,"keep")&&!stage_exists&&locks==0);
 reset("prefs");fail_dir_cleanup=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_OK);assert(!strcmp(target,"new")&&!strcmp(neighbor,"keep")&&stage_exists&&locks==0&&error==0);
 reset("prefs");fail_replace=1;fail_dir_cleanup=1;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_ERROR_IO);retained();assert(error==209&&stage_exists);
 reset("prefs");foreign=999;assert(settings_image_write_atomic(&base,"prefs",&image)==SETTING_ERROR_IO);retained();assert(error==203&&!stage_exists&&foreign==743);
 puts("settings production writer: PASS scenarios=12 neighbor_loss=0 final_alias=excluded OOM_cleanup=balanced publication_cleanup_failure=success");
}
'''
with tempfile.TemporaryDirectory(prefix='aros-settings-atomic-host-') as temporary:
    work=Path(temporary);(work/'test.c').write_text(prefix+writer+suffix)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)

# Optionally exercise the retained original production writer as a negative control.
import sys
if len(sys.argv)>1:
    original=Path(sys.argv[1]).read_text()
    with tempfile.TemporaryDirectory(prefix='aros-settings-neighbor-negative-') as temporary:
        work=Path(temporary);(work/'test.c').write_text(prefix+original+suffix)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
        result=subprocess.run([str(work/'test'),'negative'],capture_output=True,text=True)
        assert result.returncode != 0, 'original writer unexpectedly preserved prefs.new neighbor'
        print('settings neighbor negative control: PASS original production writer loses legitimate prefs.new')

init_source=(core/'workbench/libs/settings/settings_init.c').read_text()
start=init_source.index('static int Settings_Init(')
end=init_source.index('\nADD2INITLIB', start)
initializer=init_source[start:end]
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TRUE 1
#define FALSE 0
#define InitSemaphore(x) ((void)(x))
#define NEWLIST(x) ((void)(x))
struct Library { unsigned lib_Version,lib_Revision; } dos={50,84},utility={36,0};
struct SettingsBase { int sb_Lock,sb_Schemas,sb_Watches,sb_Subscriptions; struct Library *sb_DOSBase,*sb_UtilityBase; };
#define LIBBASETYPEPTR struct SettingsBase *
static int references,utility_fail;
static struct Library *OpenLibrary(const char *name,unsigned major) { struct Library *lib=!strcmp(name,"dos.library")?&dos:&utility; if(lib->lib_Version<major||(lib==&utility&&utility_fail))return NULL;references++;return lib; }
static void CloseLibrary(struct Library *lib) { assert(lib);assert(references>0);references--; }
'''
suffix=r'''
int main(void) {
 struct SettingsBase base={0};
 dos.lib_Revision=83;assert(!Settings_Init(&base));assert(references==0&&base.sb_DOSBase==NULL);
 dos.lib_Version=49;dos.lib_Revision=99;assert(!Settings_Init(&base));assert(references==0);
 dos.lib_Version=50;dos.lib_Revision=84;assert(Settings_Init(&base));assert(references==2);CloseLibrary(base.sb_UtilityBase);CloseLibrary(base.sb_DOSBase);
 base.sb_DOSBase=base.sb_UtilityBase=NULL;utility_fail=1;assert(!Settings_Init(&base));assert(references==0);utility_fail=0;
 dos.lib_Version=51;dos.lib_Revision=0;assert(Settings_Init(&base));CloseLibrary(base.sb_UtilityBase);CloseLibrary(base.sb_DOSBase);assert(references==0);
 puts("settings init revision admission: PASS DOS49/50.83refused 50.84/51accepted library_refs=0");
}
'''
with tempfile.TemporaryDirectory(prefix='aros-settings-dos-revision-host-') as temporary:
    work=Path(temporary);(work/'test.c').write_text(prefix+initializer+suffix)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)

# Remove only the revision admission check: the old DOS test must detect it.
start=initializer.index('    if (LIBBASE->sb_DOSBase->lib_Version == 50')
end=initializer.index('    LIBBASE->sb_UtilityBase = OpenLibrary',start)
unguarded=initializer[:start]+initializer[end:]
with tempfile.TemporaryDirectory(prefix='aros-settings-dos-revision-negative-') as temporary:
    work=Path(temporary);(work/'test.c').write_text(prefix+unguarded+suffix)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
    result=subprocess.run([str(work/'test')],capture_output=True,text=True)
    assert result.returncode != 0, 'old DOS admission control did not reject a removed guard'
    print('settings DOS revision negative control: PASS removed guard rejected')
