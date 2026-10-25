#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Exercise the editor's actual store helper with a bounded settings-library double."""
from pathlib import Path
import subprocess
import tempfile
import runpy

core = Path(__file__).resolve().parents[4]
source = core / 'workbench/prefs/screenmode/screensettings.h'
support=runpy.run_path(str(core/'developer/debug/test/intuition/scalediagnostics_host.py'),run_name='scale_diagnostics_support')
helper = '\n'.join(line for line in source.read_text().splitlines() if not line.startswith('#include'))
framework = (core / 'workbench/classes/zune/prefseditor/prefseditor.c').read_text()
def actual_function(name, source=None):
    global framework
    original = framework
    if source is not None: framework = source
    start = framework.index(('static BOOL ' if name=='ScreenmodePrefs2Gadgets' else 'IPTR ') + name)
    opening = framework.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (framework[end] == '{') - (framework[end] == '}')
        end += 1
    result = framework[start:end] + '\n'
    framework = original
    return result
framework_methods = ''.join(actual_function('PrefsEditor__MUIM_PrefsEditor_' + name) for name in ('Revert','Cancel','Use','Save'))
editor_source = (core / 'workbench/prefs/screenmode/smeditor.c').read_text()
editor_methods = '\n'.join(actual_function('SMEditor__' + name, editor_source).replace('static IPTR ', 'IPTR ') for name in ('MUIM_Setup','MUIM_PrefsEditor_Cancel','MUIM_PrefsEditor_Revert'))
editor_methods = actual_function('ScreenmodePrefs2Gadgets', editor_source) + support['function'](editor_source,'ScreenSettings_ScaleNotice') + support['function'](editor_source,'ScreenSettings_ScaleSync') + support['function'](editor_source,'ScreenSettings_ScaleFailure') + editor_methods
prefix = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
typedef uint8_t UBYTE;
typedef uint16_t UWORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uintptr_t IPTR;
typedef void *APTR;
typedef int BOOL;
typedef const char *CONST_STRPTR;
typedef struct Editor Object;
#define TRUE 1
#define FALSE 0
#define SETTING_LAYER_APPLIED 0
#define SETTING_LAYER_USER 1
#define SETTING_LAYER_MACHINE 2
#define SETTING_Layer 1
#define SETTING_ReadOnly 2
#define TAG_DONE 0
#define SETTING_OK 0
#define SETTING_ERROR_NO_FILE 3
#define SETTING_ERROR_FORMAT 4
#define SETTING_ERROR_UNSUPPORTED 8
#define ID_PRHD 0x50524844
#define ID_SCRM 0x5343524d
#define PHV_CURRENT 0
#define AROS_LONG2BE __builtin_bswap32
#define AROS_BE2LONG __builtin_bswap32
#define AROS_WORD2BE __builtin_bswap16
#define AROS_BE2WORD __builtin_bswap16
#define AROS_UFH3(result,name,a,b,c) result name(a,b,c)
#define AROS_UFHA(type,name,reg) type name
#define AROS_USERFUNC_INIT
#define AROS_USERFUNC_EXIT
#define Stricmp strcasecmp
struct Library { unsigned lib_Version,lib_Revision; } library;
static struct Library intuition_library={50,20};static struct Library *IntuitionBase=&intuition_library;
static unsigned pub_locks,scale_notices;
struct Screen { int unused; } live_screen;
static ULONG live_scale=100;
static BOOL refuse_scale, no_screen, bad_readback;
static BOOL auto_apply=TRUE;
#define TAG_USER 0x80000000U
SCALE_RESULT_CONTRACT
static struct Screen *LockPubScreen(const char *name) { assert(!strcmp(name,"Workbench")); if(no_screen)return NULL;pub_locks++;return &live_screen; }
static void UnlockPubScreen(void *unused, struct Screen *screen) { (void)unused;assert(screen==&live_screen&&pub_locks);pub_locks--; }
static BOOL GetAttr(ULONG attr, void *screen, IPTR *value) { assert(attr==SA_Scale&&screen==&live_screen); *value=live_scale; return !bad_readback; }
static IPTR SetAttrs(void *screen, ULONG tag, ...) {
 if(screen==&live_screen){
  va_list args;va_start(args,tag);IPTR wanted=0;struct ScreenScaleResult *result=NULL;
  while(tag!=TAG_DONE){IPTR value=va_arg(args,IPTR);if(tag==SA_Scale)wanted=value;else{assert(tag==SA_ScaleResult);result=(void*)value;}tag=va_arg(args,ULONG);}va_end(args);
  assert(result&&pub_locks);result->ssr_RequestedScale=wanted;
  result->ssr_Status=refuse_scale?SSR_FAILED:SSR_SUCCESS;
  if(!refuse_scale)live_scale=wanted;
  /* bad_readback injects both unavailable GetAttr and a malformed/missing
   * applied value; a valid explicit receipt can otherwise verify restoration. */
  result->ssr_AppliedScale=bad_readback?0:live_scale;return !refuse_scale;
 }
 return TRUE;
}

