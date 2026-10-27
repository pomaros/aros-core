<!-- Copyright (C) 2026 John KNIPPER. SPDX-License-Identifier: APL-1.1 -->

# Native CurrentDir with POSIXC loaded

```
python3 developer/debug/test/crt/native_cwd_host/run.py --output /tmp/native-cwd
```

Use a new output directory. The test compiles the production owned-directory,
context, native-CWD synchronization and scoped-snapshot functions. DOS locks,
Task identity and allocation boundaries are modeled. It verifies adoption of
native Process directory changes, preservation of a shared logical POSIX cwd
when the native directory has not changed, stable in-flight scopes, ordinary
Task bounds, root transitions, retry after lock-copy refusal and final binding cleanup.
The production policy declines unscoped native Process operations with an
exclusive cwd; an explicit operation or selected-context scope and an ordinary
Task must remain mediated. Actual context push/pop keeps its selected snapshot
independent of native directory changes and restores synchronization on exit. Seven compiling
mutations must fail for named assertions under ASan/UBSan; compilation failure
or timeout is not a passing negative control.

This is a source-level ownership/selection regression, not guest filesystem
or network acceptance. The linked runtime-context client and full VZ desktop
must exercise the actual DOS path resolver and NetDB include on the target ABI.
