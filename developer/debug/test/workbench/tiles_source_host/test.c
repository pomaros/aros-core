/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
static unsigned successes, failures;
static APTR lastOwner;
static void result(APTR owner,BOOL setting,BOOL success,CONST_STRPTR text,APTR context)
{ (void)setting;(void)context;lastOwner=owner;if(success){++successes;assert(!strcmp(text,"on"));}else ++failures; }
static void reply(struct TileSources *sources,struct RexxMsg *message)
{ message->rm_Result1=0;message->rm_Result2=(intptr_t)CreateArgstring("on",2);PutMsg(sources->replies,(struct Message *)message); }
int main(void)
{
    struct TileSources sources;
    struct RexxMsg *blocked,*healthy;
    char buffer[32];int owners[TILE_SOURCE_LIMIT+1];unsigned i;
    assert(TileSourcesInit(&sources));
    portPresent=FALSE;
    assert(!TileSourceSend(&sources,&owners[0],FALSE,"MISSING","GET",0));
    assert(!liveMessages && !liveStrings);
    portPresent=TRUE;
    assert(TileSourceSend(&sources,&owners[0],FALSE,"SOURCE","GET",100));
    blocked=(struct RexxMsg *)GetMsg(&destination);
    assert(!TileSourceSend(&sources,&owners[0],FALSE,"SOURCE","GET",101));
    assert(TileSourceSend(&sources,&owners[1],FALSE,"HEALTHY","GET",200));
    healthy=(struct RexxMsg *)GetMsg(&destination);
    reply(&sources,healthy);TileSourcesCollect(&sources,result,NULL);
    assert(successes==1 && lastOwner==&owners[1]);
    TileSourcesExpire(&sources,1099,result,NULL);assert(!failures);
    TileSourcesExpire(&sources,1100,result,NULL);assert(failures==1);
    assert(liveMessages==1 && liveStrings==1 && !TileSourcesIdle(&sources));
    TileSourcesExpire(&sources,50000,result,NULL);assert(failures==1);
    /* An unavailable source cannot block an unrelated tile after timeout. */
    assert(TileSourceSend(&sources,&owners[1],FALSE,"HEALTHY","GET",1200));
    reply(&sources,(struct RexxMsg *)GetMsg(&destination));TileSourcesCollect(&sources,result,NULL);
    assert(successes==2 && liveMessages==1);
    /* Late replies reclaim resources but never overwrite newer UI state. */
    reply(&sources,blocked);TileSourcesCollect(&sources,result,NULL);
    assert(successes==2 && failures==1 && !liveMessages && !liveStrings);
    for(i=0;i<TILE_SOURCE_LIMIT-1;++i)
        assert(TileSourceSend(&sources,&owners[i],FALSE,"SILENT","GET",2000));
    TileSourcesExpire(&sources,3000,result,NULL);
    assert(TileSourceSend(&sources,&owners[TILE_SOURCE_LIMIT-1],FALSE,"HEALTHY","GET",3001));
    healthy=(struct RexxMsg *)destination.tail;
    /* The fortieth tile retains its own capacity despite 39 silent sources. */
    for(i=0;i<TILE_SOURCE_LIMIT-1;++i) {
        struct Message *m=GetMsg(&destination); PutMsg(sources.replies,m);
    }
    assert((struct RexxMsg *)GetMsg(&destination)==healthy);
    reply(&sources,healthy);TileSourcesCollect(&sources,result,NULL);
    assert(!liveMessages && !liveStrings);
    assert(lastOwner==&owners[TILE_SOURCE_LIMIT-1]);
    successes=2; failures=1;
    for(i=0;i<TILE_SOURCE_LIMIT;++i)assert(TileSourceSend(&sources,&owners[i],FALSE,"SOURCE","GET",UINT32_MAX-100));
    assert(!TileSourceSend(&sources,&owners[TILE_SOURCE_LIMIT],FALSE,"SOURCE","GET",0));
    assert(liveMessages==TILE_SOURCE_LIMIT);
    TileSourcesExpire(&sources,1000,result,NULL);assert(failures==TILE_SOURCE_LIMIT+1);
    assert(TileSourcesNext(&sources,1000,2000)==2000);
    assert(!TileSourcesClose(&sources));
    while((healthy=(struct RexxMsg *)GetMsg(&destination)))reply(&sources,healthy);
    TileSourcesCollect(&sources,result,NULL);
    assert(TileSourcesIdle(&sources) && !liveMessages && !liveStrings);
    assert(TileSourcesClose(&sources));
    assert(TileSetCommand("SET %v NOW","on",buffer,sizeof(buffer)) && !strcmp(buffer,"SET on NOW"));
    assert(!TileSetCommand("SET %v %v","on",buffer,sizeof(buffer)));
    assert(!TileSetCommand("SET %x %v","on",buffer,sizeof(buffer)));
    assert(!TileSetCommand("SET","on",buffer,sizeof(buffer)));
    assert(!TileSetCommand("SET %v","on",buffer,4));
    puts("PASS: bounded async sources, independent progress, timeout/late reply ownership, rollover, substitution");
    return 0;
}
