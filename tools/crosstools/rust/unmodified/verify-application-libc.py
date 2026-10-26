#!/usr/bin/env python3
# Licensed under the AROS Public License (APL) Version 1.1
# Copyright (c) 2026 John Knipper
"""Admit the generated core application libc before its crate fetch/patch recipe."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def read_file(root, name):
    path = root / name
    if path.is_symlink() or not path.is_file():
        raise ValueError('missing or symlinked source: ' + name)
    return path.read_bytes()


def verify(toolchain, crates):
    if toolchain.is_symlink() or crates.is_symlink():
        raise ValueError('component directories must not be symlinks')
    std = json.loads(read_file(toolchain, 'SOURCE-ORIGIN.json'))
    expected = std['application_libc']
    origin_bytes = read_file(crates, 'NATIVE-LIBC-ORIGIN.json')
    patch_bytes = read_file(crates, 'libc-0.2.190-aros-native.diff')
    if hashlib.sha256(origin_bytes).hexdigest() != expected['origin_sha256']:
        raise ValueError('application libc origin differs from the pinned toolchain export')
    if hashlib.sha256(patch_bytes).hexdigest() != expected['patch_sha256']:
        raise ValueError('application libc patch differs from the pinned toolchain export')
    origin = json.loads(origin_bytes)
    revision = std['source_revision']
    repository = 'https://github.com/pomaros/rust-aros.git'
    if (std['schema'] != 1 or std['source_repository'] != repository or
            not re.fullmatch('[0-9a-f]{40}', revision) or
            origin['schema'] != 1 or origin['source_repository'] != repository or
            origin['source_revision'] != revision or
            origin['component'] != 'application-libc' or
            origin['source_file'] != 'crates/libc-0.2.190-aros.diff'):
        raise ValueError('application libc and toolchain must share one source identity')
    item = origin['files']['libc-0.2.190-aros-native.diff']
    if item['source_sha256'] != expected['patch_sha256'] or item['export_sha256'] != expected['patch_sha256']:
        raise ValueError('application libc source/export identity differs')
    return revision, expected['patch_sha256']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--crates', type=Path, required=True)
    parser.add_argument('--store-key', action='store_true',
                        help='print the admitted patch digest for cache selection')
    args = parser.parse_args()
    try:
        revision, key = verify(args.toolchain, args.crates)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, 'native-libc-admission result=FAIL reason=' + str(error) + '\n')
    print(key if args.store_key else 'native-libc-admission result=PASS source_revision=' + revision)


if __name__ == '__main__':
    main()
