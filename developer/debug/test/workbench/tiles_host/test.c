/* SPDX-License-Identifier: APL-1.1
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 */
int main(void)
{
    grid_tests();
    {
        struct WBTileState groupState;
        assert(WBTileStateInitBounded(&groupState,WBAPPICON_CELLS(32,32),NULL,500,32));
        assert(groupState.width==2048 && groupState.height==2048);
        assert(!WBTileStateInit(&groupState,WBAPPICON_CELLS(5,1),NULL,500));
        assert(!WBTileStateInitBounded(&groupState,WBAPPICON_CELLS(33,1),NULL,500,32));
    }

    struct WBTileState state;
    base.wb_TileTaskResource = &base;
    struct AppIconRegions regions = { .air_Count = 2, .air_Regions = {
        {0,0,64,128,"left"}, {64,0,64,128,"right"}} };
    struct BitMap bitmap = {128,128,32,123};
    struct DiskObject fallback = {42};
    struct MsgPort port = {0};
    struct AppIcon icon = {0};
    struct RastPort rp = {0};
    char label[] = "copied";
    struct TagItem tags[] = {
        {WBAPPICONA_Cells, WBAPPICON_CELLS(2,2)},
        {WBAPPICONA_Surface, (IPTR)&bitmap},
        {WBAPPICONA_Regions, (IPTR)&regions},
        {WBAPPICONA_Cadence, 1}, {TAG_DONE,0}};
    struct AppIconPublish publish = {&icon, &bitmap};
    ULONG width, height;
    struct AppIconQuery query = {&icon, ~0U, 0, 0};
    port.mp_SigTask=&owner;
    icon.ai_MsgPort=&port; /* Reach rollback paths past lifetime validation. */
    for (ULONG w=0; w<=5; ++w) for (ULONG h=0; h<=5; ++h)
        assert(WBTileStateInit(&state, WBAPPICON_CELLS(w,h), NULL, 0) ==
               (w>=1 && w<=4 && h>=1 && h<=4));
    assert(WBTileStateInit(&state, WBAPPICON_CELLS(2,2), &regions, 0));
    assert(state.cadence == 250);
    assert(WBTileStateHit(&state, 63,127)==0);
    assert(WBTileStateHit(&state, 64,0)==1);
    assert(WBTileStateHit(&state, 128,0)==-1);
    assert(WBTileStateHit(&state, -1,0)==-1);
    regions.air_Count=9;
    assert(!WBTileStateInit(&state, WBAPPICON_CELLS(2,2), &regions, 0));
    regions.air_Count=2;
    regions.air_Regions[1].air_Width=65;
    assert(!WBTileStateInit(&state, WBAPPICON_CELLS(2,2), &regions, 0));
    regions.air_Regions[1].air_Width=64;
    strcpy(regions.air_Regions[1].air_Name,"left");
    assert(!WBTileStateInit(&state, WBAPPICON_CELLS(2,2), &regions, 0));
    memset(regions.air_Regions[1].air_Name,'x',32);
    assert(!WBTileStateInit(&state, WBAPPICON_CELLS(2,2), &regions, 0));
    strcpy(regions.air_Regions[1].air_Name,"right");
    /* Every allocation failure must unwind all prior owned resources. */
    for (int n=0; n<7; ++n) {
        icon.ai_Text=label;
        int calls=allocCalls;
        failAfter=n;
        assert(!WBTileCreate(&icon,&fallback,tags,&base));
        assert(allocCalls-calls==n+1); /* Injected allocation was actually reached. */
        assert(allocations==0 && graphics.refs==0);
    }
    failAfter=-1;
    for (int phase=0; phase<3; ++phase) {
        icon.ai_Text=label;
        int gfxCalls=graphicsCalls, timerCalls=deviceCalls;
        failGraphics=phase==0;
        failDevice=phase==1;
        bitmap.width=phase==2 ? 127 : 128;
        assert(!WBTileCreate(&icon,&fallback,tags,&base));
        assert(graphicsCalls==gfxCalls+1 && deviceCalls==timerCalls+1);
        assert(allocations==0 && graphics.refs==0 && devices==0);
    }
    failGraphics=failDevice=0;
    bitmap.width=128;
    icon.ai_Text=label; icon.ai_MsgPort=&port;
    base.wb_TileTaskResource=NULL;
    assert(!WBTileCreate(&icon,&fallback,tags,&base));
    base.wb_TileTaskResource=&base;
    port.mp_SigTask=&other;
    assert(!WBTileCreate(&icon,&fallback,tags,&base));
    port.mp_SigTask=&owner;
    assert(WBTileCreate(&icon,&fallback,tags,&base));
    base.wb_AppIcons=&icon;
    assert(WBTileQuery(&query,&base) && query.aiq_Flags==0);
    icon.ai_Tile->visible=TRUE;
    icon.ai_Tile->visibilityTick=now;
    assert(WBTileQuery(&query,&base) && query.aiq_Flags==APPICONQF_VISIBLE);
    current=&other;
    assert(!WBTileQuery(&query,&base) && query.aiq_Flags==0);
    current=&owner;
    now=1000;
    assert(WBTileQuery(&query,&base) && query.aiq_Flags==0);
    now=0;
    icon.ai_Tile->visible=FALSE;
    label[0]='X'; bitmap.pixel=999; fallback.pixel=888;
    regions.air_Regions[0].air_Name[0]='X';
    assert(!strcmp(icon.ai_Text,"copied"));
    assert(icon.ai_DiskObject->pixel==42);
    assert(AppIcon_TileSize(&icon,&width,&height) && width==128 && height==128);
    assert(AppIcon_DrawTile(&icon,&rp,0,0) && rp.pixel==123);
    assert(AppIcon_TileHit(&icon,0,0));
    assert(AppIcon_TileHit(&icon,0,0));
    assert(!AppIcon_TileHit(&icon,128,0));
    assert(port.count==0); /* hit queries never send */
    current=&other; /* Wanderer sends from a different task than the owner. */
    assert(AppIcon_TileClick(&icon,0,0));
    assert(port.count==1); /* delivery is immediate, no publish/pump */
    current=&other;
    assert(!WBTilePublish(&publish,&base));
    current=&owner;
    now=249;
    assert(!WBTilePublish(&publish,&base));
    assert(port.count==1); /* pumping does not require a frame */
    struct AppIconTileMessage *msg=(void *)port.messages[0];
    assert(msg->atm_Message.am_Class==AMCLASSICON_Region);
    assert(!strcmp(msg->atm_Region,"left"));
    assert(msg->atm_Message.am_Message.mn_ReplyPort==&base.wb_TileReplyPort);
    PutMsg(&base.wb_TileReplyPort, &msg->atm_Message.am_Message);
    port.count=0;
    now=250;
    assert(WBTilePublish(&publish,&base));
    assert(AppIcon_DrawTile(&icon,&rp,0,0) && rp.pixel==999);
    /* Heartbeats do no graphics work and do not move the frame-rate gate. */
    assert(AppIcon_TilesChanged());
    now=499;
    publish.aip_Surface=NULL;
    int heartbeatAllocations=allocCalls;
    assert(WBTilePublish(&publish,&base));
    assert(allocCalls==heartbeatAllocations);
    assert(icon.ai_Tile->state.lastFrame==250);
    assert(icon.ai_Tile->state.lastAlive==499);
    assert(!AppIcon_TilesChanged());
    assert(WBTileStateDue(&icon.ai_Tile->state,500));
    publish.aip_Surface=&bitmap;
    bitmap.width=127; now=500;
    assert(!WBTilePublish(&publish,&base));
    assert(AppIcon_DrawTile(&icon,&rp,0,0) && rp.pixel==999);
    bitmap.width=128;
    assert(!AppIcon_TilesChanged());
    now=5000; /* frame at 250, expiry plus refresh deadline margin */
    assert(AppIcon_TilesChanged());
    assert(!AppIcon_DrawTile(&icon,&rp,0,0));
    assert(!AppIcon_TileHit(&icon,0,0));
    assert(!AppIcon_TileClick(&icon,0,0));
    assert(!AppIcon_TilesChanged());
    /* A healthy unchanged source revives its cached image without a copy. */
    publish.aip_Surface=NULL;
    heartbeatAllocations=allocCalls;
    assert(WBTilePublish(&publish,&base));
    assert(allocCalls==heartbeatAllocations);
    assert(AppIcon_TilesChanged());
    assert(AppIcon_DrawTile(&icon,&rp,0,0) && rp.pixel==999);
    assert(icon.ai_Tile->state.lastFrame==250);
    assert(icon.ai_Tile->state.lastAlive==5000);
    publish.aip_Surface=&bitmap;
    /* Unsigned millisecond rollover retains the correct age. */
    state.fresh=FALSE; state.haveFrame=TRUE; state.lastFrame=0; state.cadence=6000;
    assert(!WBTileStateDue(&state,5000)); /* stale does not waive cadence */
    assert(WBTileStateDue(&state,6000));
    state.fresh=TRUE; state.lastAlive=UINT32_MAX-100;
    assert(WBTileStateFresh(&state,100));
    assert(!WBTileStateFresh(&state,5000));
    /* Withholding replies cannot grow in-flight allocation without bound. */
    now=6000;
    assert(WBTilePublish(&publish,&base));
    /* New frame arrived after presentation but before the old relay reply. */
    assert(refreshMessages==1 && base.wb_TileNotifyPending);
    WBTileRefreshDone(&base);
    assert(refreshMessages==2 && base.wb_TileNotifyPending);
    WBTileRefreshDone(&base); /* Unknown presenter replies without consuming. */
    assert(refreshMessages==2 && !base.wb_TileNotifyPending);
    assert(AppIcon_TilesChanged());
    WBTileRefreshDone(&base);
    assert(refreshMessages==2 && !base.wb_TileNotifyPending);
    /* Failed wake allocation leaves dirty pixels available to periodic fallback. */
    now=6500; failNotify=TRUE;
    assert(WBTilePublish(&publish,&base));
    assert(!base.wb_TileNotifyPending && AppIcon_TilesChanged());
    failNotify=FALSE; now=7000;
    assert(WBTilePublish(&publish,&base));
    assert(refreshMessages==3 && base.wb_TileNotifyPending);
    publish.aip_Surface=NULL;
    for (int i=0; i<100; ++i) {
        AppIcon_TileClick(&icon,0,0);
        assert(WBTilePublish(&publish,&base));
    }
    assert(port.count==64 && base.wb_TileInflight==64);
    assert(refreshMessages==3 && base.wb_TileNotifyPending); /* stalled desktop: bounded refresh */
    assert(!AppIcon_TileClick(&icon,0,0)); /* bounded inflight */
    while (port.count) {
        struct Message *reply=GetMsg(&port);
        PutMsg(&base.wb_TileReplyPort,reply);
    }
    current=&other;
    assert(!WBTileDetach(&icon,&base));
    current=&owner;
    assert(WBTileDetach(&icon,&base));
    assert(icon.ai_MsgPort==NULL && icon.ai_Tile->owner==NULL);
    assert(!AppIcon_TileClick(&icon,0,0));
    assert(!WBTilePublish(&publish,&base));
    assert(!WBTileQuery(&query,&base) && query.aiq_Flags==0);
    /* Reuse of the same Task address cannot revive a detached lifetime. */
    WBTileOwnerRemoved(&owner,&base);
    assert(!AppIcon_TileClick(&icon,0,0));
    /* A fresh registered lifetime is invalidated by abrupt exit notification. */
    icon.ai_Tile->owner=&owner; icon.ai_MsgPort=&port;
    WBTileOwnerRemoved(&other,&base);
    assert(icon.ai_MsgPort==&port);
    WBTileOwnerRemoved(&owner,&base);
    assert(icon.ai_MsgPort==NULL && !AppIcon_TileClick(&icon,0,0));
    base.wb_AppIcons=NULL;
    assert(!AppIcon_TileSize(&icon,&width,&height));
    WBTileFree(&icon,&base);
    assert(allocations==0 && graphics.refs==0 && locks==0 && devices==0 && base.wb_TileInflight==0);
    puts("tiles-host PASS: cells, bounds/names, copies, allocation rollback, cadence, heartbeat separation and recovery, owner visibility query, immediate region delivery, detach and task-exit invalidation, owner identity, invalid surface, stale fallback, rollover and removal");
    pointer_tests();
    indicator_tests();
    snapshot_tests();
    return 0;
}
