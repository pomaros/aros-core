/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include "tile_transaction.h"
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <exec/memory.h>
#include <aros/debug.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>
static unsigned busy,sequence,childID;
/* Pending positions belong to the GUI task. Workers borrow only their own
 * allocated request, never queue storage. One receipt gates every next child. */
#define TILE_DROP_PENDING 40
static struct TileDropRequest pending[TILE_DROP_PENDING];
static unsigned pendingCount;
static int launch(const struct TileDropRequest *input);
static void drain(void)
{
    if(!pendingCount || __atomic_load_n(&busy,__ATOMIC_ACQUIRE) ||
       __atomic_load_n(&childID,__ATOMIC_ACQUIRE))return;
    /* Leave the owned request queued if memory/process creation fails. */
    if(launch(&pending[0])) {
        --pendingCount;
        memmove(pending,pending+1,pendingCount*sizeof(pending[0]));
    }
}
/* Parent-thread only. ChildFree releases the exact DOS death receipt;
 * busy=0 is not proof of task exit or safe code unloading. */
void TileDropPoll(void)
{
    unsigned id=__atomic_load_n(&childID,__ATOMIC_ACQUIRE);
    if(id&&ChildStatus(id)==CHILD_EXITED) {
        ChildFree(id);
        __atomic_store_n(&childID,0,__ATOMIC_RELEASE);
    }
    drain();
}
void TileDropFinish(void)
{
    do {
        unsigned id;
        /* Also covers a child not yet scheduled on SMP. */
        while(!(id=__atomic_load_n(&childID,__ATOMIC_ACQUIRE))&&
              __atomic_load_n(&busy,__ATOMIC_ACQUIRE))Delay(1);
        if(id) {
            ChildWait(id);
            ChildFree(id);
            __atomic_store_n(&childID,0,__ATOMIC_RELEASE);
        }
        drain();
        /* Retry resource shortages without spinning; accepted positions remain
         * owned until saved. Only explicit application quit waits here. */
        if(pendingCount&&!__atomic_load_n(&busy,__ATOMIC_ACQUIRE))Delay(1);
    } while(pendingCount||__atomic_load_n(&busy,__ATOMIC_ACQUIRE)||
            __atomic_load_n(&childID,__ATOMIC_ACQUIRE));
}

static int exists(const char *path)
{
    BPTR lock=Lock(path,ACCESS_READ);
    if(lock){UnLock(lock);return 1;}
    return IoErr()!=ERROR_OBJECT_NOT_FOUND; /* errors cannot authorize overwrite */
}
static int move(void *unused,const char *from,const char *to)
{
    (void)unused;
    return !exists(to)&&Rename(from,to);
}
static int suffix(char *out,const char *path,const char *tail)
{
    return snprintf(out,TILE_TXN_PATH,"%s%s",path,tail)<TILE_TXN_PATH;
}
static int group_icon(struct DiskObject *icon)
{
    STRPTR type=icon?FindToolType(icon->do_ToolTypes,"TILE"):NULL;
    return icon&&icon->do_Type==WBDRAWER&&type&&!Stricmp(type,"group");
}
static int tile_icon(struct DiskObject *icon)
{
    STRPTR type=icon?FindToolType(icon->do_ToolTypes,"TILE"):NULL;
    return icon&&icon->do_Type!=WBDRAWER&&type&&Stricmp(type,"group");
}
static int parent_group(const char *path)
{
    char parent[TILE_TXN_PATH];struct DiskObject *icon;int result;
    if(strlen(path)>=sizeof(parent))return 0;strcpy(parent,path);
    char *part=PathPart(parent);if(part<=parent)return 0;*part=0;
    icon=GetDiskObject(parent);result=group_icon(icon);if(icon)FreeDiskObject(icon);return result;
}
/* Stage a replacement .info beside its original, journal the old icon first,
 * then install the staged copy. Temporary paths are collision-checked. */
