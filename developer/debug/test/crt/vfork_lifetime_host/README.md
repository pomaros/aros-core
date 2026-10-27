# vfork creation and return lifetime regression

Run from the core checkout:

```
python3 developer/debug/test/crt/vfork_lifetime_host/create.py --output /tmp/vfork-lifetime
```

The output directory must not exist. The runner compiles the actual complete
`parent_createchild` and `__vfork_exit_controlled_stack` functions, the internal
`vfork_data` layout, the UP Exec `ChildWait`, `ChildFree`, `ChildStatus` bodies,
and `waitpid`. The fixture models DOS process creation, Task execution,
signalling and startup restoration, while retaining real intrusive child and
notification lists. ASan and UBSan check memory accesses.

The 640 creation attempts cover parent signal exhaustion, creation failure,
setup failure before `CreateNewProc` returns, setup failure with delayed exit,
and successful admission, with and without existing parent flags. An unrelated
child's exit result must remain available. A separate controlled-stack return
runs after the child Task is destroyed and frees the shared data before returning
the saved PID.

Six compiling controls independently restore signal leakage, lost parent flags,
missing reap, missing join, lookup through a dead Task, and lookup after shared
data is freed. Each must fail for the recorded assertion or sanitizer cause.

This is host evidence for the extracted paths, not execution of the launcher,
AROS assembly jumps, library startup, SMP or the linked guest product. Those
contracts require their corresponding build and runtime qualification.
