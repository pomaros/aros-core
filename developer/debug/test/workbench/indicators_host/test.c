/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include "state.h"
#include "battery.h"
#include "consumers.h"
#include "subscriptions.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    struct IndicatorState s={0}, before;
    const char *bad[]={"", "1", "1 ", "1 0", "101 0 full", "-1 0 bad", "1 2 bad", "1 0 ", "1 0 line\nbreak", "1000 0 overflow", "1 0 \177"};
    int x,y;
    unsigned i;
    char tooLong[INDICATOR_TEXT+10];
    assert(!IndicatorHealthy(&s,0));
    assert(IndicatorUpdate(&s,"75 1 Charging",100));
    assert(s.value==75 && s.charging && !strcmp(s.text,"Charging"));
    assert(IndicatorHealthy(&s,4099)); assert(!IndicatorHealthy(&s,4100));
    before=s;
    for (i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        assert(!IndicatorUpdate(&s,bad[i],5000)); assert(!memcmp(&s,&before,sizeof(s)));
    }
    memset(tooLong,'x',sizeof(tooLong)); memcpy(tooLong,"1 0 ",4); tooLong[sizeof(tooLong)-1]=0;
    assert(!IndicatorUpdate(&s,tooLong,5000)); assert(!memcmp(&s,&before,sizeof(s)));
    assert(IndicatorUpdate(&s,"0 0 Discharging",UINT32_MAX-100));
    assert(IndicatorHealthy(&s,100)); assert(!IndicatorHealthy(&s,4000));
    IndicatorAnchor(-1,-4,800,600,320,100,&x,&y); assert(x==0 && y==0);
    IndicatorAnchor(799,599,800,600,320,100,&x,&y); assert(x==480 && y==500);
    IndicatorAnchor(799,599,100,50,320,100,&x,&y); assert(x==0 && y==0);
    {
        struct IndicatorBattery b={75,0,0,1,1,3000,-1000};
        char event[192];
        assert(IndicatorBatteryEvent(&b,event,sizeof(event)));
        assert(strstr(event,"3 h 00 min remaining"));
        assert(IndicatorUpdate(&s,event,10)); assert(!s.charging && s.value==75);
        b.adapter=1; b.charging=1;
        assert(IndicatorBatteryEvent(&b,event,sizeof(event)));
        assert(strstr(event,"AC, charging"));
        assert(!strstr(event,"remaining"));
        assert(IndicatorUpdate(&s,event,20)); assert(s.charging);
        b.percent=101; assert(!IndicatorBatteryEvent(&b,event,sizeof(event)));
        b.percent=75; b.remainingMah=100001; assert(!IndicatorBatteryEvent(&b,event,sizeof(event)));
        b.remainingMah=1; b.adapter=0; b.currentMa=INT32_MIN;
        assert(IndicatorBatteryEvent(&b,event,sizeof(event)));
        b.currentMa=0;
        assert(IndicatorBatteryEvent(&b,event,sizeof(event))); assert(strstr(event,"estimate unavailable"));
        assert(!IndicatorBatteryEvent(&b,event,4));
    }
    {
        struct IndicatorState typed={0}, saved;
        char value[16]; unsigned target=999;
        assert(!IndicatorTypedGet(&typed,IND_NETWORK,"GET RADIO",0,value,sizeof(value)));
        assert(!IndicatorApply(&typed,IND_NETWORK,"75 1 fabricated",1));
        assert(IndicatorApply(&typed,IND_NETWORK,"STATUS Joined; radio control unsupported",1));
        assert(IndicatorHealthy(&typed,1));
        assert(!IndicatorTypedGet(&typed,IND_NETWORK,"GET RADIO",1,value,sizeof(value)));
        assert(IndicatorApply(&typed,IND_NETWORK,"RADIO off Disabled",100));
        assert(IndicatorTypedGet(&typed,IND_NETWORK,"GET RADIO",101,value,sizeof(value)) && !strcmp(value,"off"));
        assert(!IndicatorTypedGet(&typed,IND_NETWORK,"GET RADIO",4100,value,sizeof(value)));
        assert(IndicatorApply(&typed,IND_BLUETOOTH,"RADIO on Enabled",200));
        assert(IndicatorTypedGet(&typed,IND_BLUETOOTH,"GET RADIO",200,value,sizeof(value)) && !strcmp(value,"on"));
        saved=typed;
        const char *bad[]={"RADIO unknown bad","RADIO on", "RADIO on ","RADIO off Bad\nline", "RADIO offx bad"};
        for (unsigned j=0;j<sizeof(bad)/sizeof(bad[0]);++j) {
            assert(!IndicatorApply(&typed,IND_NETWORK,bad[j],300)); assert(!memcmp(&typed,&saved,sizeof(typed)));
        }
        assert(IndicatorApply(&typed,IND_SOUND,"LEVEL 0 Muted",100));
        assert(IndicatorTypedGet(&typed,IND_SOUND,"GET LEVEL",100,value,sizeof(value)) && !strcmp(value,"0"));
        assert(!IndicatorTypedGet(&typed,IND_SOUND,"GET RADIO",100,value,sizeof(value)));
        assert(!IndicatorApply(&typed,IND_SOUND,"LEVEL 101 Too loud",200));
        assert(!IndicatorApply(&typed,IND_SOUND,"LEVEL -1 Bad",200));
        assert(!IndicatorApply(&typed,IND_SOUND,"LEVEL 10x Bad",200));
        assert(IndicatorApply(&typed,IND_BATTERY,"100 1 Full",200));
        assert(IndicatorTypedGet(&typed,IND_BATTERY,"GET PERCENT",200,value,sizeof(value)) && !strcmp(value,"100"));
        assert(!IndicatorTypedGet(&typed,IND_BATTERY,"GET PERCENT",200,value,1));
        assert(!IndicatorTypedGet(&typed,IND_BATTERY,"GET PERCENT extra",200,value,sizeof(value)));
        assert(!IndicatorSetParse(IND_BATTERY,"SET LEVEL 1",&target));
        assert(IndicatorSetParse(IND_NETWORK,"SET RADIO on",&target) && target==100);
        assert(IndicatorSetParse(IND_BLUETOOTH,"SET RADIO off",&target) && target==0);
        assert(!IndicatorSetParse(IND_NETWORK,"SET RADIO on extra",&target));
        assert(!IndicatorSetParse(IND_NETWORK,"SET RADIO ON",&target));
        assert(IndicatorSetParse(IND_SOUND,"SET LEVEL 100",&target) && target==100);
        const char *invalid[]={"SET LEVEL -1","SET LEVEL 101","SET LEVEL 1000","SET LEVEL 1 extra","SET LEVEL 1\nQUIT","SET LEVEL ","SET LEVEL 0x10"};
        for (unsigned j=0;j<sizeof(invalid)/sizeof(invalid[0]);++j) assert(!IndicatorSetParse(IND_SOUND,invalid[j],&target));
    }
    {
        struct IndicatorState link={0}, saved;
        char value[16];
        assert(IndicatorApply(&link,IND_NETWORK,"LINK on Joined",100));
        assert(link.fields==IND_FIELD_LINK && link.value==100 && IndicatorHealthy(&link,100));
        assert(!IndicatorTypedGet(&link,IND_NETWORK,"GET RADIO",100,value,sizeof(value)));
        assert(IndicatorApply(&link,IND_NETWORK,"LINK off Disconnected",100));
        assert(link.fields==IND_FIELD_LINK && link.value==0);
        saved=link;
        assert(!IndicatorApply(&link,IND_NETWORK,"LINK unknown Wrong",200));
        assert(!IndicatorApply(&link,IND_BLUETOOTH,"LINK on Wrong kind",200));
        assert(!memcmp(&link,&saved,sizeof(link)));
    }
    {
        struct IndicatorConsumers list={0};
        struct IndicatorAux aux={0}, saved;
        char text[192], longName[100];
        assert(!strcmp(IndicatorAuxText(&aux,0),"CPU data unavailable"));
        assert(IndicatorConsumersFormat(&list,text,sizeof(text)) && strstr(text,"accounting unavailable"));
        IndicatorConsumerAdd(&list,"small",UINT32_MAX/8);
        IndicatorConsumerAdd(&list,"largest",UINT32_MAX);
        IndicatorConsumerAdd(&list,"middle",UINT32_MAX/2);
        IndicatorConsumerAdd(&list,"tie",UINT32_MAX/2);
        IndicatorConsumerAdd(&list,"idle",0);
        assert(list.scanned==5 && !strcmp(list.top[0].name,"largest"));
        assert(!strcmp(list.top[1].name,"middle") && !strcmp(list.top[2].name,"tie"));
        assert(IndicatorConsumersFormat(&list,text,sizeof(text)) && strstr(text,"largest 99.99%"));
        assert(!IndicatorConsumersFormat(&list,text,1));
        memset(longName,'x',sizeof(longName)); longName[3]='\n'; longName[99]=0;
        memset(&list,0,sizeof(list)); IndicatorConsumerAdd(&list,longName,UINT32_MAX); list.truncated=1;
        assert(strlen(list.top[0].name)==31 && list.top[0].name[3]=='?');
        assert(IndicatorConsumersFormat(&list,text,sizeof(text)) && strstr(text,"partial"));
        assert(IndicatorAuxUpdate(&aux,text,UINT32_MAX-100));
        assert(!strcmp(IndicatorAuxText(&aux,100),text));
        assert(!strcmp(IndicatorAuxText(&aux,3899),"CPU data unavailable"));
        saved=aux; assert(!IndicatorAuxUpdate(&aux,"bad\nline",300)); assert(!memcmp(&aux,&saved,sizeof(aux)));
        assert(!IndicatorAuxUpdate(&aux,"",300));
        memset(longName,127,99); longName[99]=0; assert(!IndicatorAuxUpdate(&aux,longName,300));
        /* An auxiliary sample cannot renew the battery state. */
        assert(IndicatorUpdate(&s,"75 0 Battery",100));
        assert(IndicatorAuxUpdate(&aux,"CPU: worker 10.00%",4099));
        assert(!IndicatorHealthy(&s,4100));
    }
    {
        struct IndicatorSubscriber subs[IND_SUBSCRIBERS]={0};
        char port[64], bad[65];
        for (unsigned j=0;j<IND_SUBSCRIBERS;++j) {
            snprintf(port,sizeof(port),"TEST.%u",j);
            assert(IndicatorSubscribe(subs,port,0)==(int)j);
        }
        assert(IndicatorSubscribe(subs,"TEST.full",0)<0);
        assert(IndicatorSubscribe(subs,"TEST.0",1)==0); /* idempotent while borrowed */
        assert(IndicatorUnsubscribe(subs,"TEST.0"));
        assert(IndicatorSubscribe(subs,"TEST.new",1)<0); /* retired slot still borrowed */
        assert(!strcmp(subs[0].port,"TEST.0") && !subs[0].active);
        assert(IndicatorSubscribe(subs,"TEST.new",0)==0); /* ownership returned */
        assert(!IndicatorUnsubscribe(subs,"TEST.absent"));
        assert(IndicatorSubscribe(subs,"bad port",0)<0 && IndicatorSubscribe(subs,"bad\nQUIT",0)<0);
        memset(bad,'x',64); bad[64]=0; assert(IndicatorSubscribe(subs,bad,0)<0);
        assert(IndicatorSubscriptionDue(&subs[0],100,0));
        assert(!IndicatorSubscriptionDue(&subs[0],100,1));
        subs[0].sent=UINT32_MAX-100; subs[0].tried=1;
        assert(!IndicatorSubscriptionDue(&subs[0],148,0));
        assert(IndicatorSubscriptionDue(&subs[0],149,0));
        subs[0].dirty=0;
        assert(!IndicatorSubscriptionDue(&subs[0],1898,0));
        assert(IndicatorSubscriptionDue(&subs[0],1899,0));
        for (unsigned j=0;j<7;++j) { IndicatorSubscriptionMissing(&subs[0]); assert(subs[0].active); }
        assert(IndicatorSubscribe(subs,"TEST.new",0)==0 && subs[0].failures==0);
        for (unsigned j=0;j<8;++j) IndicatorSubscriptionMissing(&subs[0]);
        assert(!subs[0].active && !subs[0].dirty);
        assert(!IndicatorSubscriptionDue(&subs[0],4000,0));
        assert(IndicatorSubscribe(subs,"TEST.reused",1)<0); /* quarantine still wins */
        assert(IndicatorSubscribe(subs,"TEST.reused",0)==0);
    }
    puts("{\"result\":\"PASS\",\"parser_negatives\":12,\"lease_boundary\":true,\"clock_wrap\":true,\"anchor_edges\":true,\"typed_contracts\":true,\"consumers\":true,\"subscriptions\":true}");
    return 0;
}
