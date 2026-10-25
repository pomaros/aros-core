/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1.
 * Disposable VM command-level backup/snapshot/restore qualification.
 * Uses the real CLI programs, never direct extended-packet calls.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <aros/debug.h>
#include "afscommands_parse.h"
#include "afsprobe_device.h"
#include "afsprobe_report.h"

#define ROOT "AFSPROBE:AFSCommands"
#define OUTPUT "RAM:AFSCommands-output"
static unsigned failures;
static char text[65536];
static BOOL clone_shadow_owned, clone_assign_owned;

static void report(const char *format, ...)
{
    char payload[384], line[512];
    int length;
    va_list args;
    va_start(args, format);
    length=vsnprintf(payload, sizeof(payload), format, args);
    va_end(args);
    if(length<0 || (size_t)length>=sizeof(payload)) {afsprobe_debug_record(NULL,513);failures++;return;}
    length=snprintf(line,sizeof(line),"[AFSCOMMANDS] %s\n",payload);
    if(length<0 || (size_t)length>=sizeof(line)) {afsprobe_debug_record(NULL,513);failures++;return;}
    afsprobe_debug_record(line,(size_t)length);
    printf("%s",line);
}
static void check(BOOL ok, const char *step)
{ report("step=%s observed=%s",step,ok ? "accepted" : "FAIL"); if(!ok) failures++; }

/* Capture both stdout and stderr so expected CLI refusals are evidence
 * read by this probe, rather than unrelated fault text in the serial log. */
static LONG command(const char *args)
{
    BPTR output = Open(OUTPUT,MODE_NEWFILE);
    LONG result;
    if(!output) return -1;
    result=SystemTags(args,SYS_Asynch,FALSE,SYS_Output,output,SYS_Error,output,TAG_DONE);
    if(!Close(output)) return -1;
    return result;
}
static LONG read_output(void)
{
    BPTR input=Open(OUTPUT,MODE_OLDFILE);
    LONG size;
    UBYTE extra;
    if(!input) return -1;
    size=Read(input,text,sizeof(text)-1);
    if(size>=0 && Read(input,&extra,1)!=0) size=-1;
    Close(input);
    if(size>=0) text[size]=0;
    return size;
}
static BOOL directory(const char *path)
{ BPTR lock=CreateDir(path); if(!lock) return FALSE; UnLock(lock); return TRUE; }
static BOOL fixture_file(const char *path, ULONG bytes, ULONG seed)
{
    UBYTE b[1024]; ULONG i,at=0,n; BOOL ok=TRUE;
    BPTR output=Open(path,MODE_NEWFILE);
    if(!output) return FALSE;
    while(at<bytes) {
        n=bytes-at; if(n>sizeof(b))n=sizeof(b);
        for(i=0;i<n;i++)b[i]=(UBYTE)((at+i+seed)%251);
        if(Write(output,b,n)!=(LONG)n){ok=FALSE;break;} at+=n;
    }
    if(!Flush(output))ok=FALSE;
    if(!Close(output))ok=FALSE;
    return ok;
}
static BOOL verify_file(const char *path, ULONG bytes, ULONG seed)
{
    UBYTE b[1024],extra; ULONG at=0,i,n; BOOL ok=TRUE;
    BPTR input=Open(path,MODE_OLDFILE);
    if(!input)return FALSE;
    while(at<bytes) {
        n=bytes-at;if(n>sizeof(b))n=sizeof(b);
        if(Read(input,b,n)!=(LONG)n){ok=FALSE;break;}
        for(i=0;i<n;i++)if(b[i]!=(UBYTE)((at+i+seed)%251)){ok=FALSE;break;}
        if(!ok)break;at+=n;
    }
    if(Read(input,&extra,1)!=0)ok=FALSE;
    if(!Close(input))ok=FALSE;
    return ok;
}
static BOOL stat_file(const char *path, struct FileInfoBlock *fib)
{
    BPTR lock=Lock(path,SHARED_LOCK); BOOL ok;
    if(!lock)return FALSE;
    ok=Examine(lock,fib);UnLock(lock);return ok;
}
static BOOL same_metadata(const struct FileInfoBlock *a,const struct FileInfoBlock *b)
{
    return a->fib_Protection==b->fib_Protection && a->fib_Size==b->fib_Size &&
        a->fib_OwnerUID==b->fib_OwnerUID && a->fib_OwnerGID==b->fib_OwnerGID &&
        !strcmp((char *)a->fib_Comment,(char *)b->fib_Comment) &&
        CompareDates(&a->fib_Date,&b->fib_Date)==0;
}
static BOOL absent(const char *path)
{
    BPTR lock=Lock(path,SHARED_LOCK);
    if(lock){UnLock(lock);return FALSE;}
    return IoErr()==ERROR_OBJECT_NOT_FOUND;
}
static BOOL list_contains(const char *command_text,uint64_t id,BOOL backup,BOOL wanted)
{
    LONG n;
    if(command(command_text)!=RETURN_OK || (n=read_output())<0)return FALSE;
    return afscommands_list_has(text,(size_t)n,id,backup)==(wanted?1:0);
}
static BOOL has_line(const char *wanted,LONG length)
{
    size_t at=0,n=strlen(wanted);
    while(at<(size_t)length) {
        size_t end=at;while(end<(size_t)length && text[end]!='\n')end++;
        if(end-at==n && !memcmp(text+at,wanted,n))return TRUE;
        at=end+(end<(size_t)length);
    }
    return FALSE;
}

