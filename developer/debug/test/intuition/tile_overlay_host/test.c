/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
int main(void){
 assert(front_priority(0)==20&&back_priority(0)==20);
 assert(front_priority(LAYERBACKDROP)==10&&back_priority(LAYERBACKDROP)==10);
 assert(front_priority(LAYERPROTECTED)==30&&back_priority(LAYERPROTECTED)==30);
 struct Layer root={0},a={0},b={0},overlay={0};
 root.front=&a;a.parent=b.parent=overlay.parent=&root;
 a.priority=b.priority=20;overlay.priority=30;a.front=&b;b.back=&a;b.front=&overlay;overlay.back=&b;
 assert(upfront(&a)&&target==&b&&moves==1&&!locks);
 assert(upfront(&b)&&moves==1&&!locks); /* cannot cross overlay */
 assert(behind(&overlay)&&moves==1&&!locks); /* bottom of own class */
 assert(!move(&a,&overlay)&&!move(&overlay,&a)&&moves==1&&!locks);
 assert(allowed(1,0,WFLG_BORDERLESS|WFLG_TOOLBOX));
 for(int bad=4;bad<=16;bad*=2)assert(!allowed(1,0,3|bad));
 assert(!allowed(1,1,3)&&!allowed(1,0,1)&&!allowed(1,0,2));
 assert(allowed(0,1,31));puts("PASS protected layer priority");return 0;}
