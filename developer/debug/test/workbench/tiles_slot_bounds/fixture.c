/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef intptr_t IPTR;
struct Rectangle { int MinX, MinY, MaxX, MaxY; };
struct IconEntry { int ie_IconX,ie_IconY,ie_AreaWidth,ie_AreaHeight,ie_GridWidth,ie_Flags; };
struct IconList_DATA { int icld_ViewX,icld_ViewY,icld__Option_IconListMode,icld_IconAreaLargestWidth,icld_IconAreaLargestHeight; struct IconEntry *update_entry; };
struct Object { int left,top,right,bottom; };
#define _mleft(o) ((o)->left)
#define _mtop(o) ((o)->top)
#define _mright(o) ((o)->right)
#define _mbottom(o) ((o)->bottom)
#define ICON_LISTMODE_GRID 1
#define ICONENTRY_FLAG_LASSO 2
static int select_lasso(struct IconList_DATA *data,struct IconEntry *node,struct Rectangle *lasso_rect) {
 struct Rectangle iconrect; IPTR update_entry=0;
 /* EXTRACT_lasso */
 return !!update_entry;
}
static struct Rectangle clear_bounds(struct Object *obj,struct IconList_DATA *data) {
 /* Area rectangle service is mocked to the registered area's local extent. */
 struct Rectangle rect={0,0,data->update_entry->ie_AreaWidth-1,data->update_entry->ie_AreaHeight-1};
 /* EXTRACT_clear */
 return rect;
}
static struct Rectangle neighbor_bounds(struct Object *obj,struct IconList_DATA *data,struct IconEntry *entry) {
 struct Rectangle rect2={0,0,entry->ie_AreaWidth-1,entry->ie_AreaHeight-1};
 /* EXTRACT_neighbor */
 return rect2;
}
static void equal(struct Rectangle r,int x,int y,int right,int bottom) {
 assert(r.MinX==x && r.MinY==y && r.MaxX==right && r.MaxY==bottom);
}
int main(void) {
 struct IconEntry small={5,7,92,90,92,0},large={102,7,189,185,189,0};
 struct IconList_DATA data={11,13,1,189,185,&small};
 struct Object obj={20,30,500,500};
 struct Rectangle lasso={8,0,41,40}; /* content x19..52: before incorrect shifted min53 */
 assert(select_lasso(&data,&small,&lasso)); assert(small.ie_Flags&ICONENTRY_FLAG_LASSO);
 assert(!select_lasso(&data,&small,&lasso)); /* no duplicate selection transition */
 lasso=(struct Rectangle){300,300,310,310};
 assert(select_lasso(&data,&small,&lasso)); assert(!small.ie_Flags);
 equal(clear_bounds(&obj,&data),20,30,105,113); /* viewport clips projected14,24 */
 equal(neighbor_bounds(&obj,&data,&small),14,24,105,113);
 data.update_entry=&large;
 equal(clear_bounds(&obj,&data),111,30,299,208);
 equal(neighbor_bounds(&obj,&data,&large),111,24,299,208);
 /* Ordinary icon still receives historical centering in a legacy grid. */
 small.ie_GridWidth=0; data.update_entry=&small;
 equal(clear_bounds(&obj,&data),62,71,153,160);
 equal(neighbor_bounds(&obj,&data,&small),62,71,153,160);
 lasso=(struct Rectangle){8,0,41,40}; assert(!select_lasso(&data,&small,&lasso));
 lasso=(struct Rectangle){42,0,43,40}; assert(select_lasso(&data,&small,&lasso));
 /* Free positioning must not center an ordinary entry either. */
 data.icld__Option_IconListMode=0;
 equal(neighbor_bounds(&obj,&data,&small),14,24,105,113);
 equal(clear_bounds(&obj,&data),20,30,105,113);
 puts("tiles-slot-bounds PASS");
}
