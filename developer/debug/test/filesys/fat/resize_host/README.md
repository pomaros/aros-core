# FAT resize zero-extension regression

Run from the common core:

```
python3 developer/debug/test/filesys/fat/resize_host/run.py --output /tmp/fat-resize
```

The output directory must not exist. The test compiles the actual
`OpSetFileSize` body under ASan/UBSan. FAT entries, directory updates, allocation,
cache handles and `WriteFileChunk` are modeled boundaries. Dirty cluster storage
makes omitted initialization observable when the larger directory size is
published. Positive short writes force the operation to resume at the actual
written offset.

Cases cover an empty file, unaligned tails, exact cluster boundaries,
multiple clusters, shrink/regrow, dirty reused clusters, preserved prefix and
file position, no-op resize, negative size, and write/access refusals. Injected
allocation, write, zero-progress and directory-update failures require an
unchanged visible size and restored allocation chain. Four compiling controls
remove zero fill, skip short-write bytes, skip the old tail or lose the rollback
link; each must fail its named assertion. Compile failures and timeouts do not
count as detected defects.

This does not execute the real cache or `WriteFileChunk`, establish disk flush
ordering or crash consistency, exercise FAT mirrors, qualify a guest handler,
or prove rollback after a second failure during rollback itself. The production
handler conservatively retains allocated data when its link cannot be restored.
