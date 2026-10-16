/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include "state.h"
#include <string.h>
#include <stdio.h>
int IndicatorUpdate(struct IndicatorState *s, const char *p, uint32_t now)
{
    unsigned value=0, charging, digits=0;
    const char *text;
    size_t length;
    if (!p) return 0;
    while (*p>='0' && *p<='9') {
        if (++digits>3) return 0;
        value=value*10+(unsigned)(*p++-'0');
    }
    if (!digits || value>100 || *p++!=' ') return 0;
    if (*p!='0' && *p!='1') return 0;
    charging=(unsigned)(*p++-'0');
    if (*p++!=' ') return 0;
    text=p;
    for (length=0; *p; ++p, ++length)
        if ((unsigned char)*p<32 || (unsigned char)*p==127 || length>=INDICATOR_TEXT-1) return 0;
    if (!length) return 0;
    s->value=value; s->charging=charging;
    memcpy(s->text,text,length+1); s->received=now; s->available=1; s->fields=IND_FIELD_VALUE;
    return 1;
}
int IndicatorHealthy(const struct IndicatorState *s, uint32_t now)
{
    return s->available && (uint32_t)(now-s->received)<INDICATOR_LEASE_MS;
}
void IndicatorAnchor(int x, int y, int sw, int sh, int w, int h, int *left, int *top)
{
    int maxx=sw>w ? sw-w : 0, maxy=sh>h ? sh-h : 0;
    *left=x<0 ? 0 : x>maxx ? maxx : x;
    *top=y<0 ? 0 : y>maxy ? maxy : y;
}

static int percent(const char *p, unsigned *value, const char **end)
{
    unsigned n=0, count=0;
    while (*p>='0' && *p<='9') {
        if (++count>3) return 0;
        n=n*10+(unsigned)(*p++-'0');
    }
    if (!count || n>100) return 0;
    *value=n; *end=p; return 1;
}
int IndicatorApply(struct IndicatorState *s, enum IndicatorKind kind,
                   const char *p, uint32_t now)
{
    unsigned n=0, fields=IND_FIELD_VALUE;
    const char *end;
    char event[INDICATOR_TEXT+16];
    int count;
    if (!p) return 0;
    if (kind==IND_BATTERY) return IndicatorUpdate(s,p,now);
    if ((kind==IND_NETWORK || kind==IND_BLUETOOTH || kind==IND_SOUND) && !strncmp(p,"STATUS ",7)) {
        fields=0; p+=7;
    } else if (kind==IND_NETWORK && !strncmp(p,"LINK ",5)) {
        fields=IND_FIELD_LINK;
        if (!strncmp(p,"LINK on ",8)) { n=100; p+=8; }
        else if (!strncmp(p,"LINK off ",9)) { n=0; p+=9; }
        else return 0;
    } else if (kind==IND_NETWORK || kind==IND_BLUETOOTH) {
        if (!strncmp(p,"RADIO on ",9)) { n=100; p+=9; }
        else if (!strncmp(p,"RADIO off ",10)) { n=0; p+=10; }
        else return 0;
    } else if (kind==IND_SOUND) {
        if (strncmp(p,"LEVEL ",6) || !percent(p+6,&n,&end) || *end!=' ') return 0;
        p=end+1;
    } else return 0;
    /* Limit the text before formatting; truncated state is never accepted. */
    for (size_t i=0;;++i) {
        if (i>=INDICATOR_TEXT) return 0;
        if (!p[i]) break;
    }
    count=snprintf(event,sizeof(event),"%u 0 %s",n,p);
    if (count<=0 || (size_t)count>=sizeof(event) || !IndicatorUpdate(s,event,now)) return 0;
    s->fields=fields; return 1;
}
int IndicatorTypedGet(const struct IndicatorState *s, enum IndicatorKind kind,
                      const char *command, uint32_t now, char *out, size_t size)
{
    int count;
    if (!command || !out || !size || (!IndicatorHealthy(s,now) || !(s->fields&IND_FIELD_VALUE))) return 0;
    if ((kind==IND_BATTERY && !strcmp(command,"GET PERCENT")) ||
        (kind==IND_SOUND && !strcmp(command,"GET LEVEL")))
        count=snprintf(out,size,"%u",s->value);
    else if ((kind==IND_NETWORK || kind==IND_BLUETOOTH) && !strcmp(command,"GET RADIO"))
        count=snprintf(out,size,"%s",s->value ? "on":"off");
    else return 0;
    return count>0 && (size_t)count<size;
}
int IndicatorSetParse(enum IndicatorKind kind, const char *p, unsigned *value)
{
    unsigned n;
    const char *end;
    if (!p || !value) return 0;
    if (kind==IND_NETWORK || kind==IND_BLUETOOTH) {
        if (!strcmp(p,"SET RADIO on")) { *value=100; return 1; }
        if (!strcmp(p,"SET RADIO off")) { *value=0; return 1; }
    } else if (kind==IND_SOUND && !strncmp(p,"SET LEVEL ",10) &&
               percent(p+10,&n,&end) && !*end) {
        *value=n; return 1;
    }
    return 0;
}
