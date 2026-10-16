/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include "tile_transaction.h"
#include <string.h>
int TileRenameAdd(struct TileRenamePlan *plan,const char *from,const char *to)
{
    unsigned n;
    if(!plan||!from||!to||!from[0]||!to[0]||plan->count>=TILE_TXN_STEPS||
       strlen(from)>=TILE_TXN_PATH||strlen(to)>=TILE_TXN_PATH||!strcmp(from,to))return 0;
    n=plan->count++;strcpy(plan->steps[n].from,from);strcpy(plan->steps[n].to,to);return 1;
}
int TileRenameExecute(struct TileRenamePlan *plan,int (*move)(void*,const char*,const char*),void *context)
{
    unsigned n;
    if(!plan||!move||plan->count>TILE_TXN_STEPS)return 0;
    plan->completed=plan->rollbackFailed=0;
    for(n=0;n<plan->count;++n) {
        if(!move(context,plan->steps[n].from,plan->steps[n].to))break;
        ++plan->completed;
    }
    if(n==plan->count)return 1;
    while(plan->completed) {
        n=plan->completed-1;
        if(!move(context,plan->steps[n].to,plan->steps[n].from)) {
            plan->rollbackFailed=1;return 0;
        }
        --plan->completed;
    }
    return 0;
}

long TileBackdropAppend(char *buffer,unsigned long length,unsigned long capacity,const char *entry)
{
    unsigned long start,end,n;
    if(!buffer||!entry||entry[0]!=':'||strchr(entry,'\n')||strchr(entry,'\r')||length>capacity)return -1;
    n=strlen(entry);
    for(start=0;start<length;start=end+1) {
        for(end=start;end<length&&buffer[end]!='\n';++end){}
        unsigned long stop=end;
        if(stop>start&&buffer[stop-1]=='\r')--stop;
        if(stop-start==n&&!memcmp(buffer+start,entry,n))return length;
    }
    unsigned long separator=length&&buffer[length-1]!='\n';
    if(n>=capacity-length||separator>capacity-length-n-1)return -1;
    if(separator)buffer[length++]='\n';
    memcpy(buffer+length,entry,n);length+=n;buffer[length++]='\n';return length;
}
