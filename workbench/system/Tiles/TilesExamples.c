/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <workbench/workbench.h>
#include <intuition/intuition.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <stdio.h>

/* PutDiskObject serializes real image and ToolTypes data, not a text .info. */
static BOOL example(const char *path, STRPTR *types)
{
    struct DiskObject icon = {0};
    struct Image image = {0};
    UWORD bits[64];
    icon.do_Magic = 0xe310; icon.do_Version = 1; icon.do_Type = WBTOOL;
    icon.do_CurrentX = icon.do_CurrentY = NO_ICON_POSITION;
    icon.do_Gadget.Width = icon.do_Gadget.Height = 32;
    icon.do_Gadget.Flags = GFLG_GADGIMAGE; icon.do_Gadget.GadgetRender = &image;
    image.Width = image.Height = 32; image.Depth = 1; image.PlanePick = 1;
    image.ImageData = bits;
    for (unsigned y=0; y<32; ++y) bits[y*2] = bits[y*2+1] = (y/4)&1 ? 0x0f0f : 0xf0f0;
    icon.do_ToolTypes = types;
    return PutDiskObject((STRPTR)path, &icon);
}
int main(int argc, char **argv)
{
    char eyes[256], weather[256];
    const char *directory = argc > 1 ? argv[1] : "RAM:";
    STRPTR eyesTypes[] = {"TILE=eyes", "SIZE=2x1", "LABEL=Eyes", "HOVER=FAST", NULL};
    STRPTR weatherTypes[] = {"TILE=weather", "SIZE=2x2", "LABEL=Rome (offline demo)",
                             "ENV=TilesWeatherDemo", "EVERY=60", NULL};
    if (argc > 2 || snprintf(eyes,sizeof(eyes),"%s",directory) >= (int)sizeof(eyes) ||
        snprintf(weather,sizeof(weather),"%s",directory) >= (int)sizeof(weather) ||
        !AddPart(eyes,"Eyes",sizeof(eyes)) || !AddPart(weather,"WeatherOffline",sizeof(weather))) {
        fprintf(stderr,"Usage: TilesExamples [existing destination drawer]\n"); return RETURN_ERROR;
    }
    if (!SetVar("TilesWeatherDemo","18|cloudy",9,GVF_GLOBAL_ONLY) ||
        !example(eyes,eyesTypes) || !example(weather,weatherTypes)) {
        fprintf(stderr,"TilesExamples: creation failed (DOS error %ld); remove partial examples before retry\n",(long)IoErr());
        return RETURN_ERROR;
    }
    printf("Created %s.info and %s.info. Weather is an OFFLINE sample, not live data.\n",eyes,weather);
    return RETURN_OK;
}
