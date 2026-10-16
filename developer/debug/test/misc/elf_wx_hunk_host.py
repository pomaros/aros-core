#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# This file is distributed under the AROS Public License, version 1.1.
"""Exercise the production ELF hunk protector with real Darwin W^X mappings."""
from pathlib import Path
import platform
import subprocess
import tempfile

if platform.system() != 'Darwin' or platform.machine() != 'arm64':
    raise SystemExit('SKIP: requires Darwin arm64 for real ELF-entry execution')
root = Path(__file__).resolve().parents[4]
source = (root / 'rom/dos/internalloadseg_elf.c').read_text()
start = source.index('static BOOL protect_elf_code_hunk(')
production = source[start:source.index('\n}\n', start) + 3]
prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
typedef uintptr_t BPTR;
typedef uintptr_t IPTR;
typedef void *APTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define BADDR(value) ((void *)(value))
#define MAP_Readable 1
#define MAP_Executable 4
struct KernelBase { int unused; };
struct hunk { uint32_t size; BPTR next; char data[]; } __attribute__((packed));
#define BPTR2HUNK(value) ((struct hunk *)((char *)(value) - offsetof(struct hunk, next)))
#define HUNK2BPTR(value) ((BPTR)&(value)->next)
static unsigned protects;
static void *last_address;
static uint32_t last_length;
static int executable(void *ptr)
{
    mach_vm_address_t address = (mach_vm_address_t)ptr;
    mach_vm_size_t length = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    assert(mach_vm_region(mach_task_self(), &address, &length,
        VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object) == KERN_SUCCESS);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    return (info.protection & VM_PROT_EXECUTE) != 0;
}
static void KrnSetProtection(void *address, uint32_t length, int flags)
{
    assert(flags == (MAP_Readable | MAP_Executable));
    assert((uintptr_t)address % (unsigned)getpagesize() == 0);
    assert(mprotect(address, length, PROT_READ | PROT_EXEC) == 0);
    sys_icache_invalidate(address, length);
    last_address = address; last_length = length; protects++;
}
'''
main = r'''
static void check(unsigned later_page)
{
    size_t page = (size_t)getpagesize(), length = page * 4;
    uint8_t *code = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *data = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(code != MAP_FAILED && data != MAP_FAILED);
    struct hunk *h = (void *)code, *d = (void *)data;
    h->size = (uint32_t)length; h->next = 0;
    d->size = (uint32_t)page; d->next = HUNK2BPTR(h);
    void *payload = code + (later_page ? 2 * page : 28);
    const uint32_t body[] = {0x52800920, 0xd65f03c0}; /* mov w0,#73; ret */
    const uint32_t jump[] = {0x58000050, 0xd61f0200}; /* ldr x16,literal; br x16 */
    memcpy(payload, body, sizeof body);
    memcpy(h->data, jump, sizeof jump);
    memcpy(h->data + sizeof jump, &payload, sizeof payload);
    assert(!executable(h->data));
    if (later_page) {
        /* Protecting only a page-aligned payload still misses the entry stub. */
        assert(mprotect(payload, sizeof body, PROT_READ | PROT_EXEC) == 0);
        assert(executable(payload) && !executable(h->data));
    } else {
        /* The previous raw payload call is rejected by the real host. */
        errno = 0;
        assert(mprotect(payload, sizeof body, PROT_READ | PROT_EXEC) == -1 && errno == EINVAL);
    }
    unsigned before = protects;
    assert(!protect_elf_code_hunk(HUNK2BPTR(d), code + length - 4, 8, NULL));
    assert(!protect_elf_code_hunk(HUNK2BPTR(d), (void *)1, 1, NULL));
    assert(protects == before);
    assert(protect_elf_code_hunk(HUNK2BPTR(d), payload, sizeof body, NULL));
    assert(protects == before + 1 && last_address == code && last_length == length);
    assert(executable(h->data) && executable(payload) && !executable(data));
    assert(((int (*)(void))h->data)() == 73);
    d->data[0] = 42; assert(d->data[0] == 42); /* independent data stays R/W */
    assert(munmap(code, length) == 0 && munmap(data, page) == 0);
}
int main(void)
{
    check(0); check(1);
    puts("ELF W^X PASS: real unaligned-payload rejection, late-page trampoline execution, writable data, bounds");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='aros-elf-wx-') as tmp:
    c = Path(tmp) / 'test.c'
    binary = Path(tmp) / 'test'
    c.write_text(prefix + production + main)
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-Wextra',
                    '-Wno-unused-parameter', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
