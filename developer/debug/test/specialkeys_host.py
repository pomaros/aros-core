#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Compile the special-keys header as it ships and check its parser,
formatter and key translation on the host, under ASan and UBSan."""
from pathlib import Path
import subprocess
import tempfile

core = Path(__file__).resolve().parents[3]
include = core / 'arch/aarch64-apple/include'
test = r'''
#include <assert.h>
#include <stdio.h>
#include <aros/apple/specialkeys.h>

static enum sk_verdict key(const struct sk_config *c, struct sk_state *s, unsigned raw,
    uint16_t *code, enum sk_action *action)
{
    return sk_translate(c, s, (uint16_t)raw, code, action);
}

int main(void)
{
    struct sk_config c, d;
    struct sk_state s;
    char text[1024];
    uint16_t code;
    enum sk_action action;
    size_t n;
    int i;

    /* Defaults: printed functions where AROS has one, else the F-key. */
    sk_defaults(&c);
    assert(c.action[0] == SK_BRIGHTNESS_DOWN && c.action[1] == SK_BRIGHTNESS_UP);
    for (i = 2; i < 6; i++) assert(c.action[i] == SK_FKEY);
    assert(c.action[7] == SK_MEDIA_PLAY && c.action[9] == SK_MUTE && c.action[11] == SK_VOLUME_UP);
    assert(!c.fnlock && c.light_timeout == 0);

    /* Format and parse round-trip; unknown and malformed lines count. */
    c.fnlock = 1; c.light_timeout = 30; c.action[4] = SK_LIGHT_DOWN; c.action[5] = SK_LIGHT_UP;
    n = sk_format(&c, text, sizeof(text));
    assert(n > 0 && text[n] == 0);
    assert(sk_parse(&d, text, n) == 0);
    assert(memcmp(&c, &d, sizeof(c)) == 0);
    assert(sk_format(&c, text, 20) == 0);
    {
        const char *odd = "; comment\n  f5   light-toggle \r\nF13 mute\nF1 dim\nfnlock 2\n"
            "light-timeout 9999\nlight-timeout 3600\nF05 mute\nbogus 1\n\n";
        assert(sk_parse(&d, odd, strlen(odd)) == 6);
        assert(d.action[4] == SK_LIGHT_TOGGLE && d.action[0] == SK_BRIGHTNESS_DOWN);
        assert(!d.fnlock && d.light_timeout == 3600);
    }
    assert(sk_key_by_name("F12", 3) == 11 && sk_key_by_name("F0", 2) < 0 && sk_key_by_name("F1x", 3) < 0);

    /* Translation without fnlock: F1 is an action, fn+F1 is F1. */
    sk_defaults(&c);
    memset(&s, 0, sizeof(s));
    assert(key(&c, &s, 0x50, &code, &action) == SK_SWALLOW && action == SK_BRIGHTNESS_DOWN);
    assert(key(&c, &s, 0x50, &code, &action) == SK_SWALLOW && action == SK_BRIGHTNESS_DOWN); /* repeat */
    assert(key(&c, &s, 0xD0, &code, &action) == SK_SWALLOW && action == SK_NONE);
    assert(key(&c, &s, AROS_APPLE_RAWKEY_FN, &code, &action) == SK_SWALLOW && s.fn_down);
    assert(key(&c, &s, 0x50, &code, &action) == SK_PASS);
    assert(key(&c, &s, 0xD0, &code, &action) == SK_PASS);
    /* A key pressed with fn and released after fn goes up the same way. */
    assert(key(&c, &s, 0x5A - 1, &code, &action) == SK_PASS);    /* fn+F10: F10 */
    assert(key(&c, &s, AROS_APPLE_RAWKEY_FN | 0x80, &code, &action) == SK_SWALLOW && !s.fn_down);
    assert(key(&c, &s, 0xD9, &code, &action) == SK_PASS);
    /* Media keys are rewritten, the release too, even after fn moved. */
    assert(key(&c, &s, 0x57, &code, &action) == SK_REWRITE && code == SK_RAWKEY_MEDIA_PLAY);
    assert(key(&c, &s, AROS_APPLE_RAWKEY_FN, &code, &action) == SK_SWALLOW);
    assert(key(&c, &s, 0xD7, &code, &action) == SK_REWRITE && code == (SK_RAWKEY_MEDIA_PLAY | 0x80));
    /* fn layer: arrows and Backspace. */
    assert(key(&c, &s, SK_RAWKEY_LEFT, &code, &action) == SK_REWRITE && code == SK_RAWKEY_HOME);
    assert(key(&c, &s, SK_RAWKEY_BACKSPACE, &code, &action) == SK_REWRITE && code == SK_RAWKEY_DELETE);
    assert(key(&c, &s, AROS_APPLE_RAWKEY_FN | 0x80, &code, &action) == SK_SWALLOW);
    assert(key(&c, &s, SK_RAWKEY_LEFT | 0x80, &code, &action) == SK_REWRITE && code == (SK_RAWKEY_HOME | 0x80));
    assert(key(&c, &s, SK_RAWKEY_LEFT, &code, &action) == SK_PASS);
    assert(key(&c, &s, SK_RAWKEY_LEFT | 0x80, &code, &action) == SK_PASS);
    /* Ordinary keys and non-rawkey codes pass. */
    assert(key(&c, &s, 0x20, &code, &action) == SK_PASS && code == 0x20);
    assert(key(&c, &s, 0x1234, &code, &action) == SK_PASS);
    /* An F-key mapped to the F-key, and one mapped to nothing. */
    assert(key(&c, &s, 0x52, &code, &action) == SK_PASS);
    c.action[2] = SK_NONE;
    assert(key(&c, &s, 0x52, &code, &action) == SK_SWALLOW && action == SK_NONE);

    /* fnlock: the top row is F1..F12 and fn gives the actions. */
    sk_defaults(&c); c.fnlock = 1;
    memset(&s, 0, sizeof(s));
    assert(key(&c, &s, 0x6F, &code, &action) == SK_PASS);             /* F12 */
    assert(key(&c, &s, 0xEF, &code, &action) == SK_PASS);
    assert(key(&c, &s, AROS_APPLE_RAWKEY_FN, &code, &action) == SK_SWALLOW);
    assert(key(&c, &s, 0x6F, &code, &action) == SK_SWALLOW && action == SK_VOLUME_UP);
    assert(key(&c, &s, 0x4B, &code, &action) == SK_SWALLOW && action == SK_VOLUME_DOWN);
    puts("specialkeys host: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='specialkeys-') as temp:
    source = Path(temp) / 'test.c'
    binary = Path(temp) / 'test'
    source.write_text(test)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(include), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
