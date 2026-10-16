/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
int main(void){struct Screen screen={100,80,{0}};struct BitMap bitmap={64,64};struct TileLiftItem items[]={{1,&bitmap,-10,-20,64,64},{2,&bitmap,50,40,64,64}};
for(int n=1;n<=4;n++){calls=0;fail_at=n;assert(!TileLiftOpen(&screen,items,2));assert(!allocations&&!ports&&!windows);}
fail_at=0;fail_read=1;assert(!TileLiftOpen(&screen,items,2));assert(!allocations&&!ports&&!windows);fail_read=0;
struct TileLift *lift=TileLiftOpen(&screen,items,2);assert(lift&&blits==2&&writes==1);
struct TileLiftEvent e;assert(TileLiftInput(lift,52,42,FALSE,&e)&&e.type==TILE_LIFT_CLICK&&e.id==2&&e.x==2&&e.y==2);
assert(TileLiftInput(lift,0,0,FALSE,&e)&&e.id==1&&e.x==10&&e.y==20);
assert(TileLiftInput(lift,90,0,FALSE,&e)&&e.type==TILE_LIFT_DISMISS);
assert(TileLiftInput(lift,52,42,TRUE,&e)&&e.type==TILE_LIFT_DISMISS);
struct BitMap replacement={64,64},small={32,32};
assert(!TileLiftUpdateBitmap(lift,1,&bitmap));assert(!TileLiftUpdateBitmap(lift,1,&small));assert(!TileLiftUpdateBitmap(lift,99,&replacement));assert(!TileLiftUpdateBitmap(lift,1,NULL));
assert(TileLiftUpdateBitmap(lift,1,&replacement)&&lift->items[0].bitmap==&replacement);assert(!TileLiftUpdateBitmap(lift,1,&replacement));assert(TileLiftUpdateBitmap(lift,1,&bitmap));assert(blits==2);
TileLiftRefreshItem(lift,2);assert(blits==3&&writes==1);
assert(TileLiftUpdateBitmap(lift,1,&replacement));
TileLiftRefreshItem(lift,1);assert(blits==5&&writes==1&&blit_sources[3]==&replacement&&blit_sources[4]==&bitmap);
TileLiftRefreshItem(lift,99);assert(blits==5&&writes==1);
TileLiftRefresh(lift);assert(blits==7&&writes==2);TileLiftClose(lift);assert(!allocations&&!ports&&!windows);
items[0].width=65;assert(!TileLiftOpen(&screen,items,2));TileLiftClose(NULL);
items[0].width=64;items[0].left=-10;items[0].top=-20;
for(int n=1;n<=3;n++){calls=0;fail_at=n;assert(!TilePanelOpen(&screen,items,1,20,10,64,64));assert(!allocations&&!ports&&!windows);}
fail_at=0;int oldwrites=writes;lift=TilePanelOpen(&screen,items,1,20,10,64,64);assert(lift&&!lift->background&&writes==oldwrites&&clears==1);
assert(TileLiftInput(lift,20,10,FALSE,&e)&&e.id==1&&e.x==10&&e.y==20);
assert(TileLiftInput(lift,19,10,FALSE,&e)&&e.type==TILE_LIFT_DISMISS);
TileLiftRefreshItem(lift,1);assert(writes==oldwrites&&clears==1);TileLiftClose(lift);assert(!allocations&&!ports&&!windows);
assert(!TilePanelOpen(&screen,items,1,80,10,64,64));
puts("{\"result\":\"PASS\",\"allocation_failures\":4,\"read_failure\":true,\"dim_to_30_percent\":true,\"clipped_blits\":true,\"hit_and_escape\":true,\"outstanding_allocations\":0}");return 0;}
