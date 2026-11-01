/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License 1.1. */
#include <proto/dos.h>
#include <dos/dos.h>
#include <aros/debug.h>
#include <stdio.h>
#include <string.h>
#define FILES 32
#define FILE_BYTES 32768
int main(int argc,char **argv)
{
    BPTR handles[FILES]={0},directory;char root[224],name[256];
    UBYTE data[512];unsigned created=0,index=0;int result=20;const char *phase="prepare";
    if(argc!=2 || strlen(argv[1])>160) {
        puts("Usage: cache-open-files <disposable FAT volume or directory>");return 20;
    }
    size_t n=strlen(argv[1]);
    snprintf(root,sizeof(root),"%s%scache-open-files",argv[1],n && (argv[1][n-1]==':' || argv[1][n-1]=='/')?"":"/");
    directory=CreateDir((CONST_STRPTR)root);
    if(!directory) {puts("FAIL exclusive test-directory creation; existing content untouched");return 20;}
    UnLock(directory);
    for(unsigned i=0;i<FILES;++i) {
        index=i;
        snprintf(name,sizeof(name),"%s/file-%02u.dat",root,i);
        phase="create";handles[i]=Open((CONST_STRPTR)name,MODE_NEWFILE);
        if(!handles[i])goto cleanup;
        ++created;phase="write";memset(data,(int)(i+1),sizeof(data));
        for(unsigned at=0;at<FILE_BYTES;at+=sizeof(data))
            if(Write(handles[i],data,(LONG)sizeof(data))!=(LONG)sizeof(data))goto cleanup;
        if(!Close(handles[i])) {handles[i]=BNULL;goto cleanup;}
        handles[i]=BNULL;
    }
    /* Each file spans two16KiB FAT cache ranges. Keep all32 readers open,
     * exceeding the16-range cache of the targeted small-volume profile. */
    for(unsigned i=0;i<FILES;++i) {
        index=i;
        snprintf(name,sizeof(name),"%s/file-%02u.dat",root,i);
        phase="reader-open";handles[i]=Open((CONST_STRPTR)name,MODE_OLDFILE);
        if(!handles[i])goto cleanup;
        phase="reader-seek";if(Seek(handles[i],FILE_BYTES-1,OFFSET_BEGINNING)==-1)goto cleanup;
        phase="reader-read";if(Read(handles[i],data,1)!=1 || data[0]!=i+1)goto cleanup;
    }
    phase="reread";
    for(unsigned i=0;i<FILES;++i)
        if(Seek(handles[i],0,OFFSET_BEGINNING)==-1 || Read(handles[i],data,1)!=1 || data[0]!=i+1)goto cleanup;
    result=0;
cleanup:
    if(result)bug("[FATCACHE] failure phase=%s index=%u error=%ld\n",phase,index,(long)IoErr());
    for(unsigned i=0;i<FILES;++i)if(handles[i] && !Close(handles[i]))result=20;
    for(unsigned i=0;i<created;++i) {
        snprintf(name,sizeof(name),"%s/file-%02u.dat",root,i);
        if(!DeleteFile((CONST_STRPTR)name))result=20;
    }
    if(!DeleteFile((CONST_STRPTR)root))result=20;
    printf("[FATCACHE] %s readers=%u bytes=%u reread=%u cleanup=%u\n",result?"FAIL":"PASS",FILES,FILE_BYTES,result?0:1,result?0:1);
    return result;
}
