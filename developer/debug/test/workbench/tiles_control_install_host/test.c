/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
int main(void){STRPTR types[]={"TILE=toggle",NULL};
add("HOME:Tiles",1);add("HOME:Tiles/Control",1);add("HOME:Tiles/Control/network.info",0);
assert(save_icon("HOME:Tiles/Control/network","SYS:Tiles/Control/network",types,FALSE,0,0)&&write_count==0);
strcpy(deny,"HOME:Tiles/Control/bluetooth.info");assert(!save_icon("HOME:Tiles/Control/bluetooth","SYS:Tiles/Control/bluetooth",types,FALSE,0,0)&&write_count==0);deny[0]=0;
add("SYS:Tiles/Control/sound.info",0);assert(save_icon("HOME:Tiles/Control/sound","SYS:Tiles/Control/sound",types,FALSE,0,0)&&read_count==1&&write_count==1);
fail_write=1;assert(!save_icon("HOME:Tiles/Control/battery","SYS:Tiles/Control/battery",types,FALSE,0,0));assert(lookup("HOME:Tiles/Control/battery.info")<0);fail_write=0;
assert(save_icon("HOME:Tiles/Control/battery","SYS:Tiles/Control/battery",types,FALSE,0,0));
add("HOME:conflict",0);assert(!drawer("HOME:conflict"));assert(drawer("HOME:Tiles"));assert(drawer("HOME:new"));
char *argv[]={"TilesControl","HOME:Tiles",NULL};assert(control_main(2,argv)==0);int oldwrites=write_count;assert(control_main(2,argv)==0&&write_count==oldwrites);
/* Remove fake seed to exercise all generated provider contracts. */
int seed=lookup("SYS:Tiles/Control/sound.info");strcpy(files[seed].path,"unused-seed");
char *fresh[]={"TilesControl","CHECK:Tiles",NULL};assert(control_main(2,fresh)==0&&contract_mask==127);
puts("PASS: preserved custom icons; denied access refuses write; seed imported; failed write/retry; directory conflict; full idempotent provisioning");return 0;}