static int stage_icon(const char *path,struct DiskObject *icon,const char *stamp,char *stage,char *directory)
{
    if(snprintf(directory,TILE_TXN_PATH,"%s.tile-stage-%s",path,stamp)>=TILE_TXN_PATH) {directory[0]=0;return 0;}
    BPTR lock=CreateDir(directory);if(!lock){directory[0]=0;return 0;}UnLock(lock);
    if(snprintf(stage,TILE_TXN_PATH,"%s/icon",directory)>=TILE_TXN_PATH)return 0;
    return PutDiskObject(stage,icon);
}
static int replacement(struct TileRenamePlan *plan,const char *path,struct DiskObject *icon,
                       const char *stamp,char *stage,char *backup,char *directory)
{
    char original[TILE_TXN_PATH],stageInfo[TILE_TXN_PATH],backupInfo[TILE_TXN_PATH];
    const char *leaf=FilePart(path);
    if(snprintf(backup,TILE_TXN_PATH,"%.*s.tile-old-%s-%s",(int)(leaf-path),path,stamp,leaf)>=TILE_TXN_PATH||
       !suffix(original,path,".info")||!suffix(backupInfo,backup,".info")||exists(backupInfo))return 0;
    if(!stage_icon(path,icon,stamp,stage,directory)||!suffix(stageInfo,stage,".info"))return 0;
    return TileRenameAdd(plan,original,backupInfo)&&TileRenameAdd(plan,stageInfo,original);
}
/* A left-out entry is relative to the volume root, beginning with ':'.
 * Preserve other entries byte-for-byte, bounded to64KiB; install by journal. */
