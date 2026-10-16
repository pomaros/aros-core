/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef int BOOL;typedef unsigned long ULONG;
#define TRUE 1
#define FALSE 0
#define MEMF_CLEAR 1
#define MEMF_PUBLIC 2
struct Tile {char definitionPath[512];int fresh;};
struct AppIcon {struct AppIcon *next;struct Tile *ai_Tile;};
static struct {struct AppIcon *wb_AppIcons;} registry,*WorkbenchBase=&registry;
static int locks,allocated,rendered,resolves;
#define LockWorkbench() do{assert(!locks);++locks;}while(0)
#define UnlockWorkbench() do{assert(locks==1);--locks;}while(0)
#define LB(base) (base)
#define ForeachNode(list,node) for(node=*(list);node;node=node->next)
static int Stricmp(const char*a,const char*b){assert(locks==1);return strcasecmp(a,b);}
static void *AllocVec(size_t n,int flags){(void)flags;++allocated;return calloc(1,n);}
static void FreeVec(void*p){assert(p&&allocated>0);--allocated;free(p);}
static int NameFromLock(int lock,char*out,int n){assert(!locks&&lock==1&&n>40);++resolves;strcpy(out,"RAM Disk:TileHome/Tiles/Alpha.info");return 1;}
static void UnLock(int lock){assert(!locks&&lock==1);}
#include "adapter.inc"
static void parse_once(void)
{
    for(int iteration=0;iteration<1;++iteration) {
        char *bdrp_fullfile=AllocVec(64,0);strcpy(bdrp_fullfile,"HOME:Tiles/Alpha.info");
        int lofLock=1;BOOL isonlyicon=TRUE;
#include "parse.inc"
        assert(!strcmp(bdrp_fullfile,"RAM Disk:TileHome/Tiles/Alpha"));
        ++rendered;FreeVec(bdrp_fullfile);
    }
    assert(!locks&&!allocated);
}
int main(void)
{
    struct Tile tile={{0},1};strcpy(tile.definitionPath,"ram disk:tilehome/tiles/ALPHA");
    struct AppIcon plain={NULL,NULL},live={&plain,&tile};registry.wb_AppIcons=&plain;
    parse_once();assert(rendered==1); /* initial ordinary left-out */
    registry.wb_AppIcons=&live;parse_once();assert(rendered==1); /* registered live tile replaces it */
    tile.fresh=0;parse_once();assert(rendered==1); /* fallback still owns presentation */
    registry.wb_AppIcons=&plain;parse_once();assert(rendered==2); /* removal restores disk icon */
    strcpy(tile.definitionPath,"RAM Disk:TileHome/Tiles/Alphabet");registry.wb_AppIcons=&live;parse_once();assert(rendered==3);
    tile.definitionPath[0]=0;parse_once();assert(rendered==4&&resolves==6);
    assert(!AppIcon_TileDefinitionRegistered(NULL));
    puts("tiles-backdrop-transition PASS");
}
