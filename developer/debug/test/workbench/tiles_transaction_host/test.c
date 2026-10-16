/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1 */
#include "tile_transaction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
struct Context {unsigned calls,fail,undoFail;};
static int present(const char *p){struct stat s;return !stat(p,&s);}
static void put(const char *p,const char *text){FILE *f=fopen(p,"wx");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));}
static void same(const char *p,const char *expected){char s[64]={0};FILE*f=fopen(p,"r");assert(f);assert(fread(s,1,sizeof(s)-1,f)==strlen(expected));fclose(f);assert(!strcmp(s,expected));}
static int move(void *v,const char *from,const char *to){struct Context *c=v;++c->calls;if(c->calls==c->fail||c->calls==c->undoFail)return 0;if(present(to))return 0;return !rename(from,to);}
static void plan(struct TileRenamePlan *p){memset(p,0,sizeof(*p));assert(TileRenameAdd(p,"Alpha.info","Alpha.backup"));assert(TileRenameAdd(p,"Beta.info","Beta.backup"));assert(TileRenameAdd(p,"stage","Group"));assert(TileRenameAdd(p,"stage.info","Group.info"));}
static void seed(void){put("Alpha.info","alpha-source");put("Beta.info","beta-source");assert(!mkdir("stage",0700));put("stage/Alpha.info","alpha-copy");put("stage/Beta.info","beta-copy");put("stage.info","TILE=group");}
static void clear(void){unlink("Alpha.info");unlink("Beta.info");unlink("Alpha.backup");unlink("Beta.backup");unlink("Group/Alpha.info");unlink("Group/Beta.info");rmdir("Group");unlink("Group.info");unlink("stage/Alpha.info");unlink("stage/Beta.info");rmdir("stage");unlink("stage.info");}
int main(int argc,char **argv){assert(argc==2&&!chdir(argv[1]));struct TileRenamePlan p;struct Context c;
 for(unsigned fail=1;fail<=4;++fail){seed();plan(&p);c=(struct Context){0,fail,0};assert(!TileRenameExecute(&p,move,&c));assert(!p.completed&&!p.rollbackFailed);same("Alpha.info","alpha-source");same("Beta.info","beta-source");assert(present("stage")&&!present("Group")&&!present("Group.info"));clear();}
 seed();plan(&p);c=(struct Context){0,0,0};put("Group.info","user-owned");assert(!TileRenameExecute(&p,move,&c));same("Group.info","user-owned");same("Alpha.info","alpha-source");same("Beta.info","beta-source");clear();
 seed();plan(&p);c=(struct Context){0,3,4};assert(!TileRenameExecute(&p,move,&c)&&p.rollbackFailed&&p.completed==2);same("Alpha.backup","alpha-source");same("Beta.backup","beta-source");assert(present("stage"));clear();
 seed();plan(&p);c=(struct Context){0,0,0};assert(TileRenameExecute(&p,move,&c)&&p.completed==4);same("Group/Alpha.info","alpha-copy");same("Group/Beta.info","beta-copy");same("Group.info","TILE=group");same("Alpha.backup","alpha-source");same("Beta.backup","beta-source");clear();
 memset(&p,0,sizeof(p));assert(!TileRenameAdd(&p,"same","same"));char longpath[513];memset(longpath,'x',512);longpath[512]=0;assert(!TileRenameAdd(&p,longpath,"to"));for(unsigned i=0;i<16;++i)assert(TileRenameAdd(&p,"a","b"));assert(!TileRenameAdd(&p,"c","d"));char backdrop[128]=":existing\r\n:Alpha";long used=strlen(backdrop);
 assert(TileBackdropAppend(backdrop,used,sizeof(backdrop),":Alpha")==used);
 assert(TileBackdropAppend(backdrop,used,sizeof(backdrop),":existing")==used);
 long appended=TileBackdropAppend(backdrop,used,sizeof(backdrop),":Beta");assert(appended==used+7);assert(!memcmp(backdrop+used,"\n:Beta\n",7));
 char unchanged[128];memcpy(unchanged,backdrop,sizeof(backdrop));
 assert(TileBackdropAppend(backdrop,appended,appended,":overflow")==-1);assert(!memcmp(unchanged,backdrop,sizeof(backdrop)));
 assert(TileBackdropAppend(backdrop,appended,sizeof(backdrop),":bad\nentry")==-1);
 puts("tiles-transaction-host PASS");}
