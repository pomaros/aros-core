# ABORTOPENLIB host test

`run.py --output NEW_DIR` builds genmodule from `tools/genmodule`, generates
the per-task test library `developer/debug/test/library/userel.conf` (two
relative libraries), and links its generated `__GM_DupBase` helpers,
`OpenLib` and `CloseLib` with the actual `_set_open_rellibraries_list`,
`_set_close_rellibraries_list` (`compiler/autoinit/libraries.c`) and
`set_call_libfuncs` (`compiler/libinit/libinit.c`). Exec, task storage and
library loading are mocks in `host.c`, built with AddressSanitizer and
UndefinedBehaviorSanitizer.

Cases:

1. A successful open, then the normal close: OPENLIB and CLOSELIB order
   unchanged, ABORTOPENLIB not called.
2. The first relative library fails: nothing is closed, the root's copied
   relative-library pointers are never closed, the task slot and the offset
   table are restored.
3. The second fails: the first is closed exactly once.
4. An OPENLIB function fails after another one allocated: the abort
   function runs on the failed base while it is still current (offset table
   and task slot), with the relative libraries still open, frees the
   allocation; then the relative libraries close; CLOSELIB never runs.
5. A parent's base in the task slot is restored after a failure.
6. MakeLibrary fails: no hook runs.

Mutants (each must fail): the abort call removed, the abort moved after the
restore, the relative libraries not closed on failure, the offset table or
the task slot not restored, the open's prefix not unwound, the root's
copied slots not cleared before opening, an unwound slot not cleared.

Not covered: a native module build with an ABORTOPENLIB function, devices,
and the non-duplicated library path (those generate the same one-line call).
