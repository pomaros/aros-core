/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
static void snapshot_tests(void)
{
    struct AppIcon icons[2]={{0}};struct MsgPort ports[2]={{0}};
    struct BitMap image={64,64,32,81};struct DiskObject fallback={3};
    struct Screen *screen=(struct Screen *)(IPTR)0x1000;
    struct AppIconRegions regions={.air_Count=1,.air_Regions={{.air_Left=0,.air_Top=0,.air_Width=64,.air_Height=64,.air_Name="click"}}};
    struct TagItem tags[]={{WBAPPICONA_Surface,(IPTR)&image},{WBAPPICONA_Regions,(IPTR)&regions},{TAG_DONE,0}};
    struct AppIconSnapshotEntry entries[40];
    struct AppIconSnapshotRequest request={screen,entries,40,0,0,0,0};
    struct AppIconSnapshotActivate click={0,0,12,13};
    memset(&base,0,sizeof(base));base.wb_TileTaskResource=&base;current=&owner;now=40000;
    for(unsigned i=0;i<2;++i) {
        ports[i].mp_SigTask=&owner;icons[i].ai_Text="snapshot";icons[i].ai_MsgPort=&ports[i];
        assert(WBTileCreate(&icons[i],&fallback,tags,&base));
        icons[i].next=base.wb_AppIcons;base.wb_AppIcons=&icons[i];
        assert(AppIcon_TilePlacement(&icons[i],screen,TRUE,10+i*80,20,64,64));
        assert(AppIcon_TileVisibility(&icons[i],FALSE)); /* covered still placed */
    }
    current=&other;request.aisr_Capacity=1;assert(!WBTileSnapshot(&request,&base)&&!request.aisr_Handle&&!request.aisr_Count);
    request.aisr_Capacity=40;failAfter=1;assert(!WBTileSnapshot(&request,&base));failAfter=-1;
    assert(WBTileSnapshot(&request,&base)&&request.aisr_Count==2&&!request.aisr_Flags);
    APTR oldHandle=request.aisr_Handle;click.aisa_Handle=oldHandle;
    assert(entries[0].ais_Bitmap!=icons[1].ai_Tile->bitmap&&entries[0].ais_Bitmap->pixel==81);
    struct AppIconSnapshotRequest duplicate={screen,entries,40,0,0,0,0};
    assert(!WBTileSnapshot(&duplicate,&base));
    current=&owner;assert(!WBTileSnapshotFree(oldHandle,&base)&&!WBTileSnapshotActivate(&click,&base));
    current=&other;assert(WBTileSnapshotActivate(&click,&base)&&ports[1].count==1);
    pointer_reply_all(&ports[1]);
    click.aisa_X=64;assert(!WBTileSnapshotActivate(&click,&base));click.aisa_X=12;
    current=&owner;now+=500;image.pixel=99;
    struct AppIconPublish publish={&icons[1],&image};assert(WBTilePublish(&publish,&base));
    current=&other;assert(!WBTileSnapshotActivate(&click,&base));
    /* copied output is independent even when the original bitmap was replaced */
    struct WBTileSnapshotNode *saved=snapshots;assert(saved->entries[0].ais_Bitmap->pixel==81);
    struct BitMap *stable=saved->entries[0].ais_Bitmap;
    assert(WBTileSnapshotRefresh(oldHandle,&base));
    assert(saved->entries[0].ais_Bitmap==stable&&stable->pixel==99);
    assert(WBTileSnapshotActivate(&click,&base));pointer_reply_all(&ports[1]);
    current=&owner;UQUAD oldGeneration=icons[1].ai_Tile->registrationGeneration;
    base.wb_AppIcons=&icons[0];WBTileFree(&icons[1],&base);
    current=&other;assert(WBTileSnapshotRefresh(oldHandle,&base));
    assert(!WBTileSnapshotActivate(&click,&base)&&saved->entries[0].ais_Bitmap->pixel==3);
    current=&owner;icons[1].ai_Text="reused";icons[1].ai_MsgPort=&ports[1];
    assert(WBTileCreate(&icons[1],&fallback,tags,&base));icons[1].next=&icons[0];base.wb_AppIcons=&icons[1];
    assert(icons[1].ai_Tile->registrationGeneration!=oldGeneration);
    assert(AppIcon_TilePlacement(&icons[1],screen,TRUE,90,20,64,64));
    current=&other;assert(!WBTileSnapshotActivate(&click,&base));
    assert(WBTileSnapshotFree(oldHandle,&base));assert(!WBTileSnapshotFree(oldHandle,&base));
    assert(WBTileSnapshot(&request,&base)&&request.aisr_Handle!=oldHandle);
    assert(!WBTileSnapshotFree(oldHandle,&base));assert(WBTileSnapshotFree(request.aisr_Handle,&base));
    /* Address reuse with identical initial frame epoch must still fail. */
    assert(WBTileSnapshot(&request,&base));click.aisa_Handle=request.aisr_Handle;
    current=&owner;base.wb_AppIcons=&icons[0];WBTileFree(&icons[1],&base);
    icons[1].ai_Text="reused twice";icons[1].ai_MsgPort=&ports[1];
    assert(WBTileCreate(&icons[1],&fallback,tags,&base));icons[1].next=&icons[0];base.wb_AppIcons=&icons[1];
    assert(AppIcon_TilePlacement(&icons[1],screen,TRUE,90,20,64,64));
    current=&other;assert(!WBTileSnapshotActivate(&click,&base));
    assert(WBTileSnapshotFree(request.aisr_Handle,&base));
    request.aisr_Screen=(struct Screen *)(IPTR)0x2000;
    assert(WBTileSnapshot(&request,&base)&&!request.aisr_Count);
    assert(WBTileSnapshotFree(request.aisr_Handle,&base));request.aisr_Screen=screen;
    /* Nonfront/hidden and stale placement are not exported. */
    assert(AppIcon_TilePlacement(&icons[1],NULL,FALSE,0,0,0,0));
    now+=500;assert(WBTileSnapshot(&request,&base)&&!request.aisr_Count);assert(WBTileSnapshotFree(request.aisr_Handle,&base));
    assert(AppIcon_TilePlacement(&icons[0],screen,TRUE,10,20,64,64));
    current=&owner;assert(WBTileSuspend(&icons[0],&base));current=&other;
    assert(WBTileSnapshot(&request,&base)&&request.aisr_Count==1&&!request.aisr_Omitted&&!request.aisr_Flags);
    assert(entries[0].ais_Bitmap->pixel==3);
    click.aisa_Handle=request.aisr_Handle;assert(!WBTileSnapshotActivate(&click,&base));
    current=&owner;publish.aip_Icon=&icons[0];assert(WBTilePublish(&publish,&base));
    current=&other;assert(WBTileSnapshotRefresh(request.aisr_Handle,&base)&&entries[0].ais_Bitmap->pixel==99);
    assert(WBTileSnapshotActivate(&click,&base));pointer_reply_all(&ports[0]);
    WBTileOwnerRemoved(&owner,&base);
    assert(WBTileSnapshotRefresh(request.aisr_Handle,&base)&&entries[0].ais_Bitmap->pixel==3);
    assert(!WBTileSnapshotActivate(&click,&base));
    assert(WBTileSnapshotFree(request.aisr_Handle,&base));
    /* Four globally, one per creator; owner-exit retirement is deferred. */
    struct Task creators[5]={{0}};APTR handles[4];
    for(unsigned i=0;i<4;++i){current=&creators[i];assert(WBTileSnapshot(&request,&base));handles[i]=request.aisr_Handle;}
    current=&creators[4];assert(!WBTileSnapshot(&request,&base));
    WBTileOwnerRemoved(&creators[0],&base);assert(WBTileSnapshot(&request,&base));assert(WBTileSnapshotFree(request.aisr_Handle,&base));
    for(unsigned i=1;i<4;++i){current=&creators[i];assert(WBTileSnapshotFree(handles[i],&base));}
    current=&other;assert(!WBTileSnapshotFree(handles[0],&base));
    current=&owner;base.wb_AppIcons=NULL;WBTileFree(&icons[0],&base);WBTileFree(&icons[1],&base);
    assert(!snapshots&&!allocations&&!graphics.refs&&!devices&&!locks&&!base.wb_TileInflight);
    puts("tiles-snapshot-host PASS: covered copies, ownership, capacity, allocation rollback, epoch/reuse rejection, hidden/stale placement, static fallback/resume and bounded/deferred retirement");
}
