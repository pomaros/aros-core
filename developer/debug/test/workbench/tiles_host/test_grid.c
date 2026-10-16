/* SPDX-License-Identifier: APL-1.1
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 */
static void grid_assert(const struct IconGridSlot *slots, ULONG count, ULONG limit, BOOL vertical)
{
    for (ULONG i=0; i<count; ++i) {
        assert(slots[i].placed && slots[i].x>=0 && slots[i].y>=0);
        assert((vertical ? slots[i].y+slots[i].height : slots[i].x+slots[i].width)<=limit);
        assert(IconGridFits(slots,count,i));
    }
}
static void grid_tests(void)
{
    struct IconGridSlot slots[40], saved[40];
    unsigned seed=12345;
    assert(IconGridSnap(-100,8,64)==0);
    assert(IconGridSnap(39,8,64)==0);
    assert(IconGridSnap(40,8,64)==1);
    assert(IconGridSnap(72,8,64)==1);
    for (unsigned pass=0; pass<80; ++pass) {
        ULONG limit=4+pass%13;
        BOOL vertical=pass%2;
        memset(slots,0,sizeof(slots));
        for (unsigned i=0; i<40; ++i) {
            seed=seed*1664525u+1013904223u;
            slots[i].width=i%3 ? 1 : 1+(seed>>24)%4;
            slots[i].height=i%3 ? 1 : 1+(seed>>28)%4;
            /* Deliberate pinned collisions; later entries must be relocated. */
            slots[i].pinned=pass%3==0;
            slots[i].x=slots[i].y=0;
        }
        assert(IconGridPlace(slots,40,limit,vertical));
        grid_assert(slots,40,limit,vertical);
        memcpy(saved,slots,sizeof(slots));
        for (unsigned i=0; i<40; ++i) slots[i].pinned=TRUE;
        assert(IconGridPlace(slots,40,limit,vertical));
        for (unsigned i=0; i<40; ++i) {
            assert(slots[i].x==saved[i].x && slots[i].y==saved[i].y);
            slots[i].pinned=FALSE;
        }
        /* Clean Up repacks but may not change a tile's cell span. */
        assert(IconGridPlace(slots,40,limit,vertical));
        grid_assert(slots,40,limit,vertical);
        for (unsigned i=0; i<40; ++i)
            assert(slots[i].width==saved[i].width && slots[i].height==saved[i].height);
    }
    memset(slots,0,sizeof(slots));
    slots[0].width=4; slots[0].height=4;
    assert(!IconGridPlace(slots,1,3,FALSE));
    assert(IconGridPlace(slots,1,4,FALSE));
    slots[0].width=33;
    assert(!IconGridPlace(slots,1,8,FALSE));
    /* Full32x32 group and smaller children retain exact disjoint footprints. */
    for (unsigned vertical=0;vertical<2;++vertical) {
        struct IconGridSlot groups[4]={{0}};
        groups[0].width=groups[0].height=32;
        groups[1].width=17;groups[1].height=9;
        groups[2].width=4;groups[2].height=4;
        groups[3].width=groups[3].height=1;
        assert(IconGridPlace(groups,4,32,vertical));grid_assert(groups,4,32,vertical);
        assert(groups[0].x==0&&groups[0].y==0);
        assert(vertical?groups[1].x>=32:groups[1].y>=32);
        for(unsigned j=0;j<4;++j)groups[j].pinned=TRUE;
        assert(IconGridPlace(groups,4,32,vertical));grid_assert(groups,4,32,vertical);
    }
    /* A crowded desktop with one tile must retain every ordinary cell. */
    {
        struct IconGridSlot dense[600] = {{0}};
        for (ULONG i=0; i<600; ++i) dense[i].width=dense[i].height=1;
        dense[0].width=dense[0].height=2;
        assert(IconGridPlace(dense,600,30,FALSE));
        grid_assert(dense,600,30,FALSE);
        assert(dense[599].y<=20);
    }
    puts("tiles-grid-host PASS: ordinary1x1/mixed1..4 spans, pinned collisions, exact reservations, horizontal/vertical wrap, snap, restore, Clean Up and invalid bounds");
}