static int backdrop(struct TileRenamePlan *plan,const char *definition,const char *stamp,
                    char *stage,char *backup,char *directory)
{
    char absolute[TILE_TXN_PATH],parent[TILE_TXN_PATH],original[TILE_TXN_PATH];
    BPTR lock,file;char *colon,*buffer;LONG size=0,got;int ok=0;
    strcpy(parent,definition);char *leaf=FilePart(parent);char name[128];
    if(strlen(leaf)>=sizeof(name))return 0;strcpy(name,leaf);*leaf=0;
    lock=Lock(parent,ACCESS_READ);if(!lock)return 0;
    ok=NameFromLock(lock,absolute,sizeof(absolute));UnLock(lock);if(!ok)return 0;
    if(!AddPart(absolute,name,sizeof(absolute))||!(colon=strchr(absolute,':')))return 0;
    if((size_t)(colon-absolute)+10>=sizeof(original))return 0;
    memcpy(original,absolute,colon-absolute+1);strcpy(original+(colon-absolute+1),".backdrop");
    if(snprintf(backup,TILE_TXN_PATH,"%s.old-%s",original,stamp)>=TILE_TXN_PATH||exists(backup))return 0;
    if(snprintf(directory,TILE_TXN_PATH,"%s.tile-stage-%s",original,stamp)>=TILE_TXN_PATH){directory[0]=0;return 0;}
    lock=CreateDir(directory);if(!lock){directory[0]=0;return 0;}UnLock(lock);
    if(snprintf(stage,TILE_TXN_PATH,"%s/data",directory)>=TILE_TXN_PATH)return 0;
    buffer=AllocVec(65536,MEMF_ANY);if(!buffer)return 0;ok=0;
    int had=exists(original);file=Open(original,MODE_OLDFILE);
    if(had&&!file)goto done;
    if(file){while(size<64000&&(got=Read(file,buffer+size,64000-size))>0)size+=got;Close(file);if(size>=64000||got<0)goto done;}
    size=TileBackdropAppend(buffer,size,65536,colon);if(size<0)goto done;
    file=Open(stage,MODE_NEWFILE);if(!file)goto done;ok=Write(file,buffer,size)==size;Close(file);if(!ok)goto done;
    if(had&&!TileRenameAdd(plan,original,backup)){ok=0;goto done;}
    ok=TileRenameAdd(plan,stage,original);
done:FreeVec(buffer);return ok;
}
static void worker(void)
{
    struct Process *process=(struct Process *)FindTask(NULL);
    __atomic_store_n(&childID,GetETaskID(process),__ATOMIC_RELEASE);
    struct TileDropRequest *request=process->pr_Task.tc_UserData;
    struct TileRenamePlan *plan=AllocVec(sizeof(*plan),MEMF_CLEAR);
    struct DiskObject *source=NULL,*target=NULL,*group=NULL;
    char stamp[32],stage[TILE_TXN_PATH]={0},backup[TILE_TXN_PATH]={0};
    char folder[TILE_TXN_PATH]={0},final[TILE_TXN_PATH]={0},first[TILE_TXN_PATH]={0},second[TILE_TXN_PATH]={0};
    char stageDir[TILE_TXN_PATH]={0},backDir[TILE_TXN_PATH]={0};
    char sourceInfo[TILE_TXN_PATH],targetInfo[TILE_TXN_PATH],aBackup[TILE_TXN_PATH]={0},bBackup[TILE_TXN_PATH]={0};
    char stagedGroup[TILE_TXN_PATH],finalGroup[TILE_TXN_PATH],backStage[TILE_TXN_PATH]={0},backOld[TILE_TXN_PATH]={0};
    int success=0,folderCreated=0;unsigned serial=++sequence;
    snprintf(stamp,sizeof(stamp),"%08lx-%u",(unsigned long)process,serial);
    if(!plan||(source=GetDiskObject(request->source))==NULL)goto done;
    if(!suffix(sourceInfo,request->source,".info"))goto done;
    if(request->operation==TILE_TXN_POSITION) {
        if(!group_icon(source)&&!tile_icon(source))goto done;
        source->do_CurrentX=request->x;source->do_CurrentY=request->y;
        if(!replacement(plan,request->source,source,stamp,stage,backup,stageDir))goto done;
    } else if(request->operation==TILE_TXN_EXTRACT) {
        if(!tile_icon(source)||!parent_group(request->source))goto done;
        if(snprintf(final,sizeof(final),"HOME:Tiles/%s",FilePart(request->source))>=sizeof(final)||
           !suffix(finalGroup,final,".info")||exists(finalGroup))goto done;
        source->do_CurrentX=request->x;source->do_CurrentY=request->y;
        if(!stage_icon(final,source,stamp,stage,stageDir)||!suffix(stagedGroup,stage,".info"))goto done;
        if(snprintf(aBackup,sizeof(aBackup),"%s.tile-old-%s",sourceInfo,stamp)>=sizeof(aBackup)||exists(aBackup))goto done;
        if(!TileRenameAdd(plan,sourceInfo,aBackup)||!TileRenameAdd(plan,stagedGroup,finalGroup)||
           !backdrop(plan,final,stamp,backStage,backOld,backDir))goto done;
    } else if(request->operation==TILE_TXN_CREATE) {
        if(!tile_icon(source)||!strcmp(request->source,request->target)||(target=GetDiskObject(request->target))==NULL||
           !tile_icon(target)||!Stricmp(FilePart(request->source),FilePart(request->target)))goto done;
        if(snprintf(folder,sizeof(folder),"HOME:Tiles/.tile-group-%s",stamp)>=sizeof(folder)||
           snprintf(final,sizeof(final),"HOME:Tiles/Group-%s",stamp)>=sizeof(final)||exists(final)||
           !suffix(stagedGroup,folder,".info")||!suffix(finalGroup,final,".info")||exists(stagedGroup)||exists(finalGroup))goto done;
        BPTR lock=CreateDir(folder);if(!lock)goto done;UnLock(lock);folderCreated=1;
        if(snprintf(first,sizeof(first),"%s/%s",folder,FilePart(request->target))>=sizeof(first)||
           snprintf(second,sizeof(second),"%s/%s",folder,FilePart(request->source))>=sizeof(second))goto done;
        target->do_CurrentX=0;target->do_CurrentY=0;
        unsigned cellsW=1,cellsH=1;STRPTR size=FindToolType(target->do_ToolTypes,"SIZE");
        if(size&&(sscanf(size,"%ux%u",&cellsW,&cellsH)!=2||cellsW<1||cellsW>4||cellsH<1||cellsH>4))cellsW=1;
        source->do_CurrentX=cellsW*64;source->do_CurrentY=0;
        if(!PutDiskObject(first,target)||!PutDiskObject(second,source))goto done;
        group=GetDefDiskObject(WBDRAWER);if(!group)goto done;
        STRPTR props[]={"TILE=group","SHOW=always","ANCHOR=free","COLUMNS=4",NULL};
        STRPTR *old=group->do_ToolTypes;group->do_ToolTypes=props;group->do_CurrentX=request->x;group->do_CurrentY=request->y;
        int wrote=PutDiskObject(folder,group);group->do_ToolTypes=old;if(!wrote)goto done;
        if(!suffix(targetInfo,request->target,".info")||
           snprintf(aBackup,sizeof(aBackup),"%s.tile-old-%s",sourceInfo,stamp)>=sizeof(aBackup)||
           snprintf(bBackup,sizeof(bBackup),"%s.tile-old-%s",targetInfo,stamp)>=sizeof(bBackup)||exists(aBackup)||exists(bBackup))goto done;
        if(!TileRenameAdd(plan,sourceInfo,aBackup)||!TileRenameAdd(plan,targetInfo,bBackup)||
           !TileRenameAdd(plan,folder,final)||!TileRenameAdd(plan,stagedGroup,finalGroup))goto done;
    } else goto done;
    success=TileRenameExecute(plan,move,NULL);
    if(success) {
        char path[TILE_TXN_PATH];
        if(backup[0]&&suffix(path,backup,".info"))DeleteFile(path);
        if(aBackup[0])DeleteFile(aBackup);if(bBackup[0])DeleteFile(bBackup);if(backOld[0])DeleteFile(backOld);
        BPTR env=CreateDir("ENV:Tiles");if(env)UnLock(env);
        if(!SetVar("Tiles/Changed",stamp,-1,GVF_GLOBAL_ONLY))
            bug("[TILE3] notification failed error=%ld\n",(long)IoErr());
    }
done:
    if(!success&&(!plan||!plan->rollbackFailed)) {
        char path[TILE_TXN_PATH];
        if(stageDir[0]&&stage[0]&&suffix(path,stage,".info"))DeleteFile(path);
        if(backDir[0]&&backStage[0])DeleteFile(backStage);
        if(folderCreated){if(first[0]&&suffix(path,first,".info"))DeleteFile(path);if(second[0]&&suffix(path,second,".info"))DeleteFile(path);if(suffix(path,folder,".info"))DeleteFile(path);DeleteFile(folder);}
    }
    if(!plan||!plan->rollbackFailed){if(stageDir[0])DeleteFile(stageDir);if(backDir[0])DeleteFile(backDir);}
    bug("[TILE3] transaction op=%u result=%s source=%s target=%s output=%s x=%ld y=%ld recovery=%u\n",request->operation,success?"committed":"rejected",request->source,request->target,final,request->x,request->y,plan?plan->rollbackFailed:0);
    if(plan&&plan->rollbackFailed)for(unsigned n=0;n<plan->count;++n)
        bug("[TILE3] recovery step=%u from=%s to=%s completed=%u\n",n,plan->steps[n].from,plan->steps[n].to,plan->completed);
    if(source)FreeDiskObject(source);if(target)FreeDiskObject(target);if(group)FreeDiskObject(group);if(plan)FreeVec(plan);FreeVec(request);
    __atomic_store_n(&busy,0,__ATOMIC_RELEASE);
}
static int launch(const struct TileDropRequest *input)
{
    struct TileDropRequest *request;unsigned expected=0;
    if(!__atomic_compare_exchange_n(&busy,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))return 0;
    request=AllocVec(sizeof(*request),MEMF_ANY);if(!request)goto failed;
    *request=*input;
    if(CreateNewProcTags(NP_Entry,(IPTR)worker,NP_Name,(IPTR)"Tile drawer transaction",NP_UserData,(IPTR)request,NP_StackSize,65536,NP_NotifyOnDeath,TRUE,TAG_DONE))return 1;
    FreeVec(request);
failed:__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return 0;
}
int TileDropStart(const struct TileDropRequest *input)
{
    if(!input||!memchr(input->source,0,sizeof(input->source))||!memchr(input->target,0,sizeof(input->target))||
       !strchr(input->source,':')||!FilePart(input->source)[0]||input->x < -32767||input->x>32767||input->y < -32767||input->y>32767||
       input->operation<TILE_TXN_CREATE||input->operation>TILE_TXN_POSITION)return 0;
    TileDropPoll();
    if(input->operation!=TILE_TXN_POSITION) {
        if(pendingCount||__atomic_load_n(&busy,__ATOMIC_ACQUIRE)||
           __atomic_load_n(&childID,__ATOMIC_ACQUIRE))return 0;
        return launch(input);
    }
    /* Repeated moves of a queued definition need only its latest position.
     * Structural operations cannot pass the queue, so paths remain ordered. */
    for(unsigned n=0;n<pendingCount;++n)
        if(!Stricmp(pending[n].source,input->source)) {
            pending[n]=*input;return 1;
        }
    if(pendingCount==TILE_DROP_PENDING)return 0;
    pending[pendingCount++]=*input;
    drain();
    return 1;
}
