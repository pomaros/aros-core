/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include "tiles_group.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static void reject(const char *value) {
    const char *tt[]={"TILE=group",value,NULL};struct TileGroupConfig c;char e[80];
    memset(&c,255,sizeof(c));assert(!TileGroupConfigParse(tt,"Control",&c,e,sizeof(e)));
    struct TileGroupConfig zero={0};assert(!memcmp(&c,&zero,sizeof(c))&&e[0]);
}
int main(void) {
    struct TileGroupConfig c;char e[80];struct TileGroupLayout g;int32_t x,y;
    const char *plain[]={"TILE=group",NULL};
    assert(TileGroupConfigParse(plain,"Control",&c,e,sizeof(e))&&c.columns==4&&!strcmp(c.lift,"LAmiga C"));
    assert(TileGroupConfigParse(plain,"Other",&c,e,sizeof(e))&&!strcmp(c.lift,"none"));
    const char *full[]={" tile = GROUP ","ANCHOR=bottom","SHOW=edge","COLUMNS=2","TITLE=Shelf","LIFT=none","UNRELATED=ignored",NULL};
    assert(TileGroupConfigParse(full,"Shelf",&c,e,sizeof(e))&&c.anchor==TILE_GROUP_BOTTOM&&c.show==TILE_GROUP_EDGE&&c.columns==2&&!strcmp(c.title,"Shelf"));
    reject("TILE=group");reject("ANCHOR=diagonal");reject("SHOW=edge");reject("SHOW=unknown");reject("COLUMNS=0");reject("COLUMNS=33");reject("COLUMNS=429496729600000");reject("COLUMNS=-1");reject("COLUMNS=1x");reject("TITLE=bad\nvalue");reject("LIFT=");reject("ANCHOR");
    const char *ordinary[]={"TITLE=ordinary",NULL};assert(!TileGroupConfigParse(ordinary,"Drawer",&c,e,sizeof(e)));
    char huge[600];memset(huge,'a',sizeof(huge));memcpy(huge,"TITLE=",6);huge[599]=0;reject(huge);huge[80]=0;reject(huge);
    const char *many[130];for(unsigned i=0;i<129;++i)many[i]="OTHER=x";many[129]=NULL;assert(!TileGroupConfigParse(many,"Control",&c,e,sizeof(e)));
    assert(TileGroupConfigParse(plain,"Control",&c,e,sizeof(e)));
    struct TileGroupMember m[3]={{-20,10,64,64,1,0},{108,10,128,128,1,0},{0,0,64,64,0,0}};
    assert(TileGroupArrange(&c,m,3,64,64,&g));
    assert(g.minX==-20&&g.minY==10&&g.width==256&&g.height==182);
    assert(g.members[0].x==0&&g.members[0].y==0&&g.members[1].x==128&&g.members[2].x==20&&g.members[2].y==118);
    assert(m[0].x==-20&&m[1].x==108); /* no input mutation */
    c.anchor=TILE_GROUP_BOTTOM;
    assert(TileGroupPlace(&c,&g,800,600,900,-20,0,0,0,0,&x,&y)&&x==544&&y==418);
    assert(TileGroupPlace(&c,&g,640,480,900,-20,0,0,0,0,&x,&y)&&x==384&&y==298);
    c.anchor=TILE_GROUP_TOP;
    assert(TileGroupPlace(&c,&g,800,600,100,99,0,0,0,0,&x,&y)&&x==100&&y==0);
    c.anchor=TILE_GROUP_LEFT;
    assert(TileGroupPlace(&c,&g,800,600,100,99,0,0,0,0,&x,&y)&&x==0&&y==99);
    c.anchor=TILE_GROUP_RIGHT;
    assert(TileGroupPlace(&c,&g,800,600,100,99,0,0,0,0,&x,&y)&&x==544&&y==99);
    c.anchor=TILE_GROUP_FREE;c.show=TILE_GROUP_LIFTED;
    assert(TileGroupPlace(&c,&g,800,600,0,0,1,1,790,590,&x,&y)&&x==544&&y==418);
    assert(TileGroupPlace(&c,&g,800,600,0,0,1,0,0,0,&x,&y)&&x==272&&y==209);
    assert(TileGroupPlace(&c,&g,800,600,INT_MIN,INT_MAX,0,0,0,0,&x,&y)&&x==0&&y==418);
    assert(!TileGroupPlace(&c,&g,128,128,0,0,0,0,0,0,&x,&y)&&!x&&!y);
    m[0].isGroup=1;assert(!TileGroupArrange(&c,m,3,64,64,&g)&&!g.count);m[0].isGroup=0;
    m[0].x=INT_MIN;assert(!TileGroupArrange(&c,m,3,64,64,&g));m[0].x=-32767;
    m[1].x=32767;assert(!TileGroupArrange(&c,m,3,64,64,&g));
    assert(!TileGroupArrange(&c,m,41,64,64,&g));
    assert(!TileGroupArrange(&c,m,0,64,64,&g));
    assert(!TileGroupArrange(&c,m,1,0,64,&g));
    m[0]=(struct TileGroupMember){0,0,128,64,0,0};c.columns=1;
    assert(!TileGroupArrange(&c,m,1,64,64,&g)); /* cannot fit columns */
    c.columns=2;m[1]=(struct TileGroupMember){0,0,64,64,0,0};
    assert(TileGroupArrange(&c,m,2,64,64,&g)&&g.width==128&&g.height==128&&g.members[1].y==64);
    m[0].explicitPosition=m[1].explicitPosition=1;m[0].x=m[1].x=7;m[0].y=m[1].y=9;
    assert(TileGroupArrange(&c,m,2,64,64,&g)&&g.members[0].x==g.members[1].x); /* explicit overlap preserved */
    puts("tiles-group-host PASS");
}
