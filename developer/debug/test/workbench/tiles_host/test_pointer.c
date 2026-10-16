/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
static void pointer_reply_all(struct MsgPort *port)
{
    struct Message *message;
    while ((message = GetMsg(port))) PutMsg(&base.wb_TileReplyPort, message);
}
static void pointer_tests(void)
{
    struct AppIcon icons[2] = {{0}};
    struct MsgPort ports[2] = {{0}};
    struct BitMap bitmap = {64,64,32,7};
    struct DiskObject fallback = {3};
    struct AppIconQuery query = {&icons[0],0,0,0};
    struct AppIconPublish publish = {&icons[0],&bitmap};
    struct TagItem tags[] = {
        {WBAPPICONA_Cells,WBAPPICON_CELLS(1,1)},
        {WBAPPICONA_Surface,(IPTR)&bitmap}, {WBAPPICONA_Cadence,500},
        {WBAPPICONA_Pointer,TRUE}, {WBAPPICONA_HoverCadence,1}, {TAG_DONE,0}};
    memset(&base,0,sizeof(base)); base.wb_TileTaskResource=&base;
    current=&owner; now=10000;
    for (unsigned i=0;i<2;++i) {
        ports[i].mp_SigTask=&owner;
        icons[i].ai_Text="pointer"; icons[i].ai_MsgPort=&ports[i];
        assert(WBTileCreate(&icons[i],&fallback,tags,&base));
        icons[i].next=base.wb_AppIcons; base.wb_AppIcons=&icons[i];
        assert(icons[i].ai_Tile->hoverCadence==33);
        assert(AppIcon_TileVisibility(&icons[i],TRUE));
        pointer_reply_all(&ports[i]);
    }
    assert(WBTileQuery(&query,&base));
    assert(AppIcon_TilePointer(&icons[0],TRUE,10,20));
    assert(ports[0].count==1);
    struct AppIconTileMessage *borrowed=(void *)ports[0].messages[0];
    assert(borrowed->atm_Message.am_Class==AMCLASSICON_PointerEnter);
    for (unsigned i=0;i<10000;++i)
        assert(AppIcon_TilePointer(&icons[0],TRUE,i%64,(i/64)%64));
    assert(AppIcon_TilePointer(&icons[0],TRUE,30,40));
    assert(ports[0].count==1 && base.wb_TileInflight==1);
    assert(borrowed->atm_Message.am_MouseX==10 && borrowed->atm_Message.am_MouseY==20);
    assert(WBTileQuery(&query,&base) && (query.aiq_Flags&APPICONQF_HOVERED));
    assert(query.aiq_MouseX==30 && query.aiq_MouseY==40);
    now=10032; assert(!WBTilePublish(&publish,&base));
    now=10033; assert(WBTilePublish(&publish,&base));
    /* A stalled first owner cannot consume more messages or stop another tile. */
    assert(AppIcon_TilePointer(&icons[1],TRUE,5,6));
    assert(ports[1].count==1 && base.wb_TileInflight==2);
    publish.aip_Icon=&icons[1];
    assert(!WBTilePublish(&publish,&base)); /* shared fast slot */
    now=10066; assert(WBTilePublish(&publish,&base));
    pointer_reply_all(&ports[1]);
    assert(AppIcon_TilePointer(&icons[1],TRUE,8,9));
    assert(ports[1].count==1);
    assert(((struct AppIconTileMessage *)ports[1].messages[0])->atm_Message.am_Class==AMCLASSICON_PointerMove);
    /* Coalesced leave follows the borrowed enter; it never mutates that enter. */
    now=10067; assert(AppIcon_TilePointer(&icons[0],FALSE,-1,40));
    assert(ports[0].count==1 && borrowed->atm_Message.am_Class==AMCLASSICON_PointerEnter);
    pointer_reply_all(&ports[0]);
    assert(WBTileQuery(&query,&base) && !(query.aiq_Flags&APPICONQF_HOVERED));
    assert(ports[0].count==1);
    assert(((struct AppIconTileMessage *)ports[0].messages[0])->atm_Message.am_Class==AMCLASSICON_PointerLeave);
    publish.aip_Icon=&icons[0];
    now=10099; assert(!WBTilePublish(&publish,&base)); /* normal500ms restored */
    assert(AppIcon_TilePointer(&icons[0],TRUE,63,63));
    assert(AppIcon_TileVisibility(&icons[0],FALSE));
    assert(WBTileQuery(&query,&base) && query.aiq_Flags==0);
    assert(!WBTilePublish(&publish,&base)); /* no fast privilege under a cover */
    now=10533; assert(WBTilePublish(&publish,&base));
    assert(AppIcon_TileVisibility(&icons[0],TRUE));
    assert(AppIcon_TilePointer(&icons[0],TRUE,20,20));
    now+=1000;
    assert(WBTileQuery(&query,&base) && query.aiq_Flags==0); /* stale observations */
    /* Late replies after icon destruction carry no live icon dereference. */
    for (unsigned i=0;i<2;++i) {
        icons[i].ai_Tile->owner=NULL; icons[i].ai_MsgPort=NULL;
    }
    base.wb_AppIcons=NULL;
    for (unsigned i=0;i<2;++i) WBTileFree(&icons[i],&base);
    for (unsigned i=0;i<2;++i) pointer_reply_all(&ports[i]);
    assert(!WBTileQuery(&query,&base));
    assert(query.aiq_Flags==0 && query.aiq_MouseX==0 && query.aiq_MouseY==0);
    assert(allocations==0 && devices==0 && graphics.refs==0 && locks==0 && base.wb_TileInflight==0);
    puts("tiles-pointer-host PASS: opt-in, 10000 coalesced moves, immutable borrowed messages, independent progress, global fast slot, leave/cover/stale pacing, late reply after destruction");
}
