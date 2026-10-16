# Clock provider launch ownership regression

Run `python3 run.py --output /tmp/clock-launch-unique` with Clang installed.
The runner extracts current production `start_clock_source` from Tiles/main.c,
retains the complete source/hash, and compiles the function with ASan/UBSan.

The handle model keeps caller input/output distinct from the two private NIL:
handles. It verifies first-open failure, second-open failure with input cleanup,
SystemTags failure with both handles closed, successful child ownership transfer,
and a child exiting and closing both handles before SystemTags returns. The
parent must neither close transferred handles nor affect caller streams.
Three mutation controls must fail: missing SYS_Input, missing SYS_Output, and
parent cleanup after successful ownership transfer.

SystemTags, DOS handles and child execution are modeled here. This checks the
production call and its ownership branches; it does not execute the native DOS
launcher or prove concurrent child scheduling. Actual guest Control/clock proof
is a separate requirement. Existing output directories are refused; compile/run
logs, commands and source/helper hashes are retained in the selected directory.
