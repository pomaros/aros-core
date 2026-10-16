/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <intuition/intuition.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <stdio.h>
#include <string.h>
#define PATH_CAP 512
/* -1 error, 0 absent, 1 present; absence is never inferred from access errors. */
static int exists(const char *path)
{
    BPTR lock=Lock(path,ACCESS_READ);
    if(lock){UnLock(lock);return 1;}
    LONG error=IoErr();
    return error==ERROR_OBJECT_NOT_FOUND || error==ERROR_DIR_NOT_FOUND ? 0 : -1;
}
static BOOL drawer(const char *path)
{
    BPTR lock=Lock(path,ACCESS_READ);
    if(!lock){if(IoErr()!=ERROR_OBJECT_NOT_FOUND)return FALSE;lock=CreateDir(path);if(!lock)return FALSE;}
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    BOOL ok=fib && Examine(lock,fib) && fib->fib_DirEntryType>0;
    if(fib)FreeDosObject(DOS_FIB,fib);UnLock(lock);return ok;
}
static BOOL pathjoin(char *out,const char *root,const char *name)
{
    if(strlen(root)>=PATH_CAP)return FALSE;
    strcpy(out,root);return AddPart(out,(STRPTR)name,PATH_CAP);
}
static BOOL save_icon(const char *path,const char *seed,STRPTR *types,BOOL group,LONG x,LONG y)
{
    char file[PATH_CAP];
    if(snprintf(file,sizeof(file),"%s.info",path)>=(int)sizeof(file))return FALSE;
    int present=exists(file);
    if(present)return present>0; /* Never overwrite existing customization. */
    struct DiskObject *imported=NULL;
    if(strcmp(path,seed)) {
        char source[PATH_CAP];
        if(snprintf(source,sizeof(source),"%s.info",seed)>=(int)sizeof(source))return FALSE;
        int available=exists(source);
        if(available<0)return FALSE;
        if(available) {imported=GetDiskObject((STRPTR)seed);if(!imported)return FALSE;}
    }
    if(imported){BOOL ok=PutDiskObject((STRPTR)path,imported);FreeDiskObject(imported);return ok;}
    struct DiskObject icon={0};struct Image image={0};struct DrawerData data={0};UWORD bits[64];
    icon.do_Magic=0xe310;icon.do_Version=1;icon.do_Type=group?WBDRAWER:WBTOOL;
    icon.do_CurrentX=x;icon.do_CurrentY=y;icon.do_ToolTypes=types;
    if(group){icon.do_DrawerData=&data;data.dd_NewWindow.Width=320;data.dd_NewWindow.Height=200;}
    icon.do_Gadget.Width=icon.do_Gadget.Height=32;
    icon.do_Gadget.Flags=GFLG_GADGIMAGE;icon.do_Gadget.GadgetRender=&image;
    image.Width=image.Height=32;image.Depth=1;image.PlanePick=1;image.ImageData=bits;
    for(unsigned n=0;n<32;++n)bits[n*2]=bits[n*2+1]=(n/8)&1?0x00ff:0xff00;
    return PutDiskObject((STRPTR)path,&icon);
}
int main(int argc,char **argv)
{
    const char *root=argc>1?argv[1]:"HOME:Tiles";
    char control[PATH_CAP],path[PATH_CAP],seed[PATH_CAP];
    STRPTR group[]={"TILE=group","SHOW=lifted","COLUMNS=2","TITLE=Control","LIFT=LAmiga C",NULL};
    STRPTR network[]={"TILE=toggle","LABEL=Network","GLYPH=wifi","GET=INDICATOR.network GET RADIO","SET=INDICATOR.network SET RADIO %v",NULL};
    STRPTR bluetooth[]={"TILE=toggle","LABEL=Bluetooth","GET=INDICATOR.bluetooth GET RADIO","SET=INDICATOR.bluetooth SET RADIO %v",NULL};
    STRPTR sound[]={"TILE=slider","LABEL=Volume","GLYPH=sound","GET=INDICATOR.sound GET LEVEL","SET=INDICATOR.sound SET LEVEL %v",NULL};
    STRPTR battery[]={"TILE=gauge","LABEL=Battery","GET=INDICATOR.battery GET PERCENT",NULL};
    STRPTR brightness[]={"TILE=slider","LABEL=Brightness","GET=INDICATOR.brightness GET LEVEL","SET=INDICATOR.brightness SET LEVEL %v",NULL};
    STRPTR dnd[]={"TILE=toggle","LABEL=Do not disturb","GET=INDICATOR.notifications GET DND","SET=INDICATOR.notifications SET DND %v",NULL};
    STRPTR clock[]={"TILE=value","LABEL=Clock","GET=INDICATOR.clock GET TIME","EVERY=60",NULL};
    STRPTR *definitions[]={network,bluetooth,sound,battery,brightness,dnd,clock};
    const char *names[]={"network","bluetooth","sound","battery","brightness","dnd","clock"};
    if(argc>2){fprintf(stderr,"Usage: TilesControl [root drawer, default HOME:Tiles]\n");return RETURN_ERROR;}
    if(!drawer(root)||!pathjoin(control,root,"Control")||!drawer(control))goto failed;
    if(!save_icon(control,"SYS:Tiles/Control",group,TRUE,NO_ICON_POSITION,NO_ICON_POSITION))goto failed;
    for(unsigned n=0;n<7;++n){
        if(!pathjoin(path,control,names[n])||!pathjoin(seed,"SYS:Tiles/Control",names[n])||
           !save_icon(path,seed,definitions[n],FALSE,(n%2)*64,(n/2)*64))goto failed;
    }
    printf("Control defaults available in %s; existing files preserved.\n",control);
    puts("Seven definitions installed. Brightness and DND stay unavailable until their real providers are running; clock uses TilesControlSource.");
    return RETURN_OK;
failed:
    fprintf(stderr,"TilesControl: provisioning stopped (DOS error %ld). Existing files preserved; completed new entries remain for retry.\n",(long)IoErr());
    return RETURN_ERROR;
}
