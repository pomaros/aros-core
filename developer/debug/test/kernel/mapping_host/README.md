<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# Mapping component host checks

These tests compile actual kernel component sources on the development host.
These internal sources are not registered in a platform build or exported as
a DOS/POSIX mapping API. Their callbacks and synthetic page tables do not exercise hardware faults,
native scheduling, remote CPU stop, TLB maintenance, filesystem coherence or
disk persistence. A passing component suite does not admit a DOS/POSIX mapping
API or establish native mmap acceptance.

The portable object engine and pager use separate runners. Run from any working
directory, providing a fresh absolute output directory for each invocation:

```sh
python3 /ABS/AROS/developer/debug/test/kernel/mapping_host/run.py --output /ABS/NEW-ENGINE-OUTPUT
python3 /ABS/AROS/developer/debug/test/kernel/mapping_host/pager.py --output /ABS/NEW-PAGER-OUTPUT
```

The object suite checks fault generations, cancellation after close, draining,
range cuts, population preflight and bounded writeback state. It uses strict
warnings, AddressSanitizer/UndefinedBehaviorSanitizer and compiled negative
controls. The pager suite checks anonymous zero pages, positioned short reads,
EOF tails, malformed allocator/read results, cancellation and ownership, plus
negative controls. Its caller must retain callback context and backing handles
while I/O blocks, then revalidate the request before publishing a returned page.

The object runner also runs the domain test for retained execution membership
and mapping/I/O pins. Both runners refuse dirty input unless
`--allow-dirty-diagnostic` is specified; that mode records DIAGNOSTIC_PASS.
Final receipts record the clean source commit and unchanged input hashes.
Negative controls must hit their exact semantic SIGABRT assertion on Darwin.

The domain test can also be invoked separately.
Run this small standalone test from the source root; choose an unused output
path:

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I rom/kernel/mapping rom/kernel/mapping/domain.c \
  developer/debug/test/kernel/mapping_host/domain_test.c \
  -o /ABS/NEW-DOMAIN-TEST
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /ABS/NEW-DOMAIN-TEST
```

LeakSanitizer is disabled in this Darwin recipe because that runtime does not
support it. AddressSanitizer and UndefinedBehaviorSanitizer stay enabled.

Domain membership is independent of a creating task or a POSIX base open count.
A coordinator joins workers before publishing them and leaves them only after
their accesses and retirement are quiescent. The last member closes admission;
it does not free mappings or stop a CPU. Each in-flight I/O pin prevents its
mapping pin from being released. Explicit stop rejects new fault I/O while
allowing cleanup I/O against retained mappings. Reclamation additionally needs
all external references withdrawn. These are internal component contracts;
native task/DOS integration and its tests are separate work.
