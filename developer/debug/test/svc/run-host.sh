#!/bin/sh
# [SMP13] host gate: the hosted vehicle and its dropped-wake control, with
# the thread sanitizer. Usage: developer/debug/test/svc/run-host.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=c11 -O2 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -fsanitize=${SANITIZER:-thread} \
    -I"$root/compiler/include" -I"$here" \
    "$here/svc_host.c" "$root/compiler/svc/svc_ring.c" -lpthread -o "$tmp/svc_host"
"$tmp/svc_host"
