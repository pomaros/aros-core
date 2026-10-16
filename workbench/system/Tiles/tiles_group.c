/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include "tiles_group.h"
#include <string.h>
#include <stdio.h>
static int eq(const char *a,const char *b) {
    for (;; ++a,++b) { unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;
        if(x!=y)return 0;if(!x)return 1; }
}
static char *trim(char *s) {
    char *end;while(*s==' '||*s=='\t')++s;end=s+strlen(s);
    while(end>s&&(end[-1]==' '||end[-1]=='\t'))--end;*end=0;return s;
}
static int fail(struct TileGroupConfig *c,char *e,size_t n,const char *why) {
    if(c)memset(c,0,sizeof(*c));if(e&&n)snprintf(e,n,"%s",why);return 0;
}
int TileGroupConfigParse(const char *const *types,const char *name,
    struct TileGroupConfig *config,char *error,size_t errorSize) {
    static const char *keys[]={"TILE","ANCHOR","SHOW","LIFT","COLUMNS","TITLE"};
    static const char *anchors[]={"free","top","bottom","left","right"};
    static const char *shows[]={"always","lifted","edge"};
    struct TileGroupConfig c;unsigned seen=0,i;
    if(error&&errorSize)*error=0;
    if(!config||!types)return fail(config,error,errorSize,"missing group ToolTypes");
    memset(&c,0,sizeof(c));c.columns=4;strcpy(c.lift,name&&eq(name,"Control")?"LAmiga C":"none");
    for(i=0;i<128&&types[i];++i) {
        char line[512],*key,*value,*sep;size_t n=0;unsigned k,j;
        while(n<sizeof(line)&&types[i][n])++n;
        if(n==sizeof(line))return fail(config,error,errorSize,"ToolType too long");
        memcpy(line,types[i],n+1);key=trim(line);sep=strchr(key,'=');
        if(sep){*sep=0;value=trim(sep+1);}else value=NULL;
        key=trim(key);for(k=0;k<6&&!eq(key,keys[k]);++k){}
        if(k==6)continue;
        if(!value||(seen&(1u<<k)))return fail(config,error,errorSize,"missing or duplicate group value");
        seen|=1u<<k;
        for(j=0;value[j];++j)if((unsigned char)value[j]<32||(unsigned char)value[j]==127)
            return fail(config,error,errorSize,"control byte in group value");
        switch(k) {
        case 0:if(!eq(value,"group"))return fail(config,error,errorSize,"not a group");break;
        case 1:for(j=0;j<5&&!eq(value,anchors[j]);++j){}if(j==5)return fail(config,error,errorSize,"invalid anchor");c.anchor=(enum TileGroupAnchor)j;break;
        case 2:for(j=0;j<3&&!eq(value,shows[j]);++j){}if(j==3)return fail(config,error,errorSize,"invalid show");c.show=(enum TileGroupShow)j;break;
        case 3:if(!*value||strlen(value)>=sizeof(c.lift))return fail(config,error,errorSize,"invalid lift");strcpy(c.lift,value);break;
        case 4:{unsigned v=0;if(!*value)return fail(config,error,errorSize,"invalid columns");
            for(j=0;value[j];++j){unsigned d=(unsigned char)value[j]-'0';if(d>9||v>TILE_GROUP_MAX_COLUMNS/10)return fail(config,error,errorSize,"invalid columns");v=v*10+d;}
            if(!v||v>TILE_GROUP_MAX_COLUMNS)return fail(config,error,errorSize,"invalid columns");c.columns=v;break;}
        case 5:if(strlen(value)>=sizeof(c.title))return fail(config,error,errorSize,"title too long");strcpy(c.title,value);break;
        }
    }
    if(i==128&&types[i])return fail(config,error,errorSize,"too many ToolTypes");
    if(!(seen&1))return fail(config,error,errorSize,"TILE=group required");
    if(c.show==TILE_GROUP_EDGE&&c.anchor==TILE_GROUP_FREE)return fail(config,error,errorSize,"edge show needs anchor");
    *config=c;return 1;
}
static int valid(const struct TileGroupConfig *c) {
    return c&&c->columns&&c->columns<=TILE_GROUP_MAX_COLUMNS&&
        c->anchor>=TILE_GROUP_FREE&&c->anchor<=TILE_GROUP_RIGHT&&
        c->show>=TILE_GROUP_ALWAYS&&c->show<=TILE_GROUP_EDGE;
}
int TileGroupArrange(const struct TileGroupConfig *c,const struct TileGroupMember *m,
    unsigned count,unsigned cw,unsigned ch,struct TileGroupLayout *out) {
    int32_t x[TILE_GROUP_MAX_MEMBERS]={0},y[TILE_GROUP_MAX_MEMBERS]={0},minx=32767,miny=32767,maxx=-32767,maxy=-32767;
    unsigned char placed[TILE_GROUP_MAX_MEMBERS]={0};unsigned i,j;
    if(!out)return 0;memset(out,0,sizeof(*out));
    if(!valid(c)||!m||!count||count>TILE_GROUP_MAX_MEMBERS||!cw||!ch||cw>32767||ch>32767)return 0;
    for(i=0;i<count;++i) {
        if(m[i].isGroup||!m[i].width||!m[i].height||m[i].width>32767||m[i].height>32767)return 0;
        if(m[i].explicitPosition) {
            if(m[i].x < -32767||m[i].x>32767||m[i].y < -32767||m[i].y>32767)return 0;
            x[i]=m[i].x;y[i]=m[i].y;placed[i]=1;
        }
    }
    for(i=0;i<count;++i)if(!placed[i]) {
        unsigned row,col;int found=0;
        for(row=0;row<256&&!found;++row)for(col=0;col<c->columns&&!found;++col) {
            int overlap=0;int32_t px=(int32_t)(col*cw),py=(int32_t)(row*ch);
            if((uint64_t)col*cw+m[i].width>(uint64_t)c->columns*cw||px>32767||py>32767)continue;
            for(j=0;j<count;++j)if(placed[j]&&px<x[j]+(int32_t)m[j].width&&px+(int32_t)m[i].width>x[j]&&py<y[j]+(int32_t)m[j].height&&py+(int32_t)m[i].height>y[j]){overlap=1;break;}
            if(!overlap){x[i]=px;y[i]=py;placed[i]=1;found=1;}
        }
        if(!found)return 0;
    }
    for(i=0;i<count;++i) {
        if(x[i]<minx)minx=x[i];if(y[i]<miny)miny=y[i];
        if(x[i]+(int32_t)m[i].width>maxx)maxx=x[i]+(int32_t)m[i].width;
        if(y[i]+(int32_t)m[i].height>maxy)maxy=y[i]+(int32_t)m[i].height;
    }
    if(maxx-minx>32767||maxy-miny>32767)return 0;
    out->minX=minx;out->minY=miny;out->width=(unsigned)(maxx-minx);out->height=(unsigned)(maxy-miny);out->count=count;
    for(i=0;i<count;++i){out->members[i].x=x[i]-minx;out->members[i].y=y[i]-miny;}return 1;
}
int TileGroupPlace(const struct TileGroupConfig *c,const struct TileGroupLayout *g,
    unsigned sw,unsigned sh,int32_t px,int32_t py,int lifted,int pointer,
    int32_t mx,int32_t my,int32_t *x,int32_t *y) {
    int32_t xmax,ymax;if(x)*x=0;if(y)*y=0;
    if(!x||!y||!valid(c)||!g||!g->width||!g->height||sw>32767||sh>32767||g->width>sw||g->height>sh)return 0;
    xmax=(int32_t)(sw-g->width);ymax=(int32_t)(sh-g->height);
    if(lifted&&c->show==TILE_GROUP_LIFTED){px=pointer?mx:xmax/2;py=pointer?my:ymax/2;}
    if(c->anchor==TILE_GROUP_LEFT)px=0;if(c->anchor==TILE_GROUP_RIGHT)px=xmax;
    if(c->anchor==TILE_GROUP_TOP)py=0;if(c->anchor==TILE_GROUP_BOTTOM)py=ymax;
    *x=px<0?0:px>xmax?xmax:px;*y=py<0?0:py>ymax?ymax:py;return 1;
}