struct ScreenModePrefs { ULONG smp_Scale,smp_Reserved[3],smp_DisplayID; UWORD smp_Width,smp_Height,smp_Depth,smp_Control; } screenmodeprefs;
struct Hook;
typedef IPTR (*HOOKFUNC)(struct Hook *,APTR,APTR);
struct Hook { HOOKFUNC h_Entry; APTR h_Data; };
struct TagItem { ULONG ti_Tag; IPTR ti_Data; };
struct SettingChunk { ULONG sc_ID,sc_Size; APTR sc_Data; };
struct SettingReplaceMessage { const char *srm_File; ULONG srm_Layer; struct SettingChunk *srm_Chunks; ULONG srm_Count,srm_Capacity; };
struct Image { struct SettingChunk chunks[8]; UBYTE bytes[8][64]; ULONG count; } images[3];
static int readonly_calls, write_calls;
static BOOL fail_write, fail_setup;
static ULONG last_layer;
static struct Library *OpenLibrary(const char *name, ULONG version) { assert(!strcmp(name,"settings.library")&&version==1); return &library; }
static void CloseLibrary(struct Library *base) { assert(base==&library); }
static void clone(struct Image *to, const struct SettingReplaceMessage *message)
{
    ULONG i;
    to->count=message->srm_Count;
    for(i=0;i<message->srm_Count;i++) {
        assert(message->srm_Chunks[i].sc_Size<=64);
        to->chunks[i]=message->srm_Chunks[i];
        memcpy(to->bytes[i],message->srm_Chunks[i].sc_Data,message->srm_Chunks[i].sc_Size);
        to->chunks[i].sc_Data=to->bytes[i];
    }
}
static LONG ReplaceSettingsA(const char *file, struct Hook *hook, const struct TagItem *tags)
{
    struct SettingChunk chunks[8];
    struct SettingReplaceMessage message;
    ULONG layer=tags[0].ti_Data;
    BOOL readonly=tags[1].ti_Data;
    struct Image *image=&images[layer];
    assert(!strcmp(file,"screenmode")&&tags[0].ti_Tag==SETTING_Layer&&tags[1].ti_Tag==SETTING_ReadOnly);
    last_layer=layer;
    if(!readonly&&fail_write)return SETTING_ERROR_FORMAT;
    if(readonly&&!image->count) return SETTING_ERROR_NO_FILE;
    memcpy(chunks,image->chunks,sizeof(chunks));
    message=(struct SettingReplaceMessage){file,layer,chunks,image->count,8};
    IPTR accepted=hook->h_Entry(hook,&message,NULL);
    if(readonly){readonly_calls++;return SETTING_OK;}
    if(!accepted)return SETTING_ERROR_FORMAT;
    clone(image,&message);
    if(layer!=SETTING_LAYER_APPLIED)clone(&images[SETTING_LAYER_APPLIED],&message);
    for(ULONG i=0;i<message.srm_Count;i++)if(message.srm_Chunks[i].sc_ID==ID_SCRM){
        struct ScreenModePrefs disk;
        memcpy(&disk,message.srm_Chunks[i].sc_Data,sizeof(disk));
        if(auto_apply)live_scale=AROS_BE2LONG(disk.smp_Scale);
    }
    write_calls++;
    return SETTING_OK;
}
'''
framework_prefix = r'''
struct SMEditor_DATA { Object *scale,*selector,*properties,*attributes; struct ScreenModePrefs imported_prefs; BOOL raster_changed,importing; IPTR initial_live_scale; BOOL initial_live_captured, setup_complete; BOOL gesture_held, pending_failure; };
struct Editor { LONG ped_BackupFH; BOOL changed, testing, loaded; struct SMEditor_DATA screen; };
typedef int Class;
typedef void *Msg;
#define SETUP_INST_DATA struct Editor *data=self
#define ENV ((IPTR)"ENV:")
#define ENVARC ((IPTR)"ENVARC:")
#define OFFSET_BEGINNING -1
#define MUIM_PrefsEditor_ImportFH 1
#define MUIM_PrefsEditor_ExportToDirectory 2
#define MUIM_PrefsEditor_Revert 3
#define MUIM_PrefsEditor_Use 6
#define MUIA_PrefsEditor_Changed 4
#define MUIA_PrefsEditor_Testing 5
#define SET(object,attr,value) ((attr)==MUIA_PrefsEditor_Testing ? ((object)->testing=(value)) : ((object)->changed=(value)))
#define XGET(object,attr) ((void)(attr),(object)->testing)
static LONG Seek(LONG fh, LONG offset, LONG mode){assert(fh==42&&offset==0&&mode==OFFSET_BEGINNING);return 0;}
static IPTR DoMethod(Object *object, ULONG method, ...);
static struct ScreenModePrefs backup;
'''
framework_suffix = r'''
static IPTR DoMethod(Object *object, ULONG method, ...)
{
    va_list args;
    IPTR arg;
    if(method==MUIM_PrefsEditor_Use)return PrefsEditor__MUIM_PrefsEditor_Use(NULL,object,NULL);
    if(method==MUIM_PrefsEditor_Revert)return PrefsEditor__MUIM_PrefsEditor_Revert(NULL,object,NULL);
    va_start(args,method);arg=va_arg(args,IPTR);va_end(args);
    if(method==MUIM_PrefsEditor_ImportFH){assert(arg==42);ScreenSettings_Decode(&backup);return TRUE;}
    if(method==MUIM_PrefsEditor_ExportToDirectory){return ScreenSettings_Transfer(ScreenSettings_DirectoryLayer((char*)arg),TRUE)==SETTING_OK;}
    assert(!"unexpected framework method");return FALSE;
}
'''

editor_prefix = r"""
#undef SETUP_INST_DATA
#define SETUP_INST_DATA struct SMEditor_DATA *data=&self->screen
struct MUIP_Setup { int unused; };
#define D(x)
#define SMF_AUTOSCROLL 1
#define MUIA_ScreenModeSelector_Active 10
#define MUIA_NoNotify 11
#define MUIA_ScreenModeProperties_DisplayID 12
#define MUIA_ScreenModeProperties_Width 13
#define MUIA_ScreenModeProperties_Height 14
#define MUIA_ScreenModeProperties_Depth 15
#define MUIA_ScreenModeProperties_Autoscroll 16
#define MUIA_ScreenModeAttributes_DisplayID 17
#define MUIA_Numeric_Value 9
#define MADF_DRAWOBJECT 0
#define NNSET(object,attr,value) ((void)(object),(void)(attr),(void)(value))
static void MUI_Redraw(Object *object, ULONG flags){(void)object;(void)flags;assert(!pub_locks);}
#define muiGlobalInfo(o) ((void)(o),1)
#define muiRenderInfo(o) ((void)(o),1)
#define _app(o) (o)
#define _win(o) (o)
static IPTR MUI_RequestA(Object *app,Object *window,ULONG flags,const char *title,const char *buttons,const char *format,IPTR *args){assert(!pub_locks&&!strcmp(format,"%s")&&((char*)args[0])[0]);scale_notices++;return 1;}
static BOOL ScreenmodePrefs2Gadgets(struct SMEditor_DATA *data);
static IPTR DoSuperMethodA(Class *class, Object *object, Msg message)
{
    (void)class;
    if(message==(Msg)2) return PrefsEditor__MUIM_PrefsEditor_Revert(NULL,object,NULL);
    assert(message==(Msg)1);
    if(fail_setup)return FALSE;
    if(!object->loaded){
        assert(ScreenSettings_Transfer(SETTING_LAYER_APPLIED,FALSE)==SETTING_OK);
        assert(ScreenmodePrefs2Gadgets(&object->screen));
        ScreenSettings_Encode(&backup);object->loaded=TRUE;
    }
    return TRUE;
}
"""

suffix = r'''
static void check_scale(struct Image *image, unsigned scale)
{
    ULONG i;
    for(i=0;i<image->count;i++)if(image->chunks[i].sc_ID==ID_SCRM){
        struct ScreenModePrefs disk;
        memcpy(&disk,image->chunks[i].sc_Data,sizeof(disk));
        assert(AROS_BE2LONG(disk.smp_Scale)==scale);
        assert(AROS_BE2LONG(disk.smp_DisplayID)==0x50001000);
        assert(AROS_BE2WORD(disk.smp_Width)==2560&&AROS_BE2WORD(disk.smp_Height)==1600);
        return;
    }
    assert(!"SCRM missing");
}
int main(void)
{
    UBYTE foreign[]={0x31,0x72,0x99};
    assert(sizeof(struct ScreenModePrefs)==28);
    assert(ScreenSettings_DirectoryLayer("ENV:")==SETTING_LAYER_APPLIED);
    assert(ScreenSettings_DirectoryLayer("envarc:")==SETTING_LAYER_USER);
    assert(ScreenSettings_DirectoryLayer("RAM:")==((ULONG)~0));
    screenmodeprefs.smp_Scale=125;
    screenmodeprefs.smp_DisplayID=0x50001000;
    screenmodeprefs.smp_Width=2560;screenmodeprefs.smp_Height=1600;screenmodeprefs.smp_Depth=32;
    images[SETTING_LAYER_USER].count=1;
    images[SETTING_LAYER_USER].chunks[0]=(struct SettingChunk){0x464f524e,3,foreign};
    assert(ScreenSettings_Transfer(SETTING_LAYER_USER,TRUE)==SETTING_OK);
    assert(last_layer==SETTING_LAYER_USER);
    assert(images[SETTING_LAYER_USER].count==3);
    check_scale(&images[SETTING_LAYER_USER],125);
    screenmodeprefs.smp_Scale=200;
    assert(ScreenSettings_Transfer(ScreenSettings_DirectoryLayer("ENVARC:"),TRUE)==SETTING_OK);
    check_scale(&images[SETTING_LAYER_USER],200);
    check_scale(&images[SETTING_LAYER_APPLIED],200);
    assert(!memcmp(images[SETTING_LAYER_USER].chunks[0].sc_Data,foreign,3));
    screenmodeprefs.smp_Scale=100;
    assert(ScreenSettings_Transfer(SETTING_LAYER_APPLIED,TRUE)==SETTING_OK);
    check_scale(&images[SETTING_LAYER_USER],200);
    check_scale(&images[SETTING_LAYER_APPLIED],100);
    screenmodeprefs.smp_Scale=999;
    assert(ScreenSettings_Transfer(SETTING_LAYER_USER,FALSE)==SETTING_OK);
    assert(screenmodeprefs.smp_Scale==200);
    assert(ScreenSettings_Transfer(SETTING_LAYER_APPLIED,FALSE)==SETTING_OK);
    assert(screenmodeprefs.smp_Scale==100);
    assert(write_calls==3&&readonly_calls==2);
    /* Failed Setup must not freeze a preview baseline. Test both directions
       so accidentally restoring a fixed/default value cannot satisfy it. */
    for(int reversed=0;reversed<2;reversed++) {
        struct Editor retry={.ped_BackupFH=42};
        live_scale=reversed?200:150;fail_setup=TRUE;
        assert(!SMEditor__MUIM_Setup(NULL,&retry,(Msg)1));
        assert(!retry.screen.initial_live_captured&&!retry.loaded);
        live_scale=reversed?150:200;fail_setup=FALSE;
        assert(SMEditor__MUIM_Setup(NULL,&retry,(Msg)1));
        assert(retry.screen.initial_live_scale==(reversed?150:200));
        live_scale=250;retry.testing=retry.changed=TRUE;
        assert(SMEditor__MUIM_PrefsEditor_Cancel(NULL,&retry,NULL));
        assert(live_scale==(reversed?150:200)&&!retry.testing&&!retry.changed);
        check_scale(&images[SETTING_LAYER_APPLIED],100);
        check_scale(&images[SETTING_LAYER_USER],200);
    }
    /* APPLIED100 / USER200 / liveTEMP150: initial snapshot is APPLIED,
       while repeated Setup must not replace the original live transaction. */
    live_scale=150;
    struct Editor editor={.ped_BackupFH=42};
    assert(SMEditor__MUIM_Setup(NULL,&editor,(Msg)1));
    assert(editor.screen.initial_live_scale==150);
    assert(AROS_BE2LONG(backup.smp_Scale)==100);
    live_scale=200;editor.testing=TRUE;editor.changed=TRUE;
    fail_setup=TRUE;assert(!SMEditor__MUIM_Setup(NULL,&editor,(Msg)1));
    assert(editor.screen.initial_live_captured&&editor.screen.initial_live_scale==150);
    fail_setup=FALSE;
    assert(SMEditor__MUIM_Setup(NULL,&editor,(Msg)1));
    assert(editor.screen.initial_live_scale==150);
    int previous_writes=write_calls;
    assert(SMEditor__MUIM_PrefsEditor_Cancel(NULL,&editor,NULL));
    assert(live_scale==150&&!editor.testing&&!editor.changed);
    assert(write_calls==previous_writes);
    check_scale(&images[SETTING_LAYER_APPLIED],100);
    check_scale(&images[SETTING_LAYER_USER],200);
    /* Failed restore keeps transaction pending and both layers untouched. */
    for(int fault=0;fault<3;fault++) {
        live_scale=200;editor.testing=editor.changed=TRUE;
        refuse_scale=fault==0;no_screen=fault==1;bad_readback=fault==2;
        assert(!SMEditor__MUIM_PrefsEditor_Cancel(NULL,&editor,NULL));
        assert(editor.testing&&editor.changed&&write_calls==previous_writes);
        refuse_scale=no_screen=bad_readback=FALSE;
    }
    /* Revert really applies APPLIED100 even when rewriting identical bytes
       would not generate a file notification. */
    live_scale=200;auto_apply=FALSE;
    assert(SMEditor__MUIM_PrefsEditor_Revert(NULL,&editor,(Msg)2));
    assert(live_scale==100&&!editor.testing&&!editor.changed);
    refuse_scale=TRUE;live_scale=200;
    assert(!SMEditor__MUIM_PrefsEditor_Revert(NULL,&editor,(Msg)2));
    assert(editor.testing&&editor.changed);
    refuse_scale=FALSE;
    fail_write=TRUE;live_scale=200;
    assert(!SMEditor__MUIM_PrefsEditor_Revert(NULL,&editor,(Msg)2));
    assert(live_scale==200&&editor.testing&&editor.changed);
    fail_write=FALSE;
    assert(!memcmp(images[SETTING_LAYER_APPLIED].chunks[0].sc_Data,foreign,3));
    /* Traditional Use and Save remain the actual framework bodies. */
    screenmodeprefs.smp_Scale=175;
    assert(PrefsEditor__MUIM_PrefsEditor_Use(NULL,&editor,NULL));
    check_scale(&images[SETTING_LAYER_APPLIED],175);
    check_scale(&images[SETTING_LAYER_USER],200);
    screenmodeprefs.smp_Scale=225;
    assert(PrefsEditor__MUIM_PrefsEditor_Save(NULL,&editor,NULL));
    check_scale(&images[SETTING_LAYER_APPLIED],225);
    check_scale(&images[SETTING_LAYER_USER],225);
    assert(!memcmp(images[SETTING_LAYER_USER].chunks[0].sc_Data,foreign,3));
    /* Generic editors still run their unchanged generic Cancel/Revert. */
    editor.testing=TRUE;ScreenSettings_Encode(&backup);
    screenmodeprefs.smp_Scale=250;
    assert(PrefsEditor__MUIM_PrefsEditor_Cancel(NULL,&editor,NULL));
    assert(screenmodeprefs.smp_Scale==225);
    puts("screenmode-store-host PASS actual-local-Cancel=1 applied100-user200-TEMP150=1 unchanged-layer-writes=1 foreign-chunks=1 refusal=3 setup-once=1 explicit-Revert=1 Use-Save=1 generic-framework=1");
    return 0;
}
'''
prefix=prefix.replace('SCALE_RESULT_CONTRACT',support['contract']())
helper=support['shared']()+helper

with tempfile.TemporaryDirectory(prefix='screenmode-store-') as temp:
    path = Path(temp)
    unit = path / 'fixture.c'
    unit.write_text(prefix + helper + framework_prefix + framework_methods + framework_suffix + editor_prefix + editor_methods + suffix)
    binary = path / 'fixture'
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(unit), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    mutations = [
        ('capture-before-success', editor_methods.replace('    if (!DoSuperMethodA(CLASS, self, (Msg)message)) return FALSE;', '    if (!data->initial_live_captured) { data->initial_live_scale=scale;data->initial_live_captured=TRUE; }\n    if (!DoSuperMethodA(CLASS, self, (Msg)message)) return FALSE;')),
        ('skip-live-restore', editor_methods.replace('!ScreenSettings_SetLiveScale(data->initial_live_scale, &attempt)', 'FALSE')),
        ('recapture-preview', editor_methods.replace('if (!data->initial_live_captured)', 'if (TRUE)')),
        ('write-APPLIED-on-Cancel', editor_methods.replace('(void)message;', '(void)message; ScreenSettings_Transfer(SETTING_LAYER_APPLIED,TRUE);')),
        ('backup-TEMP-overlay', editor_methods.replace('data->imported_prefs = screenmodeprefs;', 'screenmodeprefs.smp_Scale=150; data->imported_prefs = screenmodeprefs;')),
        ('no-Revert-apply', editor_methods.replace('!ScreenSettings_SetLiveScale(screenmodeprefs.smp_Scale, &attempt)', 'FALSE')),
    ]
    for name, mutated in mutations:
        unit.write_text(prefix + helper + framework_prefix + framework_methods + framework_suffix + editor_prefix + mutated + suffix)
        subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(unit), '-o', str(binary)], check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        assert result.returncode != 0, name + ' escaped'
    print('screenmode-store-host mutants=6 rejected=6 failed-first-Setup-both-directions=1')
