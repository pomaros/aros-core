/* SPDX-License-Identifier: APL-1.1
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Cell reservations shared by tile layout and its host regression.
 */
#ifndef ICONLIST_GRID_H
#define ICONLIST_GRID_H
struct IconGridSlot
{
    LONG x, y;
    ULONG width, height;
    BOOL pinned, placed;
    APTR entry;
};

static LONG IconGridSnap(LONG pixel, LONG anchor, ULONG pitch)
{
    QUAD delta = (QUAD)pixel - anchor;
    if (!pitch || delta <= 0) return 0;
    return (LONG)((delta + pitch / 2) / pitch);
}

static BOOL IconGridFits(const struct IconGridSlot *slots, ULONG count, ULONG index)
{
    const struct IconGridSlot *a = &slots[index];
    ULONG j;
    for (j=0; j<count; ++j) {
        const struct IconGridSlot *b=&slots[j];
        if (j==index || !b->placed) continue;
        if (a->x < b->x + (LONG)b->width && b->x < a->x + (LONG)a->width &&
            a->y < b->y + (LONG)b->height && b->y < a->y + (LONG)a->height)
            return FALSE;
    }
    return TRUE;
}

static BOOL IconGridPlace(struct IconGridSlot *slots, ULONG count, ULONG limit, BOOL vertical)
{
    ULONG i, position, next[1024] = {0};
    if (!limit || limit > 65535 || count > 65535) return FALSE;
    for (i=0; i<count; ++i) {
        slots[i].placed=FALSE;
        if (!slots[i].width || slots[i].width>32 || !slots[i].height || slots[i].height>32 ||
            (vertical ? slots[i].height : slots[i].width)>limit)
            return FALSE;
    }
    /* Preserve nonconflicting snapshots first; conflicts enter normal packing. */
    for (i=0; i<count; ++i) {
        struct IconGridSlot *s=&slots[i];
        if (s->pinned && s->x>=0 && s->y>=0 && s->x<65535 && s->y<65535 &&
            (vertical ? s->y+(LONG)s->height : s->x+(LONG)s->width)<=(LONG)limit &&
            IconGridFits(slots,count,i))
            s->placed=TRUE;
    }
    for (i=0; i<count; ++i) {
        struct IconGridSlot *s=&slots[i];
        ULONG shape=(s->width-1)*32+s->height-1;
        if (s->placed) continue;
        /* Each <=32x32 obstacle excludes at most 63x63 candidate origins.
         * The extra factor covers boundary skips in a32-cell-wide viewport.
         * Normal AppIcon surfaces remain limited to4x4 by workbench.library;
         * only registered group surfaces may supply larger reservations. */
        /* Occupancy only grows: an origin rejected for this shape can never
         * become available later. Retain one first-fit cursor per shape to
         * avoid restarting a dense desktop scan for every ordinary icon. */
        for (position=next[shape]; position<(count+1)*8192; ++position) {
            s->x=vertical ? position/limit : position%limit;
            s->y=vertical ? position%limit : position/limit;
            if ((vertical ? s->y+(LONG)s->height : s->x+(LONG)s->width)>(LONG)limit)
                continue;
            if (IconGridFits(slots,count,i)) { s->placed=TRUE; break; }
        }
        if (!s->placed) return FALSE;
        next[shape]=position+1;
    }
    return TRUE;
}
#endif
