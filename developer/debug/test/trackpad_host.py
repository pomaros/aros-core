#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Compile the trackpad gesture header as it ships and check finger
identity, clicks with one to three fingers and two-finger scrolling on the
host, under ASan and UBSan."""
from pathlib import Path
import subprocess
import tempfile

core = Path(__file__).resolve().parents[3]
include = core / 'arch/aarch64-apple/include'
test = r'''
#include <assert.h>
#include <stdio.h>
#include <aros/apple/trackpad.h>

static struct tp_config c;
static struct tp_state s;
static struct tp_event e[64];
static unsigned n;
static uint64_t now;

/* One frame 8 ms after the last, with the points given as x,y pairs. */
static unsigned frame(int button, int secondary, unsigned count, ...)
{
    struct tp_point p[TP_MAX_CONTACTS];
    __builtin_va_list ap;
    unsigned i;

    __builtin_va_start(ap, count);
    for (i = 0; i < count; i++) {
        p[i].x = (int16_t)__builtin_va_arg(ap, int);
        p[i].y = (int16_t)__builtin_va_arg(ap, int);
    }
    __builtin_va_end(ap);
    now += 8000U;
    n = tp_frame(&c, &s, p, count, button, secondary, now, e, 64);
    return n;
}

static int motion(int *x, int *y)
{
    unsigned i;
    *x = *y = 0;
    for (i = 0; i < n; i++)
        if (e[i].type == TP_MOTION) {
            *x += e[i].x;
            *y += e[i].y;
        }
    return *x != 0 || *y != 0;
}

static int has(uint8_t type, uint8_t button)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (e[i].type == type && (type != TP_PRESS && type != TP_RELEASE ? 1 : e[i].button == button))
            return 1;
    return 0;
}

static int wheel(int *x, int *y)
{
    unsigned i, k = 0;
    *x = *y = 0;
    for (i = 0; i < n; i++)
        if (e[i].type == TP_WHEEL) {
            *x += e[i].x;
            *y += e[i].y;
            k++;
        }
    return (int)k;
}

static void start(void)
{
    tp_defaults(&c);
    tp_reset(&s);
    now = 0;
}

int main(void)
{
    int x, y;

    /* One finger moves the pointer, the pad's y upward is the screen's up. */
    start();
    frame(0, 0, 1, 1000, 1000);
    assert(n == 0);
    frame(0, 0, 1, 1080, 1160);
    assert(motion(&x, &y) && x == 10 && y == -20);

    /* A second finger lands ahead of the first in the report: no jump. */
    frame(0, 0, 2, -3000, -2000, 1080, 1160);
    assert(!motion(&x, &y) && wheel(&x, &y) == 0);
    /* The first lifts and the second stays: no jump either. */
    frame(0, 0, 1, -3000, -2000);
    assert(!motion(&x, &y));
    /* and the second then moves the pointer by its own movement only. */
    frame(0, 0, 1, -2920, -2000);
    assert(motion(&x, &y) && x == 10 && y == 0);
    frame(0, 0, 0);
    assert(n == 0);

    /* Two fingers down, then the click: right button, released as right. */
    start();
    frame(0, 0, 2, 0, 0, 2000, 0);
    frame(1, 0, 2, 0, 0, 2000, 0);
    assert(has(TP_PRESS, TP_RIGHT) && !has(TP_PRESS, TP_LEFT));
    frame(1, 0, 1, 0, 0);
    assert(!has(TP_RELEASE, TP_RIGHT));
    frame(0, 0, 1, 0, 0);
    assert(has(TP_RELEASE, TP_RIGHT) && !has(TP_RELEASE, TP_LEFT));

    /* The second finger lands just after the click: still the right button. */
    start();
    frame(0, 0, 1, 0, 0);
    frame(1, 0, 1, 0, 0);
    assert(!has(TP_PRESS, TP_LEFT) && !has(TP_PRESS, TP_RIGHT));
    frame(1, 0, 2, 0, 0, 2200, 300);
    assert(has(TP_PRESS, TP_RIGHT));
    frame(0, 0, 2, 0, 0, 2200, 300);
    assert(has(TP_RELEASE, TP_RIGHT));

    /* One finger alone: left, once the settle time has passed. */
    start();
    frame(0, 0, 1, 0, 0);
    frame(1, 0, 1, 0, 0);
    assert(n == 0);
    frame(1, 0, 1, 0, 0);
    frame(1, 0, 1, 0, 0);
    frame(1, 0, 1, 0, 0);
    frame(1, 0, 1, 0, 0);       /* 32 ms after the press */
    assert(!has(TP_PRESS, TP_LEFT));
    frame(1, 0, 1, 0, 0);       /* 40 ms after the press */
    assert(has(TP_PRESS, TP_LEFT));
    frame(1, 0, 2, 0, 0, 2000, 0);   /* too late to change it */
    assert(!has(TP_PRESS, TP_RIGHT));
    frame(0, 0, 1, 0, 0);
    assert(has(TP_RELEASE, TP_LEFT));

    /* A quick click is pressed and released in the same frame. */
    start();
    frame(0, 0, 1, 0, 0);
    frame(1, 0, 1, 0, 0);
    frame(0, 0, 1, 0, 0);
    assert(n == 2 && e[0].type == TP_PRESS && e[0].button == TP_LEFT &&
           e[1].type == TP_RELEASE && e[1].button == TP_LEFT);

    /* Three fingers: middle. Control (secondary) with one finger: right, at once. */
    start();
    frame(1, 0, 3, 0, 0, 1500, 0, 3000, 0);
    assert(has(TP_PRESS, TP_MIDDLE));
    frame(0, 0, 0);
    assert(has(TP_RELEASE, TP_MIDDLE));
    frame(1, 1, 1, 0, 0);
    assert(has(TP_PRESS, TP_RIGHT));
    frame(0, 0, 1, 0, 0);
    assert(has(TP_RELEASE, TP_RIGHT));

    /* Without clickfinger every click is left, without delay. */
    start();
    c.clickfinger = 0;
    frame(1, 0, 2, 0, 0, 2000, 0);
    assert(has(TP_PRESS, TP_LEFT));
    frame(0, 0, 0);

    /* Two fingers moving up scroll down (natural), and the pointer stays. */
    start();
    frame(0, 0, 2, 0, 0, 1500, 0);
    frame(0, 0, 2, 0, 250, 1500, 250);
    assert(!motion(&x, &y) && wheel(&x, &y) == 1 && y == 1 && x == 0);
    frame(0, 0, 2, 0, 0, 1500, 0);
    assert(wheel(&x, &y) == 1 && y == -1);
    frame(0, 0, 2, 0, 0, 1500, 0);
    assert(wheel(&x, &y) == 0);
    /* Sideways: fingers right scroll left. */
    frame(0, 0, 2, 500, 0, 2000, 0);
    assert(wheel(&x, &y) == 2 && x == -2 && y == 0);
    /* Not natural: the other way. */
    c.natural = 0;
    frame(0, 0, 2, 500, 250, 2000, 250);
    assert(wheel(&x, &y) == 1 && y == -1);
    /* Scrolling off: two fingers move the pointer like one. */
    c.scroll = 0;
    frame(0, 0, 2, 580, 250, 2080, 250);
    assert(motion(&x, &y) && x == 10 && wheel(&x, &y) == 0);

    /* A thumb holds the click and another finger drags: the pointer follows
       the finger that moves. */
    start();
    frame(0, 0, 2, 0, -4000, 0, 0);
    frame(1, 0, 2, 0, -4000, 0, 0);
    assert(has(TP_PRESS, TP_RIGHT));
    start();
    c.clickfinger = 0;
    frame(0, 0, 1, 0, -4000);
    frame(1, 0, 2, 0, -4000, 0, 0);
    assert(has(TP_PRESS, TP_LEFT));
    frame(1, 0, 2, 0, -4000, 160, 0);
    assert(motion(&x, &y) && x == 20 && y == 0);
    frame(1, 0, 2, 2, -4002, 160, -80);
    assert(motion(&x, &y) && x == 0 && y == 10);

    /* Tap to click is off by default: a tap does nothing. */
    start();
    frame(0, 0, 1, 0, 0);
    frame(0, 0, 0);
    assert(n == 0);
    /* On: one finger left, two right, three middle, press and release. */
    c.tap = 1;
    frame(0, 0, 1, 0, 0);
    frame(0, 0, 1, 20, 0);
    frame(0, 0, 0);
    assert(n == 2 && e[0].type == TP_PRESS && e[0].button == TP_LEFT &&
           e[1].type == TP_RELEASE && e[1].button == TP_LEFT);
    frame(0, 0, 1, 0, 0);
    frame(0, 0, 2, 0, 0, 2000, 0);   /* the second finger lands a frame later */
    frame(0, 0, 0);
    assert(has(TP_PRESS, TP_RIGHT) && has(TP_RELEASE, TP_RIGHT));
    frame(0, 0, 3, 0, 0, 1500, 0, 3000, 0);
    frame(0, 0, 0);
    assert(has(TP_PRESS, TP_MIDDLE));
    /* Not a tap: too long, moved, pressed, or a finger added to a touch. */
    frame(0, 0, 1, 0, 0);
    {
        int k;
        for (k = 0; k < 25; k++)
            frame(0, 0, 1, 0, 0);
    }
    frame(0, 0, 0);
    assert(n == 0);
    frame(0, 0, 1, 0, 0);
    frame(0, 0, 1, 400, 0);
    frame(0, 0, 0);
    assert(!has(TP_PRESS, TP_LEFT));
    frame(0, 0, 1, 0, 0);
    frame(1, 0, 1, 0, 0);
    frame(0, 0, 1, 0, 0);
    assert(has(TP_PRESS, TP_LEFT) && has(TP_RELEASE, TP_LEFT));  /* the click */
    frame(0, 0, 0);
    assert(n == 0);                                             /* not a tap too */
    /* Two fingers scrolling are not a tap. */
    frame(0, 0, 2, 0, 0, 1500, 0);
    frame(0, 0, 2, 0, 500, 1500, 500);
    frame(0, 0, 0);
    assert(!has(TP_PRESS, TP_RIGHT));

    /* The settings file: defaults, round trip, refused lines. */
    {
        struct tp_config d;
        char text[256];
        size_t length;
        const char *odd = "; trackpad\n tap 1\nscroll-speed 9 \r\nclick-delay 0\n"
            "natural 2\nscroll-speed 0\nclick-delay 900\nbogus 1\nclickfinger\n\n";

        tp_defaults(&c);
        length = tp_format(&c, text, sizeof(text));
        assert(length > 0 && text[length] == 0);
        assert(strstr(text, "clickfinger 1\n") && strstr(text, "tap 0\n") &&
               strstr(text, "scroll-speed 5\n") && strstr(text, "click-delay 40\n"));
        assert(tp_parse(&d, text, length) == 0 && memcmp(&c, &d, sizeof(c)) == 0);
        assert(tp_format(&c, text, 20) == 0);
        assert(tp_parse(&d, odd, strlen(odd)) == 5);
        assert(d.tap == 1 && d.scroll_speed == 9 && d.settle_us == 0 && d.natural == 1);
        /* Faster scrolling: a notch per 1200 / 9 = 133 units. */
        start();
        c.scroll_speed = 9;
        frame(0, 0, 2, 0, 0, 1500, 0);
        frame(0, 0, 2, 0, 140, 1500, 140);
        assert(wheel(&x, &y) == 1);
        /* No click delay: a one-finger click is left at once. */
        c.settle_us = 0;
        frame(0, 0, 1, 0, 0);
        frame(1, 0, 1, 0, 0);
        assert(has(TP_PRESS, TP_LEFT));
        frame(0, 0, 0);
    }

    /* Sixteen fingers and more do not overflow anything. */
    {
        struct tp_point many[TP_MAX_CONTACTS];
        unsigned i;
        start();
        for (i = 0; i < TP_MAX_CONTACTS; i++) {
            many[i].x = (int16_t)(i * 800);
            many[i].y = 0;
        }
        n = tp_frame(&c, &s, many, TP_MAX_CONTACTS + 4, 1, 0, 8000, e, 64);
        assert(has(TP_PRESS, TP_MIDDLE) && s.count == TP_MAX_CONTACTS);
        n = tp_frame(&c, &s, many, TP_MAX_CONTACTS, 1, 0, 16000, e, 1);
        assert(n <= 1);
    }
    puts("trackpad host: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='trackpad-') as temp:
    source = Path(temp) / 'test.c'
    binary = Path(temp) / 'test'
    source.write_text(test)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(include), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
