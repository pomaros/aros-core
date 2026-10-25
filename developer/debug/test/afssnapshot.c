/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Disposable VM semantic probe; never selects a native/internal device.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "afsplus_client.h"
#include "afsprobe_device.h"
#include "afsprobe_report.h"

#define SOURCE "AFSPROBE:captured.bin"
#define RESTORED "AFSPROBE:restored.bin"
#define BLOCKED "AFSPROBE:blocked.bin"
#define TREE "AFSPROBE:saved-dir"
#define TREE_FILE "AFSPROBE:saved-dir/prefs.bin"
#define TREE_EMPTY "AFSPROBE:saved-dir/Empty"
#define TREE_RESTORED "AFSPROBE:restored-dir"
#define TREE_RESTORED_FILE "AFSPROBE:restored-dir/prefs.bin"
#define TREE_RESTORED_EMPTY "AFSPROBE:restored-dir/Empty"
#define PROTECT_KEY 0x41534650
static const unsigned char captured[] = "captured\0snapshot bytes";
static const unsigned char changed[] = "different live bytes";
static const char *stage = "arguments";
static LONG failure;

static void report(const char *format, ...)
{
    char line[512]; va_list args; int n;
    va_start(args,format); n=vsnprintf(line,sizeof line,format,args); va_end(args);
    if(n<0 || (size_t)n>=sizeof line) return;
    afsprobe_debug_record(line,(size_t)n);
    fwrite(line,1,(size_t)n,stdout); fflush(stdout);
}
#define REQUIRE(condition) do { if(!(condition)) { failure=IoErr(); goto done; } } while(0)
#define EXT(call) do { LONG e=(call); if(e) { failure=e; goto done; } } while(0)
static int write_file(const char *path, const void *bytes, size_t n)
{
    BPTR file=Open((CONST_STRPTR)path,MODE_NEWFILE); int ok;
    if(!file)return 0;
    ok=Write(file,bytes,(LONG)n)==(LONG)n && Flush(file);
    if(!Close(file))ok=0;
    return ok;
}
static int no_file(const char *path)
{
    BPTR lock=Lock((CONST_STRPTR)path,SHARED_LOCK);
    if(lock) {UnLock(lock);return 0;}
    return IoErr()==ERROR_OBJECT_NOT_FOUND;
}
static int bytes_are(const char *path,const void *expected,size_t length)
{
    unsigned char buffer[128];
    BPTR file=Open((CONST_STRPTR)path,MODE_OLDFILE);
    int okay;
    if(!file || length>sizeof buffer)return 0;
    okay=Read(file,buffer,sizeof buffer)==(LONG)length && !memcmp(buffer,expected,length)
        && Read(file,buffer,1)==0;
    if(!Close(file))okay=0;
    return okay;
}
static int assigned_paths_probe(void)
{
    static const char shadow[]="root shadow remains";
    BPTR lock=BNULL;
    struct FileInfoBlock fib;
    struct DateStamp stamp={12345,321,17};
    int assigned=0,okay=0;
    REQUIRE(no_file("AFSPROBE:assign-parent"));
    REQUIRE(no_file("AFSPROBE:assign-sys"));
    REQUIRE(no_file("AFSPROBE:assign-data"));
    stage="assign-root-shadows";
    REQUIRE(write_file("AFSPROBE:assign-sys",shadow,sizeof shadow));
    REQUIRE(write_file("AFSPROBE:assign-data",shadow,sizeof shadow));
    lock=CreateDir((CONST_STRPTR)"AFSPROBE:assign-parent");REQUIRE(lock!=BNULL);
    REQUIRE(AssignLock((CONST_STRPTR)"AFSPATH",lock));lock=BNULL;assigned=1;
    stage="assign-nested-create";
    lock=CreateDir((CONST_STRPTR)"AFSPATH:assign-sys");REQUIRE(lock!=BNULL);
    REQUIRE(Examine(lock,&fib) && fib.fib_DirEntryType>0);UnLock(lock);lock=BNULL;
    REQUIRE(write_file("AFSPATH:assign-sys/font",captured,sizeof captured-1));
    REQUIRE(bytes_are("AFSPROBE:assign-parent/assign-sys/font",captured,sizeof captured-1));
    stage="assign-metadata";
    REQUIRE(write_file("AFSPATH:assign-data",captured,sizeof captured-1));
    REQUIRE(SetComment((CONST_STRPTR)"AFSPATH:assign-data",(CONST_STRPTR)"assigned-comment"));
    REQUIRE(SetOwner((CONST_STRPTR)"AFSPATH:assign-data",((ULONG)42<<16)|17));
    REQUIRE(SetProtection((CONST_STRPTR)"AFSPATH:assign-data",0x50));
    REQUIRE(SetFileDate((CONST_STRPTR)"AFSPATH:assign-data",&stamp));
    lock=Lock((CONST_STRPTR)"AFSPATH:assign-data",SHARED_LOCK);REQUIRE(lock!=BNULL);
    REQUIRE(Examine(lock,&fib) && !strcmp((const char *)fib.fib_Comment,"assigned-comment")
        && fib.fib_OwnerUID==42 && fib.fib_OwnerGID==17 && fib.fib_Protection==0x50);
    UnLock(lock);lock=BNULL;
    stage="assign-rename";
    REQUIRE(Rename((CONST_STRPTR)"AFSPATH:assign-data",(CONST_STRPTR)"AFSPATH:assign-renamed"));
    REQUIRE(no_file("AFSPROBE:assign-parent/assign-data"));
    REQUIRE(bytes_are("AFSPATH:assign-renamed",captured,sizeof captured-1));
    stage="assign-replace";
    REQUIRE(write_file("AFSPATH:assign-stage",changed,sizeof changed-1));
    REQUIRE(ReplaceObject((CONST_STRPTR)"AFSPATH:assign-stage",(CONST_STRPTR)"AFSPATH:assign-renamed"));
    REQUIRE(no_file("AFSPATH:assign-stage"));
    REQUIRE(bytes_are("AFSPATH:assign-renamed",changed,sizeof changed-1));
    stage="assign-delete";
    REQUIRE(DeleteFile((CONST_STRPTR)"AFSPATH:assign-renamed"));
    REQUIRE(no_file("AFSPROBE:assign-parent/assign-renamed"));
    REQUIRE(DeleteFile((CONST_STRPTR)"AFSPATH:assign-sys/font"));
    REQUIRE(DeleteFile((CONST_STRPTR)"AFSPATH:assign-sys"));
    stage="assign-shadow-preservation";
    REQUIRE(bytes_are("AFSPROBE:assign-sys",shadow,sizeof shadow));
    REQUIRE(bytes_are("AFSPROBE:assign-data",shadow,sizeof shadow));
    lock=Lock((CONST_STRPTR)"AFSPROBE:assign-data",SHARED_LOCK);REQUIRE(lock!=BNULL);
    REQUIRE(Examine(lock,&fib) && fib.fib_Comment[0]==0 && fib.fib_OwnerUID==0 && fib.fib_OwnerGID==0);
    UnLock(lock);lock=BNULL;
    REQUIRE(AssignLock((CONST_STRPTR)"AFSPATH",BNULL));assigned=0;
    REQUIRE(DeleteFile((CONST_STRPTR)"AFSPROBE:assign-parent"));
    REQUIRE(DeleteFile((CONST_STRPTR)"AFSPROBE:assign-sys"));
    REQUIRE(DeleteFile((CONST_STRPTR)"AFSPROBE:assign-data"));
    okay=1;
done:
    if(lock)UnLock(lock);
    if(assigned)AssignLock((CONST_STRPTR)"AFSPATH",BNULL);
    if(okay)report("[AFSSNAPSHOT] assign-relative=PASS operations=create,open,metadata,rename,replace,delete root-shadow=preserved\n");
    return okay;
}
static int denied_probe(void)
{
    struct DevProc *device=GetDeviceProc((CONST_STRPTR)"AFSDENY:",NULL);
    struct AfsplusArosSnapshotInfo entries[8];
    BPTR root=BNULL;uint64_t id=0,next=0;uint32_t count=0;int ok=0;
    if(!afsprobe_bound_device(device,92,(CONST_STRPTR)"AFSDENY"))goto done;
    if(afsplus_client_snapshot_create(device->dvp_Port,&id)!=ERROR_READ_PROTECTED || id!=0)goto done;
    if(afsplus_client_snapshot_list(device->dvp_Port,0,entries,8,&count,&next)!=ERROR_READ_PROTECTED || count || next)goto done;
    if(afsplus_client_snapshot_open(device->dvp_Port,1,&id)!=ERROR_READ_PROTECTED || id!=0)goto done;
    if(afsplus_client_snapshot_delete(device->dvp_Port,1)!=ERROR_READ_PROTECTED)goto done;
    root=Lock((CONST_STRPTR)"AFSDENY:",SHARED_LOCK);if(!root)goto done;
    if(afsplus_client_snapshot_restore_file(device->dvp_Port,1,1,root,(CONST_STRPTR)"denied.bin",&id)!=ERROR_READ_PROTECTED || id!=0)goto done;
    if(!no_file("AFSDENY:denied.bin"))goto done;
    ok=1;
done:
    if(root)UnLock(root);if(device)FreeDeviceProc(device);
    return ok;
}
static LONG live_stat(struct MsgPort *port,BPTR parent,const char *name,struct AfsplusArosStat *stat)
{
    struct AfsplusExtRequest request={0}; LONG error;
    request.operation=AFSPLUS_EXT_LOOKUP_ID;
    request.object[0]=(uint64_t)(uintptr_t)parent;
    request.name0=(const uint8_t *)name;request.name_length[0]=(uint32_t)strlen(name);
    error=afsplus_client_send(port,&request);if(error)return error;
    request.offset[0]=request.output_value;request.object[0]=0;
    request.name0=NULL;request.name_length[0]=0;
    request.operation=AFSPLUS_EXT_STAT_ID;memset(stat,0,sizeof(*stat));stat->struct_size=sizeof(*stat);
    request.buffer=stat;request.buffer_size=sizeof(*stat);
    return afsplus_client_send(port,&request);
}
static int same_metadata(const struct AfsplusArosStat *a,const struct AfsplusArosStat *b)
{
    return a->kind==b->kind && a->size==b->size && a->allocated_size==b->allocated_size
        && a->protection==b->protection && a->owner_uid==b->owner_uid && a->owner_gid==b->owner_gid
        && a->created_seconds==b->created_seconds && a->created_nanoseconds==b->created_nanoseconds
        && a->modified_seconds==b->modified_seconds && a->modified_nanoseconds==b->modified_nanoseconds
        && a->changed_seconds==b->changed_seconds && a->changed_nanoseconds==b->changed_nanoseconds;
}
int main(int argc,char **argv)
{
    struct DevProc *device=NULL;struct MsgPort *port=NULL;
    BPTR root=BNULL,file=BNULL,lock=BNULL;
    struct AfsplusArosStat before,old,restored,tree_before,tree_after;
    struct AfsplusArosSnapshotInfo entries[8];
    struct FileInfoBlock fib;
    struct DateStamp stamp={12345,321,17};
    uint64_t snapshot=0,reader=0,directory=0,source=0,new_object=0,next,tree_source=0;
    unsigned char bytes[4096];uint32_t count,eof,i,at;
    int ok=0,protected=0,found=0;LONG e,result;
    if((argc!=2 && argc!=3) || strcmp(argv[1],"DISPOSABLEVM")
       || (argc==3 && strcmp(argv[2],"DENIED"))) {
        report("[AFSSNAPSHOT] result=FAIL stage=arguments reason=disposable-vm-required\n");return RETURN_FAIL;
    }
    stage="resolve-device";device=GetDeviceProc((CONST_STRPTR)"AFSPROBE:",NULL);REQUIRE(device!=NULL);
    /* A direct device has no assign lock. Retain only its live port during
     * the active-reader ACTION_DIE test, so an ordinary lock cannot mask it. */
    stage="device-identity";REQUIRE(afsprobe_bound_device(device,91,(CONST_STRPTR)"AFSPROBE"));port=device->dvp_Port;FreeDeviceProc(device);device=NULL;
    if(argc==3) {stage="denied-volume";REQUIRE(denied_probe());report("[AFSSNAPSHOT] authority-denial=PASS volume=AFSDENY unit=92\n");}
    stage="assigned-paths";if(!assigned_paths_probe())goto done;
    stage="fresh-fixtures";REQUIRE(no_file(SOURCE));REQUIRE(no_file(RESTORED));REQUIRE(no_file(BLOCKED));REQUIRE(no_file(TREE));REQUIRE(no_file(TREE_RESTORED));
    stage="source-write";REQUIRE(write_file(SOURCE,captured,sizeof(captured)-1));
    stage="source-comment";REQUIRE(SetComment((CONST_STRPTR)SOURCE,(CONST_STRPTR)"captured-comment"));
    stage="source-owner";REQUIRE(SetOwner((CONST_STRPTR)SOURCE,((ULONG)42<<16)|17));
    stage="source-date";REQUIRE(SetFileDate((CONST_STRPTR)SOURCE,&stamp));
    stage="source-protection";REQUIRE(SetProtection((CONST_STRPTR)SOURCE,0x50));
    stage="tree-fixture";lock=CreateDir((CONST_STRPTR)TREE);REQUIRE(lock!=BNULL);UnLock(lock);lock=BNULL;
    lock=CreateDir((CONST_STRPTR)TREE_EMPTY);REQUIRE(lock!=BNULL);UnLock(lock);lock=BNULL;
    REQUIRE(write_file(TREE_FILE,captured,sizeof(captured)-1));
    REQUIRE(SetComment((CONST_STRPTR)TREE_FILE,(CONST_STRPTR)"tree-file-comment"));
    REQUIRE(SetComment((CONST_STRPTR)TREE,(CONST_STRPTR)"captured-dir-comment"));
    REQUIRE(SetOwner((CONST_STRPTR)TREE,((ULONG)43<<16)|18));
    REQUIRE(SetFileDate((CONST_STRPTR)TREE,&stamp));
    root=Lock((CONST_STRPTR)"AFSPROBE:",SHARED_LOCK);REQUIRE(root!=BNULL);
    stage="tree-source-stat";EXT(live_stat(port,root,"saved-dir",&tree_before));
    REQUIRE(tree_before.kind==AFSPLUS_AROS_KIND_DIRECTORY && tree_before.owner_uid==43 && tree_before.owner_gid==18);
    stage="source-stat";EXT(live_stat(port,root,"captured.bin",&before));
    REQUIRE(before.kind==AFSPLUS_AROS_KIND_FILE && before.size==sizeof(captured)-1);
    REQUIRE(before.owner_uid==42 && before.owner_gid==17 && before.protection==0x50);
    UnLock(root);root=BNULL;
    stage="snapshot-create";EXT(afsplus_client_snapshot_create(port,&snapshot));REQUIRE(snapshot!=0);
    stage="snapshot-list";EXT(afsplus_client_snapshot_list(port,0,entries,8,&count,&next));
    REQUIRE(count==1 && entries[0].id==snapshot && next==0);
    stage="change-source";REQUIRE(write_file(SOURCE,changed,sizeof(changed)-1));
    stage="remove-source";REQUIRE(DeleteFile((CONST_STRPTR)SOURCE));REQUIRE(no_file(SOURCE));
    stage="remove-tree-source";REQUIRE(write_file(TREE_FILE,changed,sizeof(changed)-1));
    REQUIRE(DeleteFile((CONST_STRPTR)TREE_FILE));REQUIRE(DeleteFile((CONST_STRPTR)TREE_EMPTY));REQUIRE(DeleteFile((CONST_STRPTR)TREE));REQUIRE(no_file(TREE));
    stage="snapshot-open";EXT(afsplus_client_snapshot_open(port,snapshot,&reader));REQUIRE(reader!=0);
    stage="snapshot-lookup";EXT(afsplus_client_snapshot_lookup(port,reader,1,(CONST_STRPTR)"captured.bin",&source));
    stage="snapshot-stat";EXT(afsplus_client_snapshot_stat(port,reader,source,&old));REQUIRE(same_metadata(&old,&before));
    stage="snapshot-tree-stat";EXT(afsplus_client_snapshot_lookup(port,reader,1,(CONST_STRPTR)"saved-dir",&tree_source));
    EXT(afsplus_client_snapshot_stat(port,reader,tree_source,&tree_after));REQUIRE(same_metadata(&tree_after,&tree_before));
    stage="snapshot-read";EXT(afsplus_client_snapshot_read(port,reader,source,0,bytes,sizeof bytes,&count));
    REQUIRE(count==sizeof(captured)-1 && !memcmp(bytes,captured,count));
    EXT(afsplus_client_snapshot_read(port,reader,source,count,bytes,sizeof bytes,&count));REQUIRE(count==0);
    stage="snapshot-walk";EXT(afsplus_client_snapshot_dir_open(port,reader,1,&directory));
    eof=0;
    while(!eof) {
        EXT(afsplus_client_snapshot_dir_read(port,directory,bytes,sizeof bytes,8,&count,&eof));REQUIRE(count<=8);
        at=0;
        for(i=0;i<count;i++) {
            struct AfsplusArosDirEntry entry;
            REQUIRE(at<=sizeof bytes-sizeof entry);memcpy(&entry,bytes+at,sizeof entry);
            REQUIRE(entry.record_length>=sizeof entry && entry.record_length<=sizeof bytes-at);
            REQUIRE(entry.name_length<=entry.record_length-sizeof entry);
            if(entry.name_length==12 && !memcmp(bytes+at+sizeof entry,"captured.bin",12)) {
                REQUIRE(entry.object_id==source && entry.kind==AFSPLUS_AROS_KIND_FILE);found++;
            }
            at+=entry.record_length;
        }
        REQUIRE(count!=0 || eof);
    }
    REQUIRE(found==1);EXT(afsplus_client_snapshot_dir_close(port,directory));directory=0;
    stage="busy-delete";e=afsplus_client_snapshot_delete(port,snapshot);REQUIRE(e==ERROR_OBJECT_IN_USE);
    stage="busy-handler-die";result=DoPkt(port,ACTION_DIE,0,0,0,0,0);e=IoErr();
    if(result!=DOSFALSE) {
        report("[AFSSNAPSHOT] result=FAIL stage=busy-handler-die reason=handler-stopped-with-active-reader\n");return RETURN_FAIL;
    }
    REQUIRE(e==ERROR_OBJECT_IN_USE);
    root=Lock((CONST_STRPTR)"AFSPROBE:",SHARED_LOCK);REQUIRE(root!=BNULL);
    stage="write-protect";REQUIRE(DoPkt(port,ACTION_WRITE_PROTECT,DOSTRUE,PROTECT_KEY,0,0,0)==DOSTRUE);protected=1;
    stage="protected-create";e=afsplus_client_snapshot_create(port,&new_object);REQUIRE(e==ERROR_DISK_WRITE_PROTECTED && new_object==0);
    stage="protected-delete";e=afsplus_client_snapshot_delete(port,snapshot);REQUIRE(e==ERROR_DISK_WRITE_PROTECTED);
    stage="protected-restore";e=afsplus_client_snapshot_restore_file(port,reader,source,root,(CONST_STRPTR)"blocked.bin",&new_object);
    REQUIRE(e==ERROR_DISK_WRITE_PROTECTED && new_object==0);REQUIRE(no_file(BLOCKED));
    stage="protected-restore-tree";e=afsplus_client_snapshot_restore_tree(port,reader,tree_source,root,(CONST_STRPTR)"blocked-dir",4096,64,4194304,&new_object);
    REQUIRE(e==ERROR_DISK_WRITE_PROTECTED && new_object==0);REQUIRE(no_file("AFSPROBE:blocked-dir"));
    stage="unprotect";REQUIRE(DoPkt(port,ACTION_WRITE_PROTECT,DOSFALSE,PROTECT_KEY,0,0,0)==DOSTRUE);protected=0;
    stage="restore";EXT(afsplus_client_snapshot_restore_file(port,reader,source,root,(CONST_STRPTR)"restored.bin",&new_object));REQUIRE(new_object!=source && new_object!=0);
    stage="restored-stat";EXT(live_stat(port,root,"restored.bin",&restored));REQUIRE(same_metadata(&restored,&before));
    UnLock(root);root=BNULL;
    stage="restored-examine";lock=Lock((CONST_STRPTR)RESTORED,SHARED_LOCK);REQUIRE(lock!=BNULL);memset(&fib,0,sizeof fib);
    REQUIRE(Examine(lock,&fib));REQUIRE(!strcmp((const char *)fib.fib_Comment,"captured-comment"));
    REQUIRE(fib.fib_OwnerUID==42 && fib.fib_OwnerGID==17 && fib.fib_Protection==0x50);
    UnLock(lock);lock=BNULL;
    stage="restored-bytes";file=Open((CONST_STRPTR)RESTORED,MODE_OLDFILE);REQUIRE(file!=BNULL);
    REQUIRE(Read(file,bytes,sizeof bytes)==(LONG)(sizeof(captured)-1));REQUIRE(!memcmp(bytes,captured,sizeof(captured)-1));
    REQUIRE(Read(file,bytes,sizeof bytes)==0);REQUIRE(Close(file));file=BNULL;
    stage="existing-target-refusal";root=Lock((CONST_STRPTR)"AFSPROBE:",SHARED_LOCK);REQUIRE(root!=BNULL);
    e=afsplus_client_snapshot_restore_file(port,reader,source,root,(CONST_STRPTR)"RESTORED.BIN",&new_object);
    REQUIRE(e==ERROR_OBJECT_EXISTS && new_object==0);
    stage="tree-budget-refusal";e=afsplus_client_snapshot_restore_tree(port,reader,tree_source,root,(CONST_STRPTR)"budget-dir",1,64,4194304,&new_object);
    REQUIRE(e==ERROR_OBJECT_TOO_LARGE && new_object==0);REQUIRE(no_file("AFSPROBE:budget-dir"));
    stage="restore-tree";EXT(afsplus_client_snapshot_restore_tree(port,reader,tree_source,root,(CONST_STRPTR)"restored-dir",4096,64,4194304,&new_object));
    stage="restored-tree-stat";EXT(live_stat(port,root,"restored-dir",&tree_after));REQUIRE(same_metadata(&tree_after,&tree_before));
    UnLock(root);root=BNULL;
    stage="restored-tree-comment";lock=Lock((CONST_STRPTR)TREE_RESTORED,SHARED_LOCK);REQUIRE(lock!=BNULL);
    REQUIRE(Examine(lock,&fib));REQUIRE(!strcmp((const char *)fib.fib_Comment,"captured-dir-comment"));UnLock(lock);lock=BNULL;
    stage="restored-tree-empty";lock=Lock((CONST_STRPTR)TREE_RESTORED_EMPTY,SHARED_LOCK);REQUIRE(lock!=BNULL);UnLock(lock);lock=BNULL;
    stage="restored-tree-bytes";file=Open((CONST_STRPTR)TREE_RESTORED_FILE,MODE_OLDFILE);REQUIRE(file!=BNULL);
    REQUIRE(Read(file,bytes,sizeof bytes)==(LONG)(sizeof(captured)-1));REQUIRE(!memcmp(bytes,captured,sizeof(captured)-1));REQUIRE(Read(file,bytes,sizeof bytes)==0);REQUIRE(Close(file));file=BNULL;
    stage="restored-tree-file-comment";lock=Lock((CONST_STRPTR)TREE_RESTORED_FILE,SHARED_LOCK);REQUIRE(lock!=BNULL);
    REQUIRE(Examine(lock,&fib));REQUIRE(!strcmp((const char *)fib.fib_Comment,"tree-file-comment"));UnLock(lock);lock=BNULL;
    stage="delete-restored-tree";REQUIRE(DeleteFile((CONST_STRPTR)TREE_RESTORED_FILE));REQUIRE(DeleteFile((CONST_STRPTR)TREE_RESTORED_EMPTY));REQUIRE(DeleteFile((CONST_STRPTR)TREE_RESTORED));REQUIRE(no_file(TREE_RESTORED));
    stage="snapshot-close";EXT(afsplus_client_snapshot_close(port,reader));reader=0;
    stage="snapshot-delete";EXT(afsplus_client_snapshot_delete(port,snapshot));snapshot=0;
    stage="empty-snapshot-list";EXT(afsplus_client_snapshot_list(port,0,entries,8,&count,&next));REQUIRE(count==0 && next==0);
    stage="delete-restored";REQUIRE(DeleteFile((CONST_STRPTR)RESTORED));REQUIRE(no_file(RESTORED));
    ok=1;
done:
    if(protected && port)DoPkt(port,ACTION_WRITE_PROTECT,DOSFALSE,PROTECT_KEY,0,0,0);
    if(file)Close(file);if(lock)UnLock(lock);if(root)UnLock(root);
    if(directory)afsplus_client_snapshot_dir_close(port,directory);
    if(reader)afsplus_client_snapshot_close(port,reader);
    if(snapshot)afsplus_client_snapshot_delete(port,snapshot);
    if(device)FreeDeviceProc(device);
    if(ok)report("[AFSSNAPSHOT] result=PASS checks=historical-bytes,metadata,walk,busy,write-protect,fresh-file-tree-restore,tree-budget,cleanup\n");
    else report("[AFSSNAPSHOT] result=FAIL stage=%s error=%ld\n",stage,(long)failure);
    return ok?RETURN_OK:RETURN_FAIL;
}
