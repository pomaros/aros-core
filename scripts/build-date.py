#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Emit command version dates; an explicit source epoch always uses UTC."""
import argparse
import datetime
import os

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--epoch', default=os.environ.get('SOURCE_DATE_EPOCH'))
a = p.parse_args()
if a.epoch is None:
    stamp = datetime.datetime.now().astimezone()
else:
    if not a.epoch or not a.epoch.isascii() or not a.epoch.isdecimal():
        p.error('SOURCE_DATE_EPOCH must be a nonnegative integer')
    try:
        stamp = datetime.datetime.fromtimestamp(int(a.epoch), datetime.timezone.utc)
    except (ValueError, OverflowError, OSError):
        p.error('SOURCE_DATE_EPOCH is outside the supported date range')
print(stamp.strftime('%d.%m.%Y %Y-%m-%d'))
