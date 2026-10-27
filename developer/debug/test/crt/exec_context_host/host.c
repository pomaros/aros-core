/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef unsigned long BPTR;
typedef long LONG;
typedef char *STRPTR;
#define BNULL 0
#define EXEC_PARENT 8
#define CLI_DEFAULTSTACK_UNIT 4
#define D(x)
struct StdCBase { int id; };
struct StdCIOBase { int id; };
struct Task { struct { char *ln_Name; } tc_Node; };
struct Process { struct Task task; BPTR pr_CES; };
struct CommandLineInterface { long cli_DefaultStack; };
typedef struct _fcb { BPTR handle; unsigned references; } fcb;
struct PosixCIntBase {
    struct { struct StdCBase *StdCBase; struct StdCIOBase *StdCIOBase; } PosixCBase;
    unsigned flags;
    BPTR exec_seglist, exec_cwd, exec_progdir;
    fcb *exec_files[3];
    char *exec_args, *exec_taskname;
    void *exec_pool;
    struct StdCBase *exec_oldstdcbase;
};
static struct Process process;
static struct PosixCIntBase base, nested, *live;
static struct CommandLineInterface cli;
static struct StdCBase stdc;
static struct StdCIOBase stdcio;
static fcb files[3];
static BPTR cwd, progdir, input, output;
static unsigned unlocks[64], releases[3], deleted, unloaded, flushed, runs;
static int with_cli, program_changes_dirs, failed_command, exit_status;
static jmp_buf done;
static struct Task *FindTask(void *unused) { (void)unused; return &process.task; }
static struct PosixCIntBase *__cwd_runtime_base(void) { return live; }
static struct CommandLineInterface *Cli(void) { return with_cli ? &cli : NULL; }
static void SetProgramName(STRPTR name) { assert(name); }
static BPTR Input(void) { return input; }
static BPTR Output(void) { return output; }
static BPTR SelectInput(BPTR next) { BPTR old=input; input=next; return old; }
static BPTR SelectOutput(BPTR next) { BPTR old=output; output=next; return old; }
static BPTR CurrentDir(BPTR next) { BPTR old=cwd; cwd=next; return old; }
static BPTR SetProgramDir(BPTR next) { BPTR old=progdir; progdir=next; return old; }
static void UnLock(BPTR lock) { if(lock) { assert(lock<64); unlocks[lock]++; } }
static void Flush(BPTR handle) { assert(handle==12 || handle==13); flushed++; }
static void __releasefcb(fcb *file) {
    assert(file>=files && file<files+3);
    assert(file->references && "execution must retain each file until restoration");
    assert(input==1 && output==2 && process.pr_CES==3 && "restore handles before releasing files");
    file->references--; releases[file-files]++; errno=EIO;
}
static void DeletePool(void *pool) { assert(pool==&base); deleted++; errno=EIO; }
static void UnLoadSeg(BPTR segment) { assert(segment==21); unloaded++; errno=EIO; }
static void host_exit(int status) { exit_status=status; longjmp(done,1); }
#define _Exit host_exit
static LONG RunCommand(BPTR segment, LONG stack, STRPTR args, size_t size) {
    assert(segment==21 && stack==(with_cli ? 16384 : 1024*1024));
    assert(strcmp(args,"argument\n")==0 && size==9);
    assert(cwd==31 && progdir==32 && "run with captured logical directories");
    assert(input==11 && output==12 && process.pr_CES==13);
    for(unsigned i=0;i<3;i++) assert(files[i].references==1);
    assert(!base.exec_cwd && !base.exec_progdir && "directory ownership transferred to Process");
    if(program_changes_dirs) {
        UnLock(CurrentDir(41)); UnLock(SetProgramDir(42));
    }
    base.PosixCBase.StdCBase=NULL; base.PosixCBase.StdCIOBase=NULL;
    live=&nested;
    runs++;
    return failed_command ? -1 : 7;
}
static void __exec_cleanup(struct PosixCIntBase *PosixCBase);
/* EXEC_DO */
/* EXEC_CLEANUP */
static void reset(void) {
    memset(&base,0,sizeof(base)); memset(&nested,0,sizeof(nested));
    memset(unlocks,0,sizeof(unlocks)); memset(releases,0,sizeof(releases));
    deleted=unloaded=flushed=runs=0;
    process.task.tc_Node.ln_Name="launcher";process.pr_CES=3;
    cwd=4;progdir=5;input=1;output=2;cli.cli_DefaultStack=4096;
    base.PosixCBase.StdCBase=&stdc;base.PosixCBase.StdCIOBase=&stdcio;
    base.exec_oldstdcbase=&stdc;base.exec_pool=&base;base.exec_seglist=21;
    base.exec_cwd=31;base.exec_progdir=32;
    base.exec_args="argument\n";base.exec_taskname="command";
    for(unsigned i=0;i<3;i++) {files[i]=(fcb){11+i,1};base.exec_files[i]=&files[i];}
    live=&base;
}
int main(void) {
    for(with_cli=0;with_cli<2;with_cli++)
    for(program_changes_dirs=0;program_changes_dirs<2;program_changes_dirs++)
    for(failed_command=0;failed_command<2;failed_command++) {
        reset();
        if(!setjmp(done)) __exec_do_regular(&base);
        assert(exit_status==(failed_command ? -1 : 7) && runs==1);
        assert(cwd==4 && progdir==5 && "restore launcher directories");
        assert(input==1 && output==2 && process.pr_CES==3);
        assert(unlocks[31]==1 && unlocks[32]==1);
        assert(unlocks[41]==(unsigned)program_changes_dirs && unlocks[42]==(unsigned)program_changes_dirs);
        assert(!unlocks[4] && !unlocks[5]);
        assert(deleted==1 && unloaded==1 && flushed==2);
        assert(!strcmp(process.task.tc_Node.ln_Name,"launcher"));
        assert(base.PosixCBase.StdCBase==&stdc && nested.PosixCBase.StdCBase==&stdc);
        assert(base.PosixCBase.StdCIOBase==&stdcio && nested.PosixCBase.StdCIOBase==&stdcio);
        for(unsigned i=0;i<3;i++) assert(!files[i].references && releases[i]==1 && "release every execution file once");
        __exec_cleanup(&base);
        assert(deleted==1 && unloaded==1);
    }
    /* Preparation rollback owns locks which have never entered the Process. */
    reset();errno=ENOMEM;__exec_cleanup(&base);
    assert(errno==ENOMEM && "cleanup preserves the operation error");
    assert(unlocks[31]==1 && unlocks[32]==1 && cwd==4 && progdir==5);
    __exec_cleanup(&base);assert(unlocks[31]==1 && unlocks[32]==1);
    puts("PASS actual exec run/cleanup: 8 lifecycle cases and idempotent preparation rollback");
    return 0;
}