/* Exercise bounded API steps through the actual command. Its cursor stays
 * in memory; the command performs no cursor file or temporary-path writes. */
static BOOL reclaim_probe(const struct FileInfoBlock *captured)
{
    struct FileInfoBlock metadata;
    const char *field;
    char *end=NULL;
    unsigned long calls=0;
    LONG status;
    check(command("C:AFSPlusReclaim AFSPROBE: RAM: 1 1 64 128")==RETURN_FAIL,
        "reclaim-cross-handler-scope-refused");
    status=command("C:AFSPlusReclaim AFSPROBE: " ROOT " 1 1 64 128");
    if(read_output()>=0 && (field=strstr(text,"calls="))!=NULL)
        calls=strtoul(field+6,&end,10);
    check(status==RETURN_OK && calls>1 && calls<=128 && end && *end==' ' &&
        strstr(text,"blocked=0 blocked-id=0 complete=1\n")!=NULL,
        "reclaim-bounded-steps-in-memory-to-completion");
    check(verify_file(ROOT "/out/restored-tree/file",9000,7) &&
        verify_file(ROOT "/out/restored-tree/nested/child",139,19) &&
        stat_file(ROOT "/out/restored-tree/file",&metadata) && same_metadata(captured,&metadata),
        "reclaim-preserves-published-descendants-and-metadata");
    return status==RETURN_OK;
}

static BOOL clone_assign_probe(const struct FileInfoBlock *captured)
{
    struct DosList *list, *entry;
    struct FileInfoBlock metadata, source_shadow, target_shadow;
    BPTR lock;
    LONG n;
    BOOL ok;
    /* Never remove a pre-existing assign or root shadow. */
    list=LockDosList(LDF_ALL|LDF_READ);
    if(!list)return FALSE;
    entry=FindDosEntry(list,"AFSCMD",LDF_ALL);
    UnLockDosList(LDF_ALL|LDF_READ);
    if(entry || !absent("AFSPROBE:out"))return FALSE;
    if(!directory("AFSPROBE:out"))return FALSE;
    clone_shadow_owned=TRUE;
    if(!fixture_file("AFSPROBE:out/restored-file",73,31) ||
        !fixture_file("AFSPROBE:out/assigned-clone",61,79) ||
        !stat_file("AFSPROBE:out/restored-file",&source_shadow) ||
        !stat_file("AFSPROBE:out/assigned-clone",&target_shadow))return FALSE;
    lock=Lock(ROOT,SHARED_LOCK);
    if(!lock)return FALSE;
    if(!AssignLock("AFSCMD",lock)){UnLock(lock);return FALSE;}
    clone_assign_owned=TRUE;
    ok=command("C:AFSPlusClone AFSCMD:out/restored-file AFSCMD:out assigned-clone")==RETURN_OK &&
        (n=read_output())==(LONG)strlen("AFSPlusClone: cloned\n") &&
        !memcmp(text,"AFSPlusClone: cloned\n",(size_t)n) &&
        verify_file(ROOT "/out/assigned-clone",9000,7) &&
        stat_file(ROOT "/out/assigned-clone",&metadata) && same_metadata(captured,&metadata) &&
        verify_file("AFSPROBE:out/restored-file",73,31) &&
        verify_file("AFSPROBE:out/assigned-clone",61,79) &&
        stat_file("AFSPROBE:out/restored-file",&metadata) && same_metadata(&source_shadow,&metadata) &&
        stat_file("AFSPROBE:out/assigned-clone",&metadata) && same_metadata(&target_shadow,&metadata);
    check(ok,"clone-assigned-source-target-root-shadows-preserved");
    if(!ok)return FALSE;
    check(command("C:AFSPlusClone AFSCMD:out/restored-file AFSCMD:out assigned-clone")==RETURN_FAIL &&
        verify_file(ROOT "/out/assigned-clone",9000,7) &&
        verify_file(ROOT "/out/restored-file",9000,7) &&
        stat_file(ROOT "/out/assigned-clone",&metadata) && same_metadata(captured,&metadata) &&
        stat_file(ROOT "/out/restored-file",&metadata) && same_metadata(captured,&metadata) &&
        verify_file("AFSPROBE:out/restored-file",73,31) &&
        verify_file("AFSPROBE:out/assigned-clone",61,79) &&
        stat_file("AFSPROBE:out/restored-file",&metadata) && same_metadata(&source_shadow,&metadata) &&
        stat_file("AFSPROBE:out/assigned-clone",&metadata) && same_metadata(&target_shadow,&metadata),
        "clone-existing-assigned-target-refused-preserved");
    return TRUE;
}

