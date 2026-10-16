/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdio.h>
#define FALSE 0
#define ICONLIST_DISP_MODELIST 1
struct IconEntry {struct IconEntry *next;unsigned ie_GridWidth,ie_GridHeight;};
struct Data {unsigned icld_DisplayFlags;struct IconEntry *icld_IconList;};
#define ForeachNode(list,entry) for(entry=*(list);entry;entry=entry->next)
static int transition(struct Data *data){struct IconEntry *entry;
#include "grid.inc"
return 1;}
int main(void){struct IconEntry b={0,2048,2048},a={&b,64,128};struct Data d={1,&a};assert(!transition(&d));assert(!a.ie_GridWidth&&!a.ie_GridHeight&&!b.ie_GridWidth&&!b.ie_GridHeight);a.ie_GridWidth=128;b.ie_GridHeight=256;d.icld_DisplayFlags=0;assert(transition(&d));assert(!a.ie_GridWidth&&!b.ie_GridHeight);puts("tiles-list-transition PASS");}
