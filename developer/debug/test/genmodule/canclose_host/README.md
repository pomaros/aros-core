# CANCLOSELIB host test

`run.py --output NEW_DIR` builds genmodule from `tools/genmodule`, generates
the per-task test library `developer/debug/test/library/userel.conf`, and
runs its generated `OpenLib` and `CloseLib` with the actual
`_set_open_rellibraries_list`, `_set_close_rellibraries_list` and
`set_call_libfuncs`, and a CANCLOSELIB guard that refuses or allows. Exec,
task storage and library loading are mocks in `host.c`, built with
AddressSanitizer and UndefinedBehaviorSanitizer.

Cases:

The generated empty ABORTOPENLIB and CANCLOSELIB sets must link with two
zero entries. A mutant omitting their definitions must fail to link or
return a missing-set failure. This checks the actual generated DEFINESET
output and the production symbol-set macros, separately from the mocks.

1. A refused close, called while another base is current: CloseLib
   returns at once; the guard saw the explicit base; the per-task open
   count, the root's open count, the offset table, the task slot, both
   relative libraries, the module state and the base are all unchanged.
2. The owner's close, allowed: CLOSELIB runs and the base is freed.
3. Two opens by one Task: a refusal keeps both; each allowed close takes
   one.
4. No guard registered: the close behaves as before.

Mutants (each must fail): the guard omitted, placed after the offset table
is set, placed after the per-task count is decremented, or its refusal
ignored.

Not covered: a native module with a CANCLOSELIB function, and the plain
(non-duplicated) library path, which generates the same first statement.
