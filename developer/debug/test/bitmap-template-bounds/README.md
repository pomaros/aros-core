# Bitmap template boundary regression

Copyright (C) 2026 John KNIPPER <code@jkn.me>
Licensed under the AROS Public License (APL), version 1.1.

Run `python3 -B developer/debug/test/bitmap-template-bounds/run.py --out /ABS/NEW-PROOF` from this tree, or omit --out for a temporary proof. This is a small HOST-only build with /usr/bin/clang; it is not an AROS native build or ABI qualification.

The runner extracts the four complete PutMemTemplate8/16/24/32 function bodies directly from the actual production file into a temporary include. It does not translate or substitute their loops. A minimal host type/GC facade supplies AROS-width scalar types and representative color/draw attributes. A byte-by-byte independent pixel oracle checks the entire destination, including unchanged surrounding pixels. Target here is little-endian rendering as used by AArch64; this fixture does not claim big-endian target qualification.

For each format and six drawing/template-inversion combinations, the matrix covers widths1/15/16/17/31/64, srcx0/1/7/15/16/17/31, heights1/2/8, and zero/one/mixed mask bits:9072 cases. The exact word-padded template ends immediately before PROT_NONE in one run. The second run allocates exactly the source extent on the heap and enables AddressSanitizer and UndefinedBehaviorSanitizer. Pixel outputs must match independently. LeakSanitizer is disabled because this macOS ASan runtime does not support it; all fixture allocations are released on successful paths.

The negative control removes only the final-pixel read guards from the extracted
production bodies. Each format executes a 64×8, srcx0, JAM1 case against the
protected boundary. A SIGBUS/SEGV handler accepts only the exact first protected
address, writes a fixed async-safe marker, and returns 90. Any other exit or
address fails. A guard-page fault is mandatory for each mutated format. The
proof labels these bodies as mutations, never as a historical binary or source
revision. Production, fixture and compiled-body hashes remain in the proof.

The product correction reloads a new source word only if another pixel remains in that row. This preserves mask reset and next-word fetch for all consumed pixels, including source-word crossings and multirow templates, while removing the unconsumed reload after the final pixel. The existing width/height early-return and row modulo contracts are unchanged. A target gfx.hidd rebuild and application rendering qualification require separate runtime checks.
