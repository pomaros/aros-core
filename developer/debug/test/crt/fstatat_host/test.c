/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 * Modeled directory and metadata providers for actual fstatat entry points.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dos/dos.h>
#include <fcntl.h>
#include <sys/stat.h>
static unsigned enters,leaves,narrow_calls,wide_calls;
static int cwd=100,expected_cwd,fail_metadata;
int __at_isabsolute(const char *path) { return path[0]=='/' || strchr(path,':')!=NULL; }
int __at_enter(int fd,BPTR *old,BPTR *lock) {
    ++enters;
    if(fd<0) {errno=EBADF;return -1;}
    *old=cwd;*lock=99;cwd=9;return 0;
}
void __at_leave(BPTR old,BPTR lock) {
    ++leaves;
    if(old||lock) {assert(old==100&&lock==99&&cwd==9);cwd=old;}
    assert(cwd==100);
}
static int metadata(const char *path) {
    assert(path&&cwd==expected_cwd);
    if(fail_metadata) {errno=EACCES;return -1;}
    return 0;
}
int stat(const char *path,struct stat *out) {++narrow_calls;if(metadata(path))return -1;out->size=42;out->marker=0x89abcdef;return 0;}
int lstat(const char *path,struct stat *out) {int r=stat(path,out);if(!r)out->size=43;return r;}
int stat64(const char *path,struct stat64 *out) {++wide_calls;if(metadata(path))return -1;out->marker=UINT64_C(0x123456789abcdef);out->size=UINT64_C(0x100000042);return 0;}
int lstat64(const char *path,struct stat64 *out) {int r=stat64(path,out);if(!r)out->size=UINT64_C(0x100000043);return r;}
static void reset(void) {assert(cwd==100);enters=leaves=narrow_calls=wide_calls=0;errno=0;}
int main(void) {
    struct {uint64_t before; union {struct stat narrow;struct stat64 wide;} value;uint64_t after;} out;
    for(unsigned wide=0;wide<2;wide++) for(unsigned absolute=0;absolute<3;absolute++)
    for(unsigned nofollow=0;nofollow<2;nofollow++) for(unsigned failure=0;failure<2;failure++) {
        reset();memset(&out,0x5a,sizeof out);out.before=123;out.after=456;
        const char *path=absolute==2?"SYS:file":absolute?"/file":"file";
        expected_cwd=absolute?100:9;fail_metadata=failure;
        int result=wide?fstatat64(7,path,&out.value.wide,nofollow?AT_SYMLINK_NOFOLLOW:0):fstatat(7,path,&out.value.narrow,nofollow?AT_SYMLINK_NOFOLLOW:0);
        assert(result==(failure?-1:0)&&cwd==100&&enters==!absolute&&leaves==1);
        assert(narrow_calls==!wide&&wide_calls==wide&&out.before==123&&out.after==456);
        if(failure)assert(errno==EACCES);
        else if(wide)assert(out.value.wide.marker==UINT64_C(0x123456789abcdef)&&out.value.wide.size==UINT64_C(0x100000042)+nofollow);
        else assert(out.value.narrow.marker==0x89abcdef&&out.value.narrow.size==42+nofollow);
    }
    for(unsigned wide=0;wide<2;wide++) for(unsigned follow=0;follow<2;follow++) {
        reset();int flags=0x40000000|(follow?AT_SYMLINK_NOFOLLOW:0);
        assert((wide?fstatat64(7,"file",&out.value.wide,flags):fstatat(7,"file",&out.value.narrow,flags))==-1);
        assert(errno==EINVAL&&!enters&&!leaves&&!narrow_calls&&!wide_calls);
    }
    reset();assert(fstatat(7,NULL,&out.value.narrow,0)==-1&&errno==EFAULT);
    assert(fstatat64(7,"file",NULL,0)==-1&&errno==EFAULT&&!enters&&!leaves);
    reset();assert(fstatat(-1,"file",&out.value.narrow,0)==-1&&errno==EBADF&&enters==1&&!leaves);
    reset();assert(fstatat64(-1,"file",&out.value.wide,0)==-1&&errno==EBADF&&enters==1&&!leaves);
    puts("PASS fstatat narrow/wide dispatch, flags, restore, failures and bounds");
    return 0;
}
