#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual Settings Cancel body must preserve the UI after any editor refuses."""
from pathlib import Path
import subprocess
import tempfile
core = Path(__file__).resolve().parents[4]
source = (core / 'workbench/prefs/settings/settingswindow.c').read_text()
start = source.index('static IPTR SettingsWindow__MUIM_PrefsWindow_Cancel')
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
actual = source[start:end]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uintptr_t IPTR;
typedef void Class;
typedef void *Msg;
typedef struct Object { int unused; } Object;
struct SettingsWindow_DATA { int unused; } data_instance;
#define SETUP_INST_DATA struct SettingsWindow_DATA *data=&data_instance
#define TRUE 1
#define FALSE 0
#define MUIM_PrefsEditor_Cancel 1
#define MUIM_Application_ReturnID 2
#define MUIV_Application_ReturnID_Quit 3
static int refusal, quits;
static int each_editor(struct SettingsWindow_DATA *data,int method,int all) {
    assert(data==&data_instance&&method==MUIM_PrefsEditor_Cancel&&all==TRUE);
    return !refusal;
}
#define _app(object) (object)
static void DoMethod(Object *object,int method,int id) {
    assert(object&&method==MUIM_Application_ReturnID&&id==MUIV_Application_ReturnID_Quit);
    quits++;
}
'''
suffix = r'''
int main(void) {
    Object object={0};
    refusal=1;
    assert(!SettingsWindow__MUIM_PrefsWindow_Cancel(NULL,&object,NULL));
    assert(quits==0);
    refusal=0;
    assert(SettingsWindow__MUIM_PrefsWindow_Cancel(NULL,&object,NULL));
    assert(quits==1);
    puts("settings-cancel-host PASS actual-caller refusal-keeps-open=1 success-quits=1");
}
'''
with tempfile.TemporaryDirectory(prefix='settings-cancel-') as temp:
    path=Path(temp)
    unit=path/'fixture.c'
    binary=path/'fixture'
    for name, body in [('production',actual),('ignore-refusal',actual.replace('if (!each_editor(data, MUIM_PrefsEditor_Cancel, TRUE))\n        return FALSE;', 'each_editor(data, MUIM_PrefsEditor_Cancel, TRUE);'))]:
        unit.write_text(prefix+body+suffix)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],text=True,capture_output=True)
        if name=='production':
            assert result.returncode==0,result.stderr
            print(result.stdout,end='')
        else:
            assert result.returncode!=0,'refusal mutant escaped'
    print('settings-cancel-host mutants=1 rejected=1')
