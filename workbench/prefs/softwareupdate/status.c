/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
/* Read-only view of the installed system's package record and of
 * SystemUpdate's depot: the receipt PkgInstall writes for each system
 * update (AFSSYS:S/PkgInstall.receipt, "key: value" lines), the interrupted
 * marker and the delivered update. No APPLY, mounts, part assembly, writes
 * or cleanup are performed by this reader. */
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>
#include "status.h"
#include "../../../arch/aarch64-apple/c/system_update_index.h"

#define RECEIPT "AFSSYS:S/PkgInstall.receipt"

/* One field of the receipt, printable; "" when absent or "-". */
static void receipt_field(const char *text,const char *key,char *out,unsigned size)
{
    char want[48]; const char *at; unsigned i=0;
    out[0]=0;
    snprintf(want,sizeof(want),"%s: ",key);
    for(at=text;at;at=strchr(at,'\n')) {
        if(*at=='\n') at++;
        if(strncmp(at,want,strlen(want))==0) {
            at+=strlen(want);
            while(at[i] && at[i]!='\n' && at[i]!='\r' && i+1<size) {
                out[i]=(unsigned char)at[i]<32 || (unsigned char)at[i]>126 ? '?' : at[i];
                i++;
            }
            out[i]=0;
            if(strcmp(out,"-")==0) out[0]=0;
            return;
        }
    }
}

static void receipt(struct UpdateStatus *status)
{
    char text[4096],verdict[16],operation[16],selected[64],previous[64],failure[120];
    BPTR fh=Open(RECEIPT,MODE_OLDFILE); LONG n;
    if(!fh) {
        BOOL absent=IoErr()==ERROR_OBJECT_NOT_FOUND;
        strcpy(status->installed,absent?"Not installed from a package: no receipt":"Receipt unavailable");
        strcpy(status->previous,absent?"None recorded":"Receipt unavailable");
        strcpy(status->last,absent?"No system update recorded":"Receipt unavailable");
        return;
    }
    n=Read(fh,text,sizeof(text)-1); Close(fh);
    if(n<=0) { strcpy(status->installed,"Unreadable receipt"); strcpy(status->previous,"Unreadable receipt");
               strcpy(status->last,"Unreadable receipt"); return; }
    text[n]=0;
    receipt_field(text,"verdict",verdict,sizeof(verdict));
    receipt_field(text,"operation",operation,sizeof(operation));
    receipt_field(text,"selected-version",selected,sizeof(selected));
    receipt_field(text,"previous-version",previous,sizeof(previous));
    receipt_field(text,"failure",failure,sizeof(failure));
    if(strcmp(verdict,"PASS")==0 && selected[0])
        snprintf(status->installed,UPDATE_STATUS_SIZE,"aros-system %s",selected);
    else if(previous[0])
        snprintf(status->installed,UPDATE_STATUS_SIZE,"aros-system %s (the last update did not complete)",previous);
    else
        strcpy(status->installed,"Unknown: the last update did not complete");
    if(previous[0]) snprintf(status->previous,UPDATE_STATUS_SIZE,"aros-system %s",previous);
    else strcpy(status->previous,"None recorded");
    snprintf(status->last,UPDATE_STATUS_SIZE,"%s %s%s%s",operation[0]?operation:"Update",
        verdict[0]?verdict:"without a verdict",failure[0]?": ":"",failure);
}
static void delivered(char *out)
{
    BPTR depot=Lock("WORK:PkgDepot",SHARED_LOCK);
    struct FileInfoBlock *fib;
    unsigned seen=0,found=0;
    char candidate[108]="",path[256],index[SYSTEM_UPDATE_INDEX_MAX];
    LONG error=0;
    if(!depot) { strcpy(out,"Update depot unavailable"); return; }
    fib=AllocDosObject(DOS_FIB,NULL);
    if(!fib || !Examine(depot,fib)) { strcpy(out,"Cannot read update depot"); goto done; }
    while(ExNext(depot,fib)) {
        BPTR ready,fh; LONG n; int kind;
        if(++seen>256) { strcpy(out,"Update depot exceeds scan limit"); goto done; }
        if(fib->fib_DirEntryType<=0) continue;
        snprintf(path,sizeof(path),"WORK:PkgDepot/%s/READY",fib->fib_FileName);
        ready=Lock(path,SHARED_LOCK);
        if(!ready) {
            if(IoErr()!=ERROR_OBJECT_NOT_FOUND) { strcpy(out,"Cannot read delivery marker"); goto done; }
            continue;
        }
        UnLock(ready);
        snprintf(path,sizeof(path),"WORK:PkgDepot/%s/index",fib->fib_FileName);
        fh=Open(path,MODE_OLDFILE);
        if(!fh) { strcpy(out,"Delivery index unavailable"); goto done; }
        n=Read(fh,index,sizeof(index)); Close(fh);
        kind=n<0?-1:SystemUpdateIndexKind(index,(size_t)n);
        if(!kind) continue;
        if(kind<0 || found) { strcpy(out,"Invalid or multiple delivered system updates"); goto done; }
        found=1; snprintf(candidate,sizeof(candidate),"%s",fib->fib_FileName);
    }
    error=IoErr();
    if(error!=ERROR_NO_MORE_ENTRIES) strcpy(out,"Update depot scan failed");
    else if(found) {
        unsigned i;
        for(i=0;candidate[i];i++) if((unsigned char)candidate[i]<32 || (unsigned char)candidate[i]>126) candidate[i]='?';
        snprintf(out,UPDATE_STATUS_SIZE,"Delivered: %s (apply validation required)",candidate);
    } else strcpy(out,"No delivered system update");
done:
    if(fib) FreeDosObject(DOS_FIB,fib);
    UnLock(depot);
}
void SoftwareUpdateReadStatus(struct UpdateStatus *status)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR oldWindow = process->pr_WindowPtr;
    process->pr_WindowPtr = (APTR)-1;
    BPTR root=Lock("AFSSYS:",SHARED_LOCK),pending;
    if(root) {
        UnLock(root);
        receipt(status);
        pending=Lock("AFSSYS:S/SystemUpdate.pending",SHARED_LOCK);
        if(pending) { UnLock(pending); strcpy(status->pending,"Interrupted update: installed-system startup is blocked"); }
        else strcpy(status->pending,IoErr()==ERROR_OBJECT_NOT_FOUND?"No interrupted update marker":"Update marker unavailable");
    } else {
        strcpy(status->installed,"Installed system volume unavailable");
        strcpy(status->previous,"Installed system volume unavailable");
        strcpy(status->last,"Installed system volume unavailable");
        strcpy(status->pending,"Installed system volume unavailable");
    }
    delivered(status->delivered);
    process->pr_WindowPtr = oldWindow;
}