/* Only the probe-owned subtree is removed. Unexpected children make
 * DeleteFile(directory) fail and therefore prevent a PASS verdict. */
static void cleanup(void)
{
    static const char *paths[]={
        ROOT "/out/restored-tree/nested/empty",ROOT "/out/restored-tree/nested/child",
        ROOT "/out/restored-tree/nested",ROOT "/out/restored-tree/file",ROOT "/out/restored-tree",
        ROOT "/out/assigned-clone",ROOT "/out/restored-file",ROOT "/out/occupied",ROOT "/out",
        ROOT "/source/nested/empty",ROOT "/source/nested/child",ROOT "/source/nested",
        ROOT "/source/file",ROOT "/source",ROOT};
    unsigned i;
    if(clone_assign_owned && !AssignLock("AFSCMD",BNULL))check(FALSE,"clone-assign-cleanup");
    if(clone_shadow_owned)
    {
        static const char *shadow[]={"AFSPROBE:out/assigned-clone","AFSPROBE:out/restored-file","AFSPROBE:out"};
        for(i=0;i<sizeof(shadow)/sizeof(shadow[0]);i++)
            if(!DeleteFile(shadow[i]) && IoErr()!=ERROR_OBJECT_NOT_FOUND)check(FALSE,"clone-root-shadow-cleanup");
    }
    for(i=0;i<sizeof(paths)/sizeof(paths[0]);i++)
        if(!DeleteFile(paths[i]) && IoErr()!=ERROR_OBJECT_NOT_FOUND)
            check(FALSE,"fixture-cleanup");
    if(!DeleteFile(OUTPUT) && IoErr()!=ERROR_OBJECT_NOT_FOUND)check(FALSE,"output-cleanup");
}

