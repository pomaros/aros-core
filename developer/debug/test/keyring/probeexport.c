/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "probeexport.h"
#define EXPORT_LIMIT (1024u*1024u)
static size_t exported_bytes,exported_files,visited_entries;
static int safe_component(const char *name)
{
    return name[0] && strcmp(name,".") && strcmp(name,"..") && !strchr(name,':') && !strchr(name,'/') && !strchr(name,'\\');
}
/* A failed migration must not turn the serial export into a credential log.
 * The complete marker and each Bluetooth key are covered by its 16-byte prefix. */
static int contains_test_secret(const void *bytes,size_t length)
{
    const unsigned char *data=bytes;
    unsigned char marker[16];size_t i;int found=0;
    for(i=0;i<sizeof(marker);i++) marker[i]=(unsigned char)('A'+(i*7+3)%26);
    for(i=0;i+sizeof(marker)<=length;i++)
        if(!memcmp(data+i,marker,sizeof(marker))) { found=1;break; }
    { volatile unsigned char *p=marker;for(i=0;i<sizeof(marker);i++)p[i]=0; }
    return found;
}
int kr_probe_export(const char *path,const char *relative)
{
    FILE *f; unsigned char *data; long length; size_t i,j,n; int ok; char pathhex[1025],line[513];
    if(exported_files>=4096 || !relative[0] || strlen(relative)>512 || relative[0]=='/' || strchr(relative,':') || strchr(relative,'\\') || strstr(relative,"..")) return 0;
    f=fopen(path,"rb"); if(!f) return 0;
    if(fseek(f,0,SEEK_END) || (length=ftell(f))<0 || (size_t)length>512*1024 ||
       (size_t)length>EXPORT_LIMIT-exported_bytes || fseek(f,0,SEEK_SET)) { fclose(f); return 0; }
    n=(size_t)length; data=malloc(n?n:1); if(!data) { fclose(f); return 0; }
    if(fread(data,1,n,f)!=n || fgetc(f)!=EOF || ferror(f)) { free(data); fclose(f); return 0; }
    if(fclose(f)) { free(data); return 0; }
    if(contains_test_secret(data,n) || contains_test_secret(relative,strlen(relative))) {
        volatile unsigned char *p=data;for(i=0;i<n;i++)p[i]=0;
        free(data);
        kr_probe_printf("[KEYRINGEXPORT] result=FAIL reason=plaintext-marker\n");
        return 0;
    }
    for(i=0;relative[i];i++) sprintf(pathhex+2*i,"%02x",(unsigned char)relative[i]);
    ok=kr_probe_printf("[KEYRINGFILE] begin pathhex=%s bytes=%lu\n",pathhex,(unsigned long)n);
    for(i=0;ok && i<n;i+=256) {
        size_t count=n-i<256?n-i:256;
        for(j=0;j<count;j++) sprintf(line+2*j,"%02x",data[i+j]);
        ok=kr_probe_printf("[KEYRINGFILE] data pathhex=%s offset=%lu hex=%s\n",pathhex,(unsigned long)i,line);
    }
    if(ok) ok=kr_probe_printf("[KEYRINGFILE] end pathhex=%s bytes=%lu\n",pathhex,(unsigned long)n);
    { volatile unsigned char *p=data; for(i=0;i<n;i++) p[i]=0; }
    free(data); if(ok) { exported_bytes+=n; ++exported_files; }
    return ok;
}
static int export_directory(const char *source,const char *relative,unsigned depth)
{
    BPTR directory; struct FileInfoBlock fib; int ok=1;
    if(depth>16) return 0;
    directory=Lock(source,ACCESS_READ); if(!directory) return 0;
    if(!Examine(directory,&fib) || fib.fib_DirEntryType<=0) { UnLock(directory); return 0; }
    while(ExNext(directory,&fib)) {
        char child[1024],name[513];
        if(++visited_entries>8192) { ok=0; break; }
        if(!safe_component((const char *)fib.fib_FileName)) { ok=0; break; }
        if(strlen(source)+strlen((const char *)fib.fib_FileName)+2>sizeof(child) || strlen(relative)+strlen((const char *)fib.fib_FileName)+2>sizeof(name)) { ok=0; break; }
        strcpy(child,source); if(!AddPart(child,fib.fib_FileName,sizeof(child))) { ok=0; break; }
        snprintf(name,sizeof(name),"%s/%s",relative,fib.fib_FileName);
        if(fib.fib_DirEntryType==ST_USERDIR) ok=export_directory(child,name,depth+1);
        else if(fib.fib_DirEntryType==ST_FILE) ok=kr_probe_export(child,name);
        else ok=0; /* Link and special-file traversal fails closed. */
        if(!ok) break;
    }
    if(ok && IoErr()!=ERROR_NO_MORE_ENTRIES) ok=0;
    UnLock(directory); return ok;
}
int kr_probe_export_envarc(void)
{
    size_t before=exported_files;
    return export_directory("ENVARC:","ENVARC",0) && exported_files>before;
}
void kr_probe_export_summary(void)
{
    kr_probe_printf("[KEYRINGEXPORT] result=PASS files=%lu bytes=%lu\n",(unsigned long)exported_files,(unsigned long)exported_bytes);
    fflush(stdout);
}
