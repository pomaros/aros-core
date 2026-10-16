/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL;typedef intptr_t BPTR;typedef int32_t LONG;typedef uint16_t UWORD;typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define ACCESS_READ 0
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_DIR_NOT_FOUND 204
#define DOS_FIB 1
#define RETURN_OK 0
#define RETURN_ERROR 10
#define WBDRAWER 2
#define WBTOOL 3
#define GFLG_GADGIMAGE 4
#define NO_ICON_POSITION -1
struct FileInfoBlock{LONG fib_DirEntryType;};
struct Image{int Width,Height,Depth,PlanePick;UWORD *ImageData;};
struct DrawerData{struct{int Width,Height;}dd_NewWindow;};
struct DiskObject{int do_Magic,do_Version,do_Type;LONG do_CurrentX,do_CurrentY;STRPTR *do_ToolTypes;struct DrawerData *do_DrawerData;struct{int Width,Height,Flags;void *GadgetRender;}do_Gadget;};
static unsigned contract_mask;
static int error,write_count,read_count,fail_write;static char deny[512];
struct Entry{char path[512];int directory;};static struct Entry files[64];static int count;
static int lookup(const char *p){for(int i=0;i<count;i++)if(!strcmp(files[i].path,p))return i;return -1;}
static void add(const char *p,int dir){assert(count<64&&lookup(p)<0);strcpy(files[count].path,p);files[count++].directory=dir;}
static LONG IoErr(void){return error;}
static BPTR Lock(const char *p,int mode){(void)mode;if(!strcmp(p,deny)){error=202;return 0;}int n=lookup(p);error=n<0?ERROR_OBJECT_NOT_FOUND:0;return n+1;}
static void UnLock(BPTR p){assert(p);}
static BPTR CreateDir(const char *p){add(p,1);return count;}
static void *AllocDosObject(int type,void *tags){(void)type;(void)tags;return calloc(1,sizeof(struct FileInfoBlock));}
static void FreeDosObject(int t,void *p){(void)t;free(p);}
static BOOL Examine(BPTR lock,struct FileInfoBlock *f){f->fib_DirEntryType=files[lock-1].directory?1:-1;return TRUE;}
static BOOL AddPart(char *p,STRPTR n,size_t capacity){size_t a=strlen(p),b=strlen(n);int slash=a&&p[a-1]!=':'&&p[a-1]!='/';if(a+b+slash>=capacity)return FALSE;if(slash)strcat(p,"/");strcat(p,n);return TRUE;}
static struct DiskObject *GetDiskObject(STRPTR p){(void)p;read_count++;return calloc(1,sizeof(struct DiskObject));}
static void FreeDiskObject(struct DiskObject *p){free(p);}
static BOOL PutDiskObject(STRPTR p,struct DiskObject *o){char f[512];assert(strlen(p)<506);snprintf(f,sizeof(f),"%s.info",p);assert(lookup(f)<0);write_count++;if(fail_write){error=221;return FALSE;}if(o->do_Magic){assert(o->do_ToolTypes);if(o->do_Type==WBDRAWER)assert(!strcmp(o->do_ToolTypes[0],"TILE=group"));else if(!strncmp(p,"CHECK:Tiles/Control/",19)) {
const char *names[]={"network","bluetooth","sound","battery","brightness","dnd","clock"};
const char *gets[]={"GET=INDICATOR.network GET RADIO","GET=INDICATOR.bluetooth GET RADIO","GET=INDICATOR.sound GET LEVEL","GET=INDICATOR.battery GET PERCENT","GET=INDICATOR.brightness GET LEVEL","GET=INDICATOR.notifications GET DND","GET=INDICATOR.clock GET TIME"};
const char *sets[]={"SET=INDICATOR.network SET RADIO %v","SET=INDICATOR.bluetooth SET RADIO %v","SET=INDICATOR.sound SET LEVEL %v",NULL,"SET=INDICATOR.brightness SET LEVEL %v","SET=INDICATOR.notifications SET DND %v",NULL};
for(unsigned n=0;n<7;n++)if(strstr(p,names[n])){int get=0,set=0;for(STRPTR *t=o->do_ToolTypes;*t;t++){get+=!strcmp(*t,gets[n]);if(!strncmp(*t,"SET=",4)){assert(sets[n]&&!strcmp(*t,sets[n]));set++;}}assert(get==1&&set==(n!=3&&n!=6));contract_mask|=1u<<n;}
}}add(f,0);return TRUE;}