int main(void)
{
    IPTR args[1]={0};
    struct RDArgs *rda=ReadArgs("DISPOSABLEVM/S",args,NULL);
    struct FileInfoBlock captured,restored;
    struct DevProc *device;
    struct DateStamp timestamp={10000,123,456};
    uint64_t id=0,second=0;
    char decimal[21],hex[19],cmd[384];
    LONG n;
    BOOL owned=FALSE;
    if(!rda || !args[0]){report("result=FAIL reason=DISPOSABLEVM-required");return RETURN_FAIL;}
    FreeArgs(rda);
    device=GetDeviceProc("AFSPROBE:",NULL);
    if(!afsprobe_bound_device(device,91,"AFSPROBE")) {
        if(device)FreeDeviceProc(device);
        report("result=FAIL reason=fdsk-unit91-identity-required");return RETURN_FAIL;
    }
    FreeDeviceProc(device);
    if(!absent(ROOT)){report("result=FAIL reason=existing-or-unavailable-fixture");return RETURN_FAIL;}
    if(!directory(ROOT)){report("result=FAIL reason=fixture-claim");return RETURN_FAIL;}
    owned=TRUE;
    check(directory(ROOT "/source") && directory(ROOT "/source/nested") && directory(ROOT "/out"),"fixture-directories");
    check(fixture_file(ROOT "/source/file",9000,7) && fixture_file(ROOT "/source/nested/child",139,19) &&
        fixture_file(ROOT "/source/nested/empty",0,0),"fixture-files");
    check(SetComment(ROOT "/source/file","CLI captured metadata") &&
        SetProtection(ROOT "/source/file",FIBF_EXECUTE) &&
        SetFileDate(ROOT "/source/file",&timestamp) && stat_file(ROOT "/source/file",&captured),"fixture-metadata");
    if(failures)goto done;
    check(command("C:AFSPlusBackup AFSPROBE:")==RETURN_OK && (n=read_output())>=0 &&
        afscommands_capture(text,(size_t)n,&id),"backup-capture-decimal64");
    if(!id)goto done;
    snprintf(decimal,sizeof(decimal),"%llu",(unsigned long long)id);
    snprintf(hex,sizeof(hex),"0x%016llx",(unsigned long long)id);
    check(list_contains("C:AFSPlusBackup AFSPROBE: LIST",id,TRUE,TRUE),"backup-list-capture");
    check(list_contains("C:AFSPlusSnapshot AFSPROBE: LIST",id,FALSE,TRUE),"snapshot-list-capture");
    check(fixture_file(ROOT "/source/file",11,91),"live-source-diverged");
    snprintf(cmd,sizeof(cmd),"C:AFSPlusSnapshot AFSPROBE: VIEW %s AFSCommands/source/file",hex);
    check(command(cmd)==RETURN_OK && verify_file(OUTPUT,9000,7),"snapshot-view-hex64-captured-file");
    snprintf(cmd,sizeof(cmd),"C:AFSPlusSnapshot AFSPROBE: VIEW %s AFSCommands/source",hex);
    check(command(cmd)==RETURN_OK && (n=read_output())>=0 && has_line("file",n) && has_line("nested",n),"snapshot-view-directory");
    snprintf(cmd,sizeof(cmd),"C:AFSPlusRestore AFSPROBE: %s AFSCommands/source/file " ROOT "/out restored-file",decimal);
    check(command(cmd)==RETURN_OK && verify_file(ROOT "/out/restored-file",9000,7) &&
        stat_file(ROOT "/out/restored-file",&restored) && same_metadata(&captured,&restored),"restore-fresh-file-and-metadata");
    snprintf(cmd,sizeof(cmd),"C:AFSPlusRestore AFSPROBE: %s AFSCommands/source " ROOT "/out restored-tree",decimal);
    check(command(cmd)==RETURN_OK && verify_file(ROOT "/out/restored-tree/file",9000,7) &&
        verify_file(ROOT "/out/restored-tree/nested/child",139,19) &&
        verify_file(ROOT "/out/restored-tree/nested/empty",0,0),"restore-fresh-tree");
    check(fixture_file(ROOT "/out/occupied",61,83),"occupied-target-fixture");
    snprintf(cmd,sizeof(cmd),"C:AFSPlusRestore AFSPROBE: %s AFSCommands/source/file " ROOT "/out occupied",decimal);
    check(command(cmd)==RETURN_FAIL && verify_file(ROOT "/out/occupied",61,83),"restore-existing-file-refused-preserved");
    snprintf(cmd,sizeof(cmd),"C:AFSPlusRestore AFSPROBE: %s AFSCommands/source " ROOT "/out restored-tree",decimal);
    check(command(cmd)==RETURN_FAIL && verify_file(ROOT "/out/restored-tree/file",9000,7) &&
        verify_file(ROOT "/out/restored-tree/nested/child",139,19) &&
        verify_file(ROOT "/out/restored-tree/nested/empty",0,0) &&
        stat_file(ROOT "/out/restored-tree/file",&restored) && same_metadata(&captured,&restored),
        "restore-existing-tree-refused-preserved");
    check(clone_assign_probe(&captured),"clone-assigned-fixture");
    check(reclaim_probe(&captured),"reclaim-cli-maintenance");
    /* Also exercise Snapshot CREATE's bare output token separately. */
    check(command("C:AFSPlusSnapshot AFSPROBE: CREATE")==RETURN_OK && (n=read_output())>1 &&
        text[n-1]=='\n' && afscommands_id(text,(size_t)n-1,&second),"snapshot-create-decimal64");
done:
    if(second) {
        snprintf(cmd,sizeof(cmd),"C:AFSPlusSnapshot AFSPROBE: DELETE %llu",(unsigned long long)second);
        check(command(cmd)==RETURN_OK,"snapshot-created-capture-delete");
    }
    if(id) {
        snprintf(cmd,sizeof(cmd),"C:AFSPlusSnapshot AFSPROBE: DELETE 0x%016llx",(unsigned long long)id);
        check(command(cmd)==RETURN_OK,"snapshot-delete-hex64-no-leaked-readers");
        check(list_contains("C:AFSPlusBackup AFSPROBE: LIST",id,TRUE,FALSE) &&
            list_contains("C:AFSPlusSnapshot AFSPROBE: LIST",id,FALSE,FALSE),"deleted-capture-absent-from-lists");
        snprintf(cmd,sizeof(cmd),"C:AFSPlusSnapshot AFSPROBE: VIEW 0x%016llx AFSCommands/source/file",(unsigned long long)id);
        check(command(cmd)==RETURN_FAIL,"deleted-capture-view-refused");
    }
    if(owned) {cleanup();check(absent(ROOT),"cleanup-owned-subtree");}
    report("result=%s files=3 large-bytes=9000 real-cli=5 metadata=1 fresh-targets=1 failures=%u",failures?"FAIL":"PASS",failures);
    return failures?RETURN_FAIL:RETURN_OK;
}
