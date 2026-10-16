/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#ifndef ICONLIST_VISIBILITY_H
#define ICONLIST_VISIBILITY_H
/* Inclusive pixel bounds, independent of bitmaps and platform APIs. */
struct IconTileBounds { int minx, miny, maxx, maxy; };
static int IconTileIntersect(struct IconTileBounds *a, const struct IconTileBounds *b)
{
    if (a->minx < b->minx) a->minx = b->minx;
    if (a->miny < b->miny) a->miny = b->miny;
    if (a->maxx > b->maxx) a->maxx = b->maxx;
    if (a->maxy > b->maxy) a->maxy = b->maxy;
    return a->minx <= a->maxx && a->miny <= a->maxy;
}
/* Keep panel anchoring independent of viewport/screen clipping. Both outputs
 * are screen-relative, but full retains the original surface dimensions. */
static int IconTileProject(const struct IconTileBounds *surface,
                           const struct IconTileBounds *viewport,
                           int dx, int dy, const struct IconTileBounds *screen,
                           struct IconTileBounds *full, struct IconTileBounds *clipped)
{
    *full = *surface;
    full->minx += dx; full->maxx += dx;
    full->miny += dy; full->maxy += dy;
    *clipped = *surface;
    if (!IconTileIntersect(clipped, viewport)) return 0;
    clipped->minx += dx; clipped->maxx += dx;
    clipped->miny += dy; clipped->maxy += dy;
    return IconTileIntersect(clipped, screen);
}
/* Convert the anchored full-surface origin to entry coordinates. No grid or
 * label offset participates. Caller supplies the same layer/viewport transform
 * as drawing; resize re-evaluates against the current screen dimensions. */
static int IconTileAnchor(unsigned anchor,int sw,int sh,int width,int height,
                          int originx,int originy,int *x,int *y)
{
    if(anchor<1||anchor>4||width<1||height<1||width>sw||height>sh)return 0;
    int oldx=*x,oldy=*y;
    if(anchor==1)*y=-originy;
    if(anchor==2)*y=sh-height-originy;
    if(anchor==3)*x=-originx;
    if(anchor==4)*x=sw-width-originx;
    return *x!=oldx||*y!=oldy;
}
#endif
