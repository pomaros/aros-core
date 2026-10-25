#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Compile actual SMEditor serialization/dirty hooks with native raster cases."""
from pathlib import Path
import subprocess
import tempfile

core = Path(__file__).resolve().parents[4]
source = (core / 'workbench/prefs/screenmode/smeditor.c').read_text()
def block(text, start):
    begin = text.index(start)
    opening = text.index('{', begin)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[begin:end]
serializer = block(source, 'static BOOL Gadgets2ScreenmodePrefs')
hook = block(source, 'AROS_UFH3(IPTR, RasterChanged,')
data = block((core / 'workbench/prefs/screenmode/smeditor_data.h').read_text(), 'struct SMEditor_DATA\n') + ';'
prefix = r'''
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t ULONG; typedef uint16_t UWORD; typedef uintptr_t IPTR;
typedef void *APTR; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define D(x)
#define AROS_UFH3(result,name,a,b,c) result name(a,b,c)
#define AROS_UFHA(type,name,reg) type name
#define AROS_USERFUNC_INIT
#define AROS_USERFUNC_EXIT
struct Object {IPTR values[16];}; typedef struct Object Object;
struct Hook {void *h_Entry,*h_Data;};
struct ScaleAttempt {IPTR opaque;};
struct ScreenModePrefs {ULONG smp_Scale,smp_Reserved[3],smp_DisplayID;UWORD smp_Width,smp_Height,smp_Depth,smp_Control;} screenmodeprefs;
enum {MUIA_List_Active,MUIA_ScreenModeProperties_DisplayID,
MUIA_ScreenModeProperties_Width,MUIA_ScreenModeProperties_DefWidth,
MUIA_ScreenModeProperties_Height,MUIA_ScreenModeProperties_DefHeight,
MUIA_ScreenModeProperties_Depth,MUIA_ScreenModeProperties_Autoscroll,
MUIA_Numeric_Value,MUIA_PrefsEditor_Changed};
#define MUIV_List_Active_Off ((IPTR)-1)
#define SMF_AUTOSCROLL 1
#define XGET(object,attr) ((object)->values[attr])
#define set(object,attr,value) ((object)->values[attr]=(value))
static int defaults;
static void Prefs_Default(void){defaults++;memset(&screenmodeprefs,0,sizeof(screenmodeprefs));}
'''
main = r'''
int main(void)
{
    Object selector={0},properties={0},scale={0},editor={0};
    struct SMEditor_DATA data={0};
    struct Hook notification={NULL,&data};
    struct ScreenModePrefs raw={0};
    raw.smp_Scale=200;raw.smp_DisplayID=0x100000;raw.smp_Width=2560;raw.smp_Height=1600;raw.smp_Depth=32;
    raw.smp_Reserved[1]=0x12345678;
    data.selector=&selector;data.properties=&properties;data.scale=&scale;data.imported_prefs=raw;
    properties.values[MUIA_ScreenModeProperties_DisplayID]=0x100000;
    properties.values[MUIA_ScreenModeProperties_Width]=2560;
    properties.values[MUIA_ScreenModeProperties_DefWidth]=2560;
    properties.values[MUIA_ScreenModeProperties_Height]=1600;
    properties.values[MUIA_ScreenModeProperties_DefHeight]=1600;
    properties.values[MUIA_ScreenModeProperties_Depth]=24;
    scale.values[MUIA_Numeric_Value]=4;
    /* The real layout maps these widgets to FFFF/FFFF/24. The initial
       backup, gear preview and its Cancel export must retain2560/1600/32. */
    assert(Gadgets2ScreenmodePrefs(&data));assert(!memcmp(&screenmodeprefs,&raw,sizeof(raw)));
    scale.values[MUIA_Numeric_Value]=6;assert(Gadgets2ScreenmodePrefs(&data));
    assert(screenmodeprefs.smp_Scale==250&&screenmodeprefs.smp_Width==2560&&screenmodeprefs.smp_Height==1600&&screenmodeprefs.smp_Depth==32);
    scale.values[MUIA_Numeric_Value]=4;assert(Gadgets2ScreenmodePrefs(&data));
    assert(!memcmp(&screenmodeprefs,&raw,sizeof(raw))&&!defaults);
    /* Import-generated notifications cannot classify a raster change. */
    data.importing=TRUE;RasterChanged(&notification,&editor,NULL);
    assert(!data.raster_changed&&!editor.values[MUIA_PrefsEditor_Changed]);
    data.importing=FALSE;RasterChanged(&notification,&editor,NULL);
    assert(data.raster_changed&&editor.values[MUIA_PrefsEditor_Changed]);
    /* A real width/depth edit still takes the current mode controls. */
    properties.values[MUIA_ScreenModeProperties_Width]=1280;
    properties.values[MUIA_ScreenModeProperties_Depth]=16;
    assert(Gadgets2ScreenmodePrefs(&data));
    assert(screenmodeprefs.smp_Width==1280&&screenmodeprefs.smp_Height==65535&&screenmodeprefs.smp_Depth==16);
    assert(!data.raster_changed&&screenmodeprefs.smp_Reserved[1]==raw.smp_Reserved[1]);
    struct ScreenModePrefs once=screenmodeprefs;
    properties.values[MUIA_ScreenModeProperties_Width]=320;
    properties.values[MUIA_ScreenModeProperties_Depth]=1;
    assert(Gadgets2ScreenmodePrefs(&data));assert(!memcmp(&screenmodeprefs,&once,sizeof(once)));
    /* Import of the original backup reinstates its raw raster before the
       framework exports ENV; the now-clamped widgets do not override it. */
    data.imported_prefs=raw;data.raster_changed=FALSE;
    assert(Gadgets2ScreenmodePrefs(&data));assert(!memcmp(&screenmodeprefs,&raw,sizeof(raw)));
    selector.values[MUIA_List_Active]=MUIV_List_Active_Off;
    assert(Gadgets2ScreenmodePrefs(&data));assert(!defaults);
    puts("screenmode-raster-host PASS native32-preserved=1 gear-preview-cancel=1 import-notifications-ignored=1 real-mode-edits=1 serialize-once=1");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='screenmode-raster-') as temp:
    path = Path(temp)
    unit = path / 'test.c'
    unit.write_text(prefix + data + hook + serializer + main)
    binary = path / 'test'
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(unit), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
