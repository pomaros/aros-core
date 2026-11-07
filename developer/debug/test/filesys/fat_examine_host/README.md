# FAT metadata enumeration component check

`run.py --out /ABS/FRESH-OUTPUT` compiles the production EXAMINE_NEXT packet case
with modeled DOS helpers under address and undefined-behavior sanitizers.
An entry held with an exclusive lock must remain enumerable without granting a
second lock, changing ownership or releasing the existing lock. The cases cover
shared/absent locks, parent and entry identity, live metadata, cursor advancement,
EOF, other failures and cleanup. Seven semantic negative controls must fail.

This HOST check does not establish Native packet ABI, mounted filesystem or
concurrency behavior. Native qualification must enumerate all entries around a
file retained open for writing, verify names and types, close the file, clean up
the fixture and observe normal process exit. Merely finding the open file is
insufficient: entries following it must remain visible.
