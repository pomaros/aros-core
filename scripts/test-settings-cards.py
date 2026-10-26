#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Run production result-card ownership against a recording MUI backend."""
from pathlib import Path
import subprocess
import tempfile
source=(Path(__file__).resolve().parents[1]/'workbench/prefs/settings/settingswindow.c').read_text()
start=source.index('static void clear_results(Object *self, struct SettingsWindow_DATA *data)\n{')
focus_start=source.index('static void release_focus(Object *self, struct SettingsWindow_DATA *data, Object *leaving)\n{')
body=source[focus_start:source.index('\nstatic void show(',focus_start)]+source[start:source.index('/*** Methods',start)]
fixture=r'''#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned long ULONG; typedef uintptr_t IPTR; typedef long LONG;
typedef int BOOL; typedef void Class;
#define TRUE 1
#define FALSE 0
#define CAT_ROWS 3
#define CAT_NAME 64
#define MUIM_Group_InitChange 1
#define MUIM_Group_ExitChange 2
#define OM_ADDMEMBER 3
#define OM_REMMEMBER 4
#define MUIA_ShowMe 100
#define MUIA_Scrollgroup_Contents 101
#define MUIA_FrameTitle 102
#define MUIA_Text_Contents 103
#define MUIA_Window_ActiveObject 104
#define MUIA_Parent 106
#define GroupFrame 105, 1
#define ScrollgroupObject new_object(1
#define VirtgroupObject new_object(2
#define TextObject new_object(3
#define VGroup new_object(5
#define End 0)
struct Object { struct Object *parent, *active; int kind, show, setups, cleanups, disposed; char text[256]; };
typedef struct Object Object;
struct catalogue { unsigned row_count; struct { char name[32]; } rows[CAT_ROWS]; };
struct SettingsWindow_DATA {
 Object *swd_Results,*swd_ResultBody,*swd_Shown,*swd_Content,*swd_NoResults;
 Object *swd_Card[CAT_ROWS],*swd_RowPage[CAT_ROWS];
 Object *swd_Title,*swd_Description,*swd_Search;
 struct catalogue *swd_Catalogue; char swd_Query[CAT_NAME]; LONG swd_ResultAt;
};
static Object objects[32]; static unsigned used;
static Object *window_under_test;
static Object *new_object(int kind,...) {
 assert(used<32);Object *o=&objects[used++];o->kind=kind;o->show=1;
 va_list ap;va_start(ap,kind);int tag;
 while((tag=va_arg(ap,int))) {
  IPTR v=va_arg(ap,IPTR);
  if(tag==MUIA_Scrollgroup_Contents)((Object *)v)->parent=o;
 }
 va_end(ap);return o;
}
static IPTR DoMethod(Object *parent,int method,...) {
 va_list ap;va_start(ap,method);
 if(method==OM_ADDMEMBER || method==OM_REMMEMBER) {
  Object *child=(Object *)va_arg(ap,IPTR);
  if(method==OM_ADDMEMBER) {assert(!child->parent);child->parent=parent;if(child->kind==4)++child->setups;}
  else {
   for(Object *o=window_under_test->active;o;o=o->parent)assert(o!=child);
   assert(child->parent==parent);child->parent=NULL;if(child->kind==4)++child->cleanups;}
 }
 va_end(ap);return 1;
}
static void set(Object *o,int attr,IPTR value) {
 if(attr==MUIA_Window_ActiveObject)o->active=(Object *)value;
 if(attr==MUIA_ShowMe)o->show=!!value;
 if(attr==MUIA_Text_Contents)snprintf(o->text,sizeof(o->text),"%s",(char *)value);
}
#define SET(o,a,v) set(o,a,(IPTR)(v))
static IPTR get(Object *o,int attr){if(attr==MUIA_Window_ActiveObject)return (IPTR)o->active;if(attr==MUIA_Parent)return (IPTR)o->parent;assert(attr==MUIA_ShowMe);return o->show;}
#define XGET(o,a) get(o,a)
static void MUI_DisposeObject(Object *o) {assert(o->kind!=4);assert(!o->parent);o->disposed=1;}
static int row_visible(struct SettingsWindow_DATA *d,ULONG row,const char *q) {
 return q[0]=='a'?row==0:q[0]=='b'?row<=1:0;
}
static Object *make_row_page(Class *cl,Object *self,struct SettingsWindow_DATA *d,ULONG row) {return new_object(4,0);}
static void filter_row(struct SettingsWindow_DATA *d,ULONG row) {}
''' + body + r'''
int main(void) {
 struct catalogue c={.row_count=3};struct SettingsWindow_DATA d={.swd_Catalogue=&c};
 Object window={0};window_under_test=&window;d.swd_Content=new_object(0,0);d.swd_Title=new_object(0,0);
 d.swd_Description=new_object(0,0);d.swd_Search=new_object(0,0);
 strcpy(d.swd_Query,"a");show_results(NULL,&window,&d);
 Object *first=d.swd_Card[0],*editor=d.swd_RowPage[0];assert(editor->setups==1);
 strcpy(d.swd_Query,"b");show_results(NULL,&window,&d);
 assert(d.swd_Card[0]==first && editor->setups==1 && editor->cleanups==0);
 assert(d.swd_RowPage[1]->setups==1 && !d.swd_RowPage[2]);
 strcpy(d.swd_Query,"z");show_results(NULL,&window,&d);
 assert(!first->show && d.swd_NoResults->show && strstr(d.swd_NoResults->text,"'z'"));
 strcpy(d.swd_Query,"a");show_results(NULL,&window,&d);
 assert(first->show && d.swd_Card[0]==first && editor->setups==1 && editor->cleanups==0);
 window.active=editor;clear_results(&window,&d);assert(window.active==d.swd_Search);assert(editor->cleanups==1 && !editor->disposed && !editor->parent);
 assert(d.swd_RowPage[1]->cleanups==1 && !d.swd_RowPage[1]->disposed);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    c=Path(directory)/'cards.c';binary=Path(directory)/'cards'
    c.write_text(fixture)
    subprocess.run(['clang','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('settings-cards result=PASS queries=4 editor-setup=once no-unmatched-create=1 borrowed-ownership=1 focus-before-detach=1')
