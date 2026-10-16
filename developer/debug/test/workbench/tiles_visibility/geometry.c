/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include <assert.h>
#include <stdio.h>
#include "iconlist_visibility.h"

static int visible(struct IconTileBounds tile, struct IconTileBounds viewport,
                   const struct IconTileBounds *uncovered, unsigned count)
{
    unsigned i;
    if (!IconTileIntersect(&tile, &viewport)) return 0;
    for (i = 0; i < count; ++i) {
        struct IconTileBounds copy = tile;
        if (IconTileIntersect(&copy, &uncovered[i])) return 1;
    }
    return 0;
}
int main(void)
{
    const struct IconTileBounds viewport = {10,20,109,119};
    const struct IconTileBounds all = {0,0,199,199};
    const struct IconTileBounds strips[] = {{0,0,29,199},{80,0,199,199}};
    assert(visible((struct IconTileBounds){30,30,79,79},viewport,&all,1));
    assert(!visible((struct IconTileBounds){30,30,79,79},viewport,strips,2));
    assert(visible((struct IconTileBounds){30,30,80,79},viewport,strips,2)); /* one pixel */
    assert(!visible((struct IconTileBounds){110,20,150,80},viewport,&all,1));
    assert(visible((struct IconTileBounds){109,20,150,80},viewport,&all,1));
    assert(!visible((struct IconTileBounds){-50,-50,9,19},viewport,&all,1));
    assert(visible((struct IconTileBounds){-50,-50,10,20},viewport,&all,1));
    assert(!visible((struct IconTileBounds){30,30,79,79},viewport,NULL,0));
    assert(!visible((struct IconTileBounds){30,30,29,29},viewport,&all,1));
    /* Pointer must intersect the clipped tile, not merely any visible pixel.
     * A covered point stays outside even when another part remains exposed. */
    struct IconTileBounds exposed = {30,30,80,79};
    struct IconTileBounds right = strips[1];
    assert(IconTileIntersect(&exposed, &viewport));
    assert(IconTileIntersect(&exposed, &right));
    struct IconTileBounds point = {79,40,79,40}, copy = exposed;
    assert(!IconTileIntersect(&copy, &point));
    point = (struct IconTileBounds){80,40,80,40}; copy = exposed;
    assert(IconTileIntersect(&copy, &point));
    point = (struct IconTileBounds){80,80,80,80}; copy = exposed;
    assert(!IconTileIntersect(&copy, &point)); /* label below surface */
    /* A partially scrolled tile anchors to its original full surface, never
     * to the first exposed pixel. Move the layer independently of clipping. */
    struct IconTileBounds full, clipped;
    const struct IconTileBounds partial = {-20,10,43,73};
    assert(IconTileProject(&partial,&viewport,50,60,&all,&full,&clipped));
    assert(full.minx==30 && full.miny==70 && full.maxx==93 && full.maxy==133);
    assert(clipped.minx==60 && clipped.miny==80 && clipped.maxx==93 && clipped.maxy==133);
    assert(IconTileProject(&partial,&viewport,-30,-40,&all,&full,&clipped));
    assert(full.minx==-50 && full.miny==-30 && full.maxx==13 && full.maxy==33);
    assert(clipped.minx==0 && clipped.miny==0 && clipped.maxx==13 && clipped.maxy==33);
    assert(!IconTileProject(&partial,&viewport,300,300,&all,&full,&clipped));
    int ax=31,ay=47;
    assert(IconTileAnchor(2,1024,768,128,64,17,29,&ax,&ay));
    assert(ax==31&&ay+29+64==768);
    assert(IconTileAnchor(2,800,600,128,64,17,29,&ax,&ay));
    assert(ay+29+64==600);
    assert(IconTileAnchor(4,800,600,128,64,17,29,&ax,&ay));
    assert(ax+17+128==800);
    assert(IconTileAnchor(1,800,600,128,64,17,29,&ax,&ay)&&ay==-29);
    assert(IconTileAnchor(3,800,600,128,64,17,29,&ax,&ay)&&ax==-17);
    assert(!IconTileAnchor(0,800,600,128,64,17,29,&ax,&ay));
    assert(!IconTileAnchor(2,80,60,128,64,17,29,&ax,&ay));
    puts("PASS production visibility/projection: full rectangle, viewport/scroll/screen clipping, movement, occlusion, one-pixel exposure");
    return 0;
}
