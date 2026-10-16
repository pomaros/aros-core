/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
static void geometry_zero(const struct AppIconGeometry *g)
{
    assert(!g->aig_Flags && !g->aig_Left && !g->aig_Top && !g->aig_Width && !g->aig_Height);
}
/* Exercise the actual presenter adapters as well as the library API. */
static void geometry_observe(struct AppIcon *icon, LONG x, LONG y)
{
    struct WBTile *t=icon->ai_Tile;
    struct AppIconQuery query={icon,0,0,0};
    assert(AppIcon_TileVisibility(icon,TRUE));
    assert(AppIcon_TileGeometry(icon,TRUE,x,y,t->state.width,t->state.height));
    pointer_reply_all(icon->ai_MsgPort);
    assert(WBTileQuery(&query,&base));
}
static void indicator_tests(void)
{
    struct AppIcon icon={0};
    struct MsgPort port={0};
    struct BitMap image={128,128,32,23}, invalid={64,128,32,19};
    struct DiskObject fallback={5};
    struct RastPort rp={0};
    struct AppIconGeometry geometry={&icon,99,99,99,99,99};
    struct AppIconQuery hover={&icon,0,0,0};
    struct AppIconPublish publish={&icon,&image};
    struct TagItem tags[]={{WBAPPICONA_Cells,WBAPPICON_CELLS(2,2)},
        {WBAPPICONA_Surface,(IPTR)&image},{WBAPPICONA_Cadence,500},
        {WBAPPICONA_Pointer,TRUE},{WBAPPICONA_HoverCadence,33},{TAG_DONE,0}};
    memset(&base,0,sizeof(base));base.wb_TileTaskResource=&base;
    current=&owner;now=20000;refreshMessages=0;
    port.mp_SigTask=&owner;icon.ai_Text="indicator";icon.ai_MsgPort=&port;
    assert(WBTileCreate(&icon,&fallback,tags,&base));base.wb_AppIcons=&icon;
    assert(WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    geometry_observe(&icon,-30,40); /* partially clipped: retain full dimensions */
    assert(WBTileQueryGeometry(&geometry,&base) && geometry.aig_Flags==APPICONGF_VALID);
    assert(geometry.aig_Left==-30 && geometry.aig_Top==40 && geometry.aig_Width==128 && geometry.aig_Height==128);
    current=&other;assert(!WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    assert(!WBTileSuspend(&icon,&base) && !icon.ai_Tile->suspended);
    current=&owner;now+=999;assert(WBTileQueryGeometry(&geometry,&base) && geometry.aig_Flags);
    ++now;assert(WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    geometry_observe(&icon,300,250);
    assert(WBTileQueryGeometry(&geometry,&base) && geometry.aig_Left==300 && geometry.aig_Top==250);
    assert(AppIcon_TileVisibility(&icon,FALSE));
    assert(!icon.ai_Tile->geometryValid);
    assert(WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    geometry_observe(&icon,301,251);
    assert(AppIcon_TileGeometry(&icon,FALSE,301,251,128,128));
    assert(WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    geometry_observe(&icon,301,251);
    assert(AppIcon_TileGeometry(&icon,TRUE,301,251,127,128));
    assert(WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    geometry_observe(&icon,301,251);
    assert(AppIcon_TileGeometry(&icon,TRUE,301,251,128,127));
    assert(WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    geometry_observe(&icon,302,252);now+=1000;
    assert(AppIcon_TileGeometry(&icon,TRUE,302,252,128,128));
    assert(WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    geometry_observe(&icon,302,252);
    assert(AppIcon_TilePointer(&icon,TRUE,10,20));assert(port.count==1);
    struct AppIconTileMessage *borrowed=(void*)port.messages[0];
    assert(borrowed->atm_Message.am_Class==AMCLASSICON_PointerEnter);
    assert(WBTileSuspend(&icon,&base));
    assert(icon.ai_Tile->suspended && !icon.ai_Tile->state.fresh && icon.ai_Tile->dirty);
    assert(icon.ai_Tile->owner==&owner && icon.ai_MsgPort==&port && base.wb_AppIcons==&icon);
    assert(refreshMessages==1 && base.wb_TileNotifyPending && port.count==1);
    assert(borrowed->atm_Message.am_Class==AMCLASSICON_PointerEnter); /* immutable borrower */
    for(unsigned i=0;i<100;++i) assert(WBTileSuspend(&icon,&base));
    assert(refreshMessages==1 && base.wb_TileInflight==1);
    assert(WBTileQueryGeometry(&geometry,&base) && geometry.aig_Flags); /* fallback geometry */
    assert(WBTileQuery(&hover,&base) && !(hover.aiq_Flags&APPICONQF_HOVERED));
    assert(!AppIcon_DrawTile(&icon,&rp,0,0));
    publish.aip_Surface=NULL;assert(WBTilePublish(&publish,&base));
    assert(icon.ai_Tile->suspended && !icon.ai_Tile->state.fresh && !AppIcon_DrawTile(&icon,&rp,0,0));
    publish.aip_Surface=&invalid;assert(!WBTilePublish(&publish,&base));
    assert(icon.ai_Tile->suspended && icon.ai_Tile->bitmap->pixel==23);
    publish.aip_Surface=&image;failAfter=0;assert(!WBTilePublish(&publish,&base));failAfter=-1;
    assert(icon.ai_Tile->suspended && !icon.ai_Tile->state.fresh);
    image.pixel=77;assert(WBTilePublish(&publish,&base));
    assert(!icon.ai_Tile->suspended && AppIcon_DrawTile(&icon,&rp,0,0) && rp.pixel==77);
    assert(WBTileSuspend(&icon,&base));
    now+=33;assert(!WBTilePublish(&publish,&base)); /* no fast resume even with prior hover */
    assert(icon.ai_Tile->suspended);
    now+=467;assert(WBTilePublish(&publish,&base) && !icon.ai_Tile->suspended);
    pointer_reply_all(&port);assert(WBTileQuery(&hover,&base));
    assert(port.count==1 && ((struct AppIconTileMessage*)port.messages[0])->atm_Message.am_Class==AMCLASSICON_PointerLeave);
    assert(WBTileSuspend(&icon,&base));assert(WBTileDetach(&icon,&base));
    assert(!WBTileSuspend(&icon,&base));assert(!WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    assert(!WBTilePublish(&publish,&base));
    base.wb_AppIcons=NULL;WBTileFree(&icon,&base);
    pointer_reply_all(&port);assert(!WBTileQuery(&hover,&base)); /* late borrowed reply */
    geometry=(struct AppIconGeometry){&icon,99,99,99,99,99};
    assert(!WBTileQueryGeometry(&geometry,&base));geometry_zero(&geometry);
    assert(!WBTileQueryGeometry(NULL,&base) && !WBTileSuspend(NULL,&base));
    assert(!allocations && !devices && !graphics.refs && !locks && !base.wb_TileInflight);
    puts("tiles-indicator-host PASS: geometry owner/zero/stale/hidden/moved/full bounds; suspend bounded wake, heartbeat refusal, failed/successful resume, normal cadence, immutable borrower and late detach reply");
}
