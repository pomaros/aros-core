/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include "tiles_config.h"
#include "tiles_render.h"
#include "graphics_fixture.h"
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static unsigned width,height,drawings,geometry_cases,parser_negatives;
static uint64_t digest;
static int minx,miny;
static void record(int x,int y,unsigned pen)
{
    assert(x>=0&&y>=0&&(unsigned)x<width&&(unsigned)y<height);
    digest=(digest^((unsigned)x+((unsigned)y<<12)+(pen<<24)))*UINT64_C(1099511628211);
    if(pen){if(x<minx)minx=x;if(y<miny)miny=y;}
}
void SetAPen(struct RastPort *rp,unsigned pen){assert(pen<=3);rp->pen=pen;}
void SetDrMd(struct RastPort *rp,unsigned mode){(void)rp;assert(mode==JAM1);}
void RectFill(struct RastPort *rp,int x,int y,int xx,int yy)
{assert(xx>=x&&yy>=y);record(x,y,rp->pen);record(xx,yy,rp->pen);drawings++;}
void Move(struct RastPort *rp,int x,int y){rp->x=x;rp->y=y;}
void Draw(struct RastPort *rp,int x,int y)
{record(rp->x,rp->y,rp->pen);record(x,y,rp->pen);rp->x=x;rp->y=y;drawings++;}
void TextExtent(struct RastPort *rp,const char *s,size_t n,struct TextExtent *e)
{
    assert(n>0&&n<=strlen(s));e->te_Width=n*rp->Font->width;e->te_Height=rp->Font->height;
    e->te_Extent=(struct Rectangle){rp->Font->bearing,-(rp->Font->height-2),(int)(n*rp->Font->width)+1,1};
}
void Text(struct RastPort *rp,const char *s,size_t n)
{
    struct TextExtent e;TextExtent(rp,s,n,&e);
    record(rp->x+e.te_Extent.MinX,rp->y+e.te_Extent.MinY,rp->pen);
    record(rp->x+e.te_Extent.MaxX,rp->y+e.te_Extent.MaxY,rp->pen);
    for(size_t i=0;i<n;i++)digest=(digest^(unsigned char)s[i])*UINT64_C(1099511628211);
    drawings++;
}
static void reset(unsigned w,unsigned h)
{width=w;height=h;drawings=0;digest=UINT64_C(1469598103934665603);minx=miny=10000;}
static void reject(const char *bad)
{
    struct TileConfig c;char error[80];const char *types[]={"TILE=toggle","GET=NETWORK RADIO",bad,NULL};
    assert(!TileConfigParse(types,&c,error,sizeof(error))&&error[0]&&!c.cellsW&&!c.getPort[0]);parser_negatives++;
}
static void parser_tests(void)
{
    struct TileConfig c;struct TileValue v;char error[80],buffer[600];
    const char *wifi[]={"TILE=toggle","SIZE=1x1","GLYPH=wifi","LABEL=Wi-Fi","GET=NETWORK RADIO","SET=NETWORK RADIO %v","PANEL=network",NULL};
    assert(TileConfigParse(wifi,&c,error,sizeof(error))&&!error[0]);
    assert(c.templateKind==TILE_TOGGLE&&c.cellsW==1&&c.cellsH==1&&c.every==60);
    assert(!strcmp(c.getPort,"NETWORK")&&!strcmp(c.setCommand,"RADIO %v"));
    const char *keys[]={" tile = GaUgE ","SIZE=3X3","ENV=Tiles/Battery","EVERY=1","(GET=ignored)","UNRELATED=ignored",NULL};
    assert(TileConfigParse(keys,&c,error,sizeof(error))&&c.templateKind==TILE_GAUGE&&c.cellsW==3&&c.cellsH==3&&c.every==60);
    const char *badsize[]={"TILE=value","ENV=VALUE","SIZE=5x1",NULL};
    for(unsigned i=0;i<3;i++){
        badsize[2]=i==0?"SIZE=5x1":i==1?"SIZE=0x2":"SIZE=";
        assert(TileConfigParse(badsize,&c,error,sizeof(error))&&c.cellsW==1&&c.cellsH==1&&c.warnings==TILE_CONFIG_WARN_SIZE);
    }
    const char *names[]={"toggle","gauge","slider","value","glyph"};
    for(unsigned i=0;i<5;i++){
        snprintf(buffer,sizeof(buffer),"TILE=%s",names[i]);const char *types[]={buffer,"ENV=state",NULL};
        assert(TileConfigParse(types,&c,error,sizeof(error))&&c.templateKind==(enum TileTemplate)i);
    }
    const struct{const char *key;unsigned capacity;}fields[]={{"LABEL=",64},{"GLYPH=",32},{"PANEL=",128},{"ENV=",128}};
    for(unsigned f=0;f<4;f++){
        size_t prefix=strlen(fields[f].key);memcpy(buffer,fields[f].key,prefix);
        memset(buffer+prefix,'a',fields[f].capacity-1);buffer[prefix+fields[f].capacity-1]=0;
        const char *types[]={"TILE=value",f==3?buffer:"GET=NET QUERY",f==3?NULL:buffer,NULL};
        assert(TileConfigParse(types,&c,error,sizeof(error)));
        buffer[prefix+fields[f].capacity-1]='a';buffer[prefix+fields[f].capacity]=0;
        assert(!TileConfigParse(types,&c,error,sizeof(error)));parser_negatives++;
    }
    for(unsigned n=63;n<=64;n++){
        memcpy(buffer,"GET=",4);memset(buffer+4,'P',n);strcpy(buffer+4+n," QUERY");
        const char *types[]={"TILE=value",buffer,NULL};assert(TileConfigParse(types,&c,error,sizeof(error))==(n==63));if(n==64)parser_negatives++;
    }
    for(unsigned n=255;n<=256;n++){
        memcpy(buffer,"GET=PORT ",9);memset(buffer+9,'Q',n);buffer[9+n]=0;
        const char *types[]={"TILE=value",buffer,NULL};assert(TileConfigParse(types,&c,error,sizeof(error))==(n==255));if(n==256)parser_negatives++;
    }
    const char *timing[]={"TILE=value","GET=NET QUERY","EVERY=0",NULL};
    assert(TileConfigParse(timing,&c,error,sizeof(error))&&c.every==60);
    timing[2]="EVERY=3600";assert(TileConfigParse(timing,&c,error,sizeof(error))&&c.every==3600);
    for(unsigned w=1;w<=4;w++)for(unsigned h=1;h<=4;h++){
        snprintf(buffer,sizeof(buffer),"SIZE=%ux%u",w,h);const char *types[]={"TILE=value","GET=NET QUERY",buffer,NULL};
        assert(TileConfigParse(types,&c,error,sizeof(error))&&c.cellsW==w&&c.cellsH==h&&!c.warnings);
    }
    const char *unknown[]={"TILE=unknown","GET=NETWORK RADIO",NULL};
    assert(!TileConfigParse(unknown,&c,error,sizeof(error)));parser_negatives++;
    const char *missing[]={"LABEL=no template","GET=NETWORK RADIO",NULL};
    assert(!TileConfigParse(missing,&c,error,sizeof(error)));parser_negatives++;
    const char *gets[]={"GET=","GET=NETWORK","GET= NETWORK ","GET=BAD/PORT RADIO","GET=NET RADIO\nINJECT","GET=NET \t"};
    for(unsigned i=0;i<6;i++){const char *types[]={"TILE=toggle",gets[i],NULL};assert(!TileConfigParse(types,&c,error,sizeof(error)));parser_negatives++;}
    const char *bad[]={"TILE=gauge","ENV=another","SET=NET RADIO","SET=NET %v %v","SET=NET %s","SET=NET %","SET=NET %%v","SET=NET","EVERY=-1","EVERY=+60","EVERY=1s","EVERY=184467440737095516160","LABEL=line\nbreak"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)reject(bad[i]);
    memset(buffer,'x',sizeof(buffer));memcpy(buffer,"LABEL=",6);buffer[70]=0;reject(buffer);
    memset(buffer,'x',sizeof(buffer));memcpy(buffer,"GLYPH=",6);buffer[38]=0;reject(buffer);
    memset(buffer,'x',sizeof(buffer));buffer[599]=0;reject(buffer);
    const char *envs[]={"../bad","/absolute","ENV:bad","good/../bad","good//bad","good/"};
    for(unsigned i=0;i<6;i++){snprintf(buffer,sizeof(buffer),"ENV=%s",envs[i]);const char *types[]={"TILE=value",buffer,NULL};assert(!TileConfigParse(types,&c,error,sizeof(error)));parser_negatives++;}
    const char *many[TILE_TOOLTYPE_LIMIT+1];for(unsigned i=0;i<TILE_TOOLTYPE_LIMIT;i++)many[i]="IGNORED=x";many[TILE_TOOLTYPE_LIMIT]=NULL;
    assert(!TileConfigParse(many,&c,error,sizeof(error)));parser_negatives++;
    char tiny[2]={1,2};assert(!TileConfigParse(unknown,&c,tiny,sizeof(tiny))&&tiny[1]==0);
    assert(!TileConfigParse(NULL,&c,NULL,0));assert(!TileConfigParse(wifi,NULL,error,sizeof(error)));
    assert(TileValueParse(TILE_TOGGLE," ON ",&v)&&v.boolean==1);assert(TileValueParse(TILE_TOGGLE,"0",&v)&&v.boolean==0);
    assert(!TileValueParse(TILE_TOGGLE,"enabled",&v)&&!v.text[0]);assert(TileValueParse(TILE_GAUGE,"100",&v)&&v.number==100);
    assert(TileValueParse(TILE_SLIDER,"0",&v)&&v.number==0);
    const char *nums[]={"-1","101","5%","+1","1.0","999999999999999999999999999"};
    for(unsigned i=0;i<6;i++)assert(!TileValueParse(TILE_GAUGE,nums[i],&v));
    assert(TileValueParse(TILE_VALUE,"Home network",&v));assert(TileValueParse(TILE_GLYPH,"connected",&v));
    assert(!TileValueParse(TILE_VALUE,"hello\nworld",&v));assert(!TileValueParse(TILE_GLYPH,"",&v));
    memset(buffer,'a',255);buffer[255]=0;assert(TileValueParse(TILE_VALUE,buffer,&v));buffer[255]='a';buffer[256]=0;assert(!TileValueParse(TILE_VALUE,buffer,&v));
    unsigned random=7;
    for(unsigned i=0;i<10000;i++){
        size_t n=i%sizeof(buffer);for(size_t j=0;j<n;j++){random=random*1664525u+1013904223u;buffer[j]=(char)(1+(random>>24)%126);}buffer[n]=0;
        const char *types[]={"TILE=value",buffer,NULL};TileConfigParse(types,&c,error,sizeof(error));TileValueParse(TILE_VALUE,buffer,&v);
    }
}
static void render_tests(void)
{
    struct TextFont font={6,8,-2};struct RastPort rp={.Font=&font};
    struct TileConfig c={.cellsW=1,.cellsH=1};struct TileValue v;
    strcpy(c.label,"A deliberately long label, clipped to its rectangle");strcpy(c.glyph,"wifi");
    for(unsigned kind=0;kind<5;kind++)for(unsigned cw=1;cw<=4;cw++)for(unsigned ch=1;ch<=4;ch++)for(unsigned sample=0;sample<3;sample++)for(unsigned fk=0;fk<2;fk++){
        c.templateKind=kind;c.cellsW=cw;c.cellsH=ch;
        const char *s=kind==TILE_TOGGLE?(sample?"on":"off"):((kind==TILE_GAUGE||kind==TILE_SLIDER)?(sample==0?"0":sample==1?"50":"100"):"A long returned value clipped to the available space");
        assert(TileValueParse(kind,s,&v));font.width=fk?14:6;font.height=fk?20:8;reset(cw*64,ch*64);
        assert(TileRender(&rp,&c,&v,width,height)&&drawings);geometry_cases++;
        assert(minx==(int)((width-(cw>=2?128:64))/2));assert(miny==(int)((height-(cw>=2&&ch>=2?128:64))/2));
    }
    c.templateKind=TILE_TOGGLE;c.cellsW=c.cellsH=1;font.width=6;font.height=8;
    assert(TileValueParse(TILE_TOGGLE,"on",&v));reset(64,64);assert(TileRender(&rp,&c,&v,width,height));uint64_t on=digest;
    assert(TileValueParse(TILE_TOGGLE,"off",&v));reset(64,64);assert(TileRender(&rp,&c,&v,width,height)&&digest!=on);
    reset(24,16);assert(TileRender(&rp,&c,&v,width,height));
    for(unsigned kind=0;kind<5;kind++)for(unsigned w=12;w<40;w++)for(unsigned h=12;h<40;h++){
        c.templateKind=kind;assert(TileValueParse(kind,kind==TILE_TOGGLE?"on":"50",&v));reset(w,h);assert(TileRender(&rp,&c,&v,w,h));
    }
    reset(11,16);assert(!TileRender(&rp,&c,&v,width,height)&&!drawings);
    c.cellsW=5;reset(64,64);assert(!TileRender(&rp,&c,&v,width,height)&&!drawings);
    c.cellsW=1;c.templateKind=TILE_GAUGE;v.number=101;assert(!TileRender(&rp,&c,&v,width,height));
    c.templateKind=TILE_VALUE;memset(v.text,'x',sizeof(v.text));assert(!TileRender(&rp,&c,&v,width,height));
    v.text[0]=0;rp.Font=NULL;assert(!TileRender(&rp,&c,&v,width,height));
}
static void hover_weather_tests(void)
{
    struct TileConfig c; struct TileValue v; char error[80];
    const char *eyesTypes[]={"TILE=eyes","HOVER=FAST",NULL};
    assert(TileConfigParse(eyesTypes,&c,error,sizeof(error)) && c.pointerEnabled && c.hoverCadence==33);
    eyesTypes[1]="HOVER=ON";
    assert(TileConfigParse(eyesTypes,&c,error,sizeof(error)) && c.pointerEnabled && !c.hoverCadence);
    eyesTypes[1]=NULL;
    assert(TileConfigParse(eyesTypes,&c,error,sizeof(error)) && !c.pointerEnabled && !c.hoverCadence);
    eyesTypes[1]="GET=X Y";assert(!TileConfigParse(eyesTypes,&c,error,sizeof(error)));
    eyesTypes[1]="ENV=weather";assert(!TileConfigParse(eyesTypes,&c,error,sizeof(error)));
    reject("HOVER=32"); reject("HOVER=yes");
    const char *weather[]={"TILE=weather","ENV=TilesWeatherDemo","LABEL=Rome (offline)",NULL};
    assert(TileConfigParse(weather,&c,error,sizeof(error)));
    const char *bad[]={"101|sunny","-101|rain","18.5|cloudy","18|unknown","18|rain|snow","|sunny","18|","18 |rain","18| rain"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!TileValueParse(TILE_WEATHER,bad[i],&v));
    const char *conditions[]={"sunny","cloudy","rain","snow","storm","fog"};
    struct TextFont font={6,8,-2};struct RastPort rp={.Font=&font};
    for(unsigned i=0;i<6;i++)for(int n=-100;n<=100;n+=100){
        char value[40];snprintf(value,sizeof(value),"%d|%s",n,conditions[i]);
        assert(TileValueParse(TILE_WEATHER,value,&v)&&v.number==n&&!strcmp(v.text,conditions[i]));
        for(unsigned w=12;w<=128;w+=4)for(unsigned h=12;h<=128;h+=4){reset(w,h);assert(TileRender(&rp,&c,&v,w,h));}
    }
    c.templateKind=TILE_EYES;assert(TileValueParse(TILE_EYES,"",&v));assert(!TileValueParse(TILE_EYES,"remote",&v));
    const long coords[]={LONG_MIN,-1,0,32,64,LONG_MAX};
    for(unsigned w=12;w<=128;w+=4)for(unsigned h=12;h<=128;h+=4)for(unsigned i=0;i<6;i++){
        v.pointerX=coords[i];v.pointerY=coords[5-i];v.hovered=1;reset(w,h);assert(TileRender(&rp,&c,&v,w,h));
    }
    v.pointerX=0;v.pointerY=0;v.hovered=1;reset(64,64);assert(TileRender(&rp,&c,&v,64,64));uint64_t left=digest;
    v.pointerX=63;v.pointerY=63;reset(64,64);assert(TileRender(&rp,&c,&v,64,64)&&digest!=left);
    v.hovered=0;reset(64,64);assert(TileRender(&rp,&c,&v,64,64));uint64_t neutral=digest;
    v.pointerX=LONG_MIN;v.pointerY=LONG_MAX;reset(64,64);assert(TileRender(&rp,&c,&v,64,64)&&digest==neutral);
}
int main(void)
{
    parser_tests();render_tests();hover_weather_tests();
    printf("{\"result\":\"PASS\",\"parser_negatives\":%u,\"geometry_cases\":%u,\"fuzz_inputs\":10000,\"small_surface_cases\":3920,\"hover_weather_checked\":true,\"weather_surface_cases\":16200,\"eyes_surface_cases\":5400,\"text_bearings_checked\":true}\n",parser_negatives,geometry_cases);
    return 0;
}
