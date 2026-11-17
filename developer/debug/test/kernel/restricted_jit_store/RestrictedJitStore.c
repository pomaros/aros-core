/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/kernel.h>
#include <proto/exec.h>
#include <aros/apple/restricted.h>
#include <aros/apple/jit.h>
#include <stdio.h>
#include <string.h>
APTR KernelBase;
extern const unsigned char StoreEntry[],StoreEntryEnd[],StorePayload[],StorePayloadEnd[];
extern const unsigned char UnsupportedPayload[],UnsupportedPayloadEnd[];
static unsigned char bytes[4096],expected[4096];
static struct AROSRestrictedResult result;
#define CHECK(x) do { if (!(x)) { failed=__LINE__;goto cleanup; } } while (0)
int main(void)
{
    int failed=0;uint64_t domain=0;
    const uintptr_t address=UINT64_C(0x200000000);
    struct AROSRestrictedLifetime before={0},after={0};
    KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");
    if (!KernelBase) return 20;
    const struct AROSRestrictedAPI *api=(const void *)KrnGetSystemAttr(AROS_RESTRICTED_KATTR_API);
    intptr_t mode=KrnGetSystemAttr(AROS_APPLE_KATTR_JIT_MODE);
    if (!api || api->version<36 || (mode!=AROS_APPLE_JIT_SPRR && mode!=AROS_APPLE_JIT_RWX)) return 20;
    api->lifetime(&before);
    CHECK(domain=api->create_domain(StoreEntry,StoreEntryEnd-StoreEntry,0));
    CHECK(api->reserve_memory(domain,address,8192));
    CHECK(api->commit_memory(domain,address,3) && api->commit_memory(domain,address+4096,7));
    CHECK(api->write_domain(domain,address,StorePayload,StorePayloadEnd-StorePayload));
    CHECK(api->protect_memory(domain,address,7));
    CHECK(api->run_domain(domain,address,&result));
    printf("[JITSTORE] supported status=%lld resumed=%llu esr=%llx far=%llx\n",
        (long long)result.status,(unsigned long long)result.resumed,
        (unsigned long long)result.esr,(unsigned long long)result.far);fflush(stdout);
    CHECK(result.status==43 && result.drained && !result.resumed && !result.exceptions);
    CHECK(api->read_domain(domain,address+4096,bytes,sizeof(bytes)));
    expected[1]=0xee;expected[2]=0xee;expected[3]=0xff;
    expected[8]=0xee;expected[9]=0xff;expected[10]=0xee;expected[11]=0xff;
    const unsigned char full[]={0xee,0xff,0xee,0xff,0x55,0x66,0x77,0x88};
    memcpy(expected+16,full,8);
    CHECK(!memcmp(bytes,expected,sizeof(bytes)));
    CHECK(api->protect_memory(domain,address,3));
    CHECK(api->write_domain(domain,address,UnsupportedPayload,UnsupportedPayloadEnd-UnsupportedPayload));
    CHECK(api->protect_memory(domain,address,7));
    memset(&result,0,sizeof(result));
    CHECK(api->run_domain(domain,address,&result));
    printf("[JITSTORE] unsupported status=%lld resumed=%llu esr=%llx far=%llx\n",
        (long long)result.status,(unsigned long long)result.resumed,
        (unsigned long long)result.esr,(unsigned long long)result.far);fflush(stdout);
    if (mode==AROS_APPLE_JIT_SPRR)
        CHECK(result.status==-2 && result.drained && !result.resumed &&
              (result.esr>>26)==0x24 && result.far==address+4096+8 && result.pc==address);
    else CHECK(result.status==43 && result.drained);
cleanup:
    if (domain && api->close_domain(domain)!=1) failed=__LINE__;
    api->lifetime(&after);
    if (after.pages!=before.pages || after.live!=before.live || after.domains!=before.domains ||
        after.pending!=before.pending || after.shared_objects!=before.shared_objects) failed=__LINE__;
    printf("[JITSTORE] mode=%ld pages_before=%llu pages_after=%llu result=%s line=%d\n",
        (long)mode,(unsigned long long)before.pages,(unsigned long long)after.pages,failed ? "FAIL" : "PASS",failed);
    fflush(stdout);return failed ? 20 : 0;
}
