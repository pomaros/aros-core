/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int BPTR;typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define MODE_OLDFILE 1005
#define MODE_NEWFILE 1006
#define SYS_Asynch 1
#define SYS_Input 2
#define SYS_Output 3
#define TAG_DONE 0
/* Distinct caller streams remain open throughout. Child handles are separate. */
static unsigned live[5],owner[5],opens,closes,launches,failOpen,failLaunch,immediate;
static BPTR Open(const char *path,int mode){assert(!strcmp(path,"NIL:"));++opens;assert(opens<=2);assert(mode==(opens==1?MODE_OLDFILE:MODE_NEWFILE));if(opens==failOpen)return 0;unsigned h=opens+2;assert(!live[h]);live[h]=1;owner[h]=1;return h;}
static int Close(BPTR h){assert(h>=3&&h<=4&&live[h]&&owner[h]==1);live[h]=owner[h]=0;++closes;return 1;}
static void child_exit(void){for(unsigned h=3;h<=4;h++){assert(live[h]&&owner[h]==2);live[h]=owner[h]=0;++closes;}}
static int SystemTags(const char *command,...){assert(!strcmp(command,"SYS:System/TilesControlSource"));++launches;va_list ap;va_start(ap,command);unsigned seen=0;int tag;
 while((tag=va_arg(ap,int))!=TAG_DONE){int value=va_arg(ap,int);assert(tag>=SYS_Asynch&&tag<=SYS_Output);assert(!(seen&(1u<<tag)));seen|=1u<<tag;if(tag==SYS_Asynch)assert(value==TRUE);else {unsigned expected=tag==SYS_Input?3:4;assert(value==(int)expected&&live[expected]&&owner[expected]==1);}}
 va_end(ap);assert(seen==((1u<<SYS_Asynch)|(1u<<SYS_Input)|(1u<<SYS_Output)));
 if(failLaunch)return -1;
 owner[3]=owner[4]=2;if(immediate)child_exit();return 0;
}
/* EXTRACT */
static void reset(void){assert(!live[3]&&!live[4]);memset(live,0,sizeof(live));memset(owner,0,sizeof(owner));live[1]=live[2]=owner[1]=owner[2]=1;opens=closes=launches=failOpen=failLaunch=immediate=0;}
static void caller_intact(void){assert(live[1]&&live[2]&&owner[1]==1&&owner[2]==1);}
int main(void){reset();failOpen=1;assert(!start_clock_source()&&opens==1&&!closes&&!launches);caller_intact();reset();failOpen=2;assert(!start_clock_source()&&opens==2&&closes==1&&!launches);caller_intact();reset();failLaunch=1;assert(!start_clock_source()&&launches==1&&closes==2);caller_intact();reset();assert(start_clock_source()&&launches==1&&!closes&&owner[3]==2&&owner[4]==2);caller_intact();child_exit();assert(closes==2);reset();immediate=1;assert(start_clock_source()&&launches==1&&closes==2&&!live[3]&&!live[4]);caller_intact();puts("tiles-clock-launch-host PASS");}
