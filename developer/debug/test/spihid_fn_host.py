#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Compile the actual fn-field functions of spi_hid_device.c on the host and
check them against Apple-style keyboard report descriptors."""
from pathlib import Path
import subprocess
import tempfile

core = Path(__file__).resolve().parents[3]
source = (core / 'arch/aarch64-apple/spi/spi_hid_device.c').read_text()

def function(name):
    start = source.index('int ' + name + '(')
    depth, at = 0, source.index('{', start)
    for end in range(at, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError(name)

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define DEV_OK 0
#define DEV_ERR_ARGUMENT (-1)
#define DEV_ERR_PROTOCOL (-4)
#define DEV_ERR_LENGTH (-5)
#define AROS_APPLE_SPIHID_MESSAGE_HEADER_BYTES UINT32_C(8)
struct AppleSPIHIDFrame { uint32_t flags, device, offset, remaining, length; const uint8_t *payload; };
'''
main = r'''
/* Report 1: modifiers, a reserved byte, six key slots, fn, padding. */
static const uint8_t apple[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x95,0x01, 0x75,0x08, 0x81,0x01,
    0x95,0x06, 0x75,0x08, 0x15,0x00, 0x26,0xff,0x00, 0x05,0x07, 0x19,0x00, 0x2a,0xff,0x00, 0x81,0x00,
    0x05,0xff, 0x09,0x03, 0x75,0x01, 0x95,0x01, 0x81,0x02,
    0x95,0x01, 0x75,0x07, 0x81,0x01,
    0xc0
};
/* The same with page 0xff00, an extended usage, and an output report first. */
static const uint8_t variant[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x85,0x01,
    0x05,0x08, 0x19,0x01, 0x29,0x05, 0x75,0x01, 0x95,0x05, 0x91,0x02,
    0x75,0x08, 0x95,0x02, 0x81,0x01,
    0x06,0x00,0xff, 0x0b,0x03,0x00,0xff,0x00, 0x75,0x01, 0x95,0x01, 0x81,0x02,
    0xc0
};
/* The J313's own keyboard descriptor, as spihid.hidd logged it on the M1:
   fn is seven bits wide, after the one-bit Consumer eject field. */
static const uint8_t j313[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,0x95,0x05,
    0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x26,0xff,0x00,0x05,0x07,0x19,0x00,0x29,0xff,0x81,
    0x00,0x05,0x0c,0x75,0x01,0x95,0x01,0x09,0xb8,0x15,0x00,0x25,0x01,0x81,0x02,0x05,0xff,0x09,0x03,0x75,0x07,0x95,0x01,0x81,0x02,0xc0,0x05,0x0c,0x09,0x01,0xa1,0x01,
    0x85,0x52,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x09,0xcd,0x81,0x02,0x09,0xb3,0x81,0x02,0x09,0xb4,0x81,0x02,0x09,0xb5,0x81,0x02,0x09,0xb6,0x81,0x02,0x81,0x01,
    0x81,0x01,0x81,0x01,0x85,0x09,0x15,0x00,0x25,0x01,0x75,0x08,0x95,0x01,0x06,0x01,0xff,0x09,0x0b,0xb1,0x02,0x75,0x08,0x95,0x02,0xb1,0x01,0xc0,0x06,0x00,0xff,0x09,
    0x06,0xa1,0x01,0x06,0x00,0xff,0x09,0x06,0x15,0x00,0x26,0xff,0x00,0x75,0x08,0x95,0x40,0x85,0x3f,0x81,0x22,0xc0
};
static const uint8_t nofn[] = { 0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x75,0x08, 0x95,0x08, 0x81,0x01, 0xc0 };

int main(void)
{
    uint32_t id = 9, byte = 9, bit = 9, width = 9;
    uint8_t payload[8 + 12];
    struct AppleSPIHIDFrame frame = { 0, 0, 0, 0, sizeof(payload), payload };

    assert(AppleSPIHIDFindFnField(apple, sizeof(apple), &id, &byte, &bit, &width) == DEV_OK);
    assert(id == 1 && byte == 9 && bit == 0 && width == 1);
    memset(payload, 0, sizeof(payload));
    payload[8] = 1;
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, width) == 0);
    payload[8 + 9] = 1;
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, width) == 1);
    payload[8] = 2;                               /* another report id */
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, width) == 0);
    payload[8] = 1;
    frame.length = 8 + 9;                         /* too short for the fn byte */
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, width) == 0);

    assert(AppleSPIHIDFindFnField(variant, sizeof(variant), &id, &byte, &bit, &width) == DEV_OK);
    assert(id == 1 && byte == 3 && bit == 0 && width == 1);

    /* The real keyboard: report 1, after the id, modifiers, a reserved byte,
       six keys and eject: bit 65, so byte 9 bit 1, seven bits wide. */
    assert(AppleSPIHIDFindFnField(j313, sizeof(j313), &id, &byte, &bit, &width) == DEV_OK);
    assert(sizeof(j313) == 182 && id == 1 && byte == 9 && bit == 1 && width == 7);
    memset(payload, 0, sizeof(payload));
    frame.length = sizeof(payload);
    payload[8] = 1;
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, width) == 0);
    payload[8 + 9] = 0x01;                        /* eject alone: not fn */
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, width) == 0);
    payload[8 + 9] = 0x02;                        /* the field's value 1 */
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, width) == 1);
    payload[8 + 9] = 0x80;                        /* its top bit */
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, width) == 1);
    /* A field that runs into the next byte. */
    payload[8 + 9] = 0;
    payload[8 + 10] = 0x01;
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, 4U, 5U) == 1);
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, 4U, 4U) == 0);
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, 0U) == 0);
    assert(AppleSPIHIDKeyboardFn(&frame, id, byte, bit, 9U) == 0);
    assert(AppleSPIHIDFindFnField(nofn, sizeof(nofn), &id, &byte, &bit, &width) == DEV_ERR_PROTOCOL);
    assert(AppleSPIHIDFindFnField(apple, 5, &id, &byte, &bit, &width) != DEV_OK);
    assert(AppleSPIHIDFindFnField(apple, sizeof(apple) - 30, &id, &byte, &bit, &width) != DEV_OK);
    puts("spihid fn host: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='spihid-fn-') as temp:
    c = Path(temp) / 'test.c'
    b = Path(temp) / 'test'
    c.write_text(prefix + function('AppleSPIHIDFindFnField') + '\n' +
                 function('AppleSPIHIDKeyboardFn') + '\n' + main)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(c), '-o', str(b)], check=True)
    subprocess.run([str(b)], check=True)
