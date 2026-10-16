/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include "consumers.h"
#include <stdio.h>
#include <string.h>
void IndicatorConsumerAdd(struct IndicatorConsumers *list, const char *name, uint32_t usage)
{
    unsigned i,j;
    ++list->scanned;
    if (!usage) return;
    for (i=0;i<IND_CPU_TOP;++i) if (usage>list->top[i].usage) break;
    if (i==IND_CPU_TOP) return;
    for (j=IND_CPU_TOP-1;j>i;--j) list->top[j]=list->top[j-1];
    list->top[i].usage=usage;
    if (!name || !*name) name="Unnamed";
    for (j=0;j<IND_CPU_NAME-1 && name[j];++j) {
        unsigned char c=(unsigned char)name[j];
        list->top[i].name[j]=(c>=32 && c<=126 && c!=27)?(char)c:'?';
    }
    list->top[i].name[j]=0;
}
int IndicatorConsumersFormat(const struct IndicatorConsumers *list, char *out, size_t size)
{
    unsigned i;
    size_t used=0;
    int n;
    if (!size) return 0;
    n=snprintf(out,size,"CPU%s: ",list->truncated?" (partial)":"");
    if (n<0 || (size_t)n>=size) return 0;
    used=(size_t)n;
    if (!list->top[0].usage) {
        n=snprintf(out+used,size-used,"Idle or accounting unavailable");
        return n>=0 && (size_t)n<size-used;
    }
    for (i=0;i<IND_CPU_TOP && list->top[i].usage;++i) {
        unsigned bp=(unsigned)(((uint64_t)list->top[i].usage*10000)>>32);
        n=snprintf(out+used,size-used,"%s%s %u.%02u%%",i?"; ":"",list->top[i].name,bp/100,bp%100);
        if (n<0 || (size_t)n>=size-used) return 0;
        used+=(size_t)n;
    }
    return 1;
}
int IndicatorAuxUpdate(struct IndicatorAux *aux, const char *text, uint32_t now)
{
    size_t i;
    for (i=0;i<sizeof(aux->text) && text[i];++i)
        if ((unsigned char)text[i]<32 || (unsigned char)text[i]>126) return 0;
    if (!i || i==sizeof(aux->text)) return 0;
    memcpy(aux->text,text,i+1); aux->received=now; aux->available=1;
    return 1;
}
const char *IndicatorAuxText(const struct IndicatorAux *aux, uint32_t now)
{
    return aux->available && (uint32_t)(now-aux->received)<4000 ? aux->text : "CPU data unavailable";
}
