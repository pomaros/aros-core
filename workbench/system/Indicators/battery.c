/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1 */
#include "battery.h"
#include <stdio.h>
int IndicatorBatteryEvent(const struct IndicatorBattery *b, char *out, size_t size)
{
    char estimate[64]="estimate unavailable";
    int length;
    if (!out || !size || b->percent>100 || b->adapter>1 || b->charging>1 ||
        (b->capacityValid && b->remainingMah>100000U)) return 0;
    if (!b->adapter && b->capacityValid && b->currentValid && b->currentMa<0) {
        uint64_t minutes=(uint64_t)b->remainingMah*60/(uint64_t)(-(int64_t)b->currentMa);
        if (minutes<100000)
            snprintf(estimate,sizeof(estimate),"%lu h %02lu min remaining",
                     (unsigned long)(minutes/60),(unsigned long)(minutes%60));
    }
    length=snprintf(out,size,"%u %u %u%%, %s%s; %s",b->percent,b->adapter,b->percent,
                    b->adapter?"AC":"battery",b->charging?", charging":"",estimate);
    return length>0 && (size_t)length<size;
}
