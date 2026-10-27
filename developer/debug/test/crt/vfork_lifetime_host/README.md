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

The 1152 creation attempts cover cwd snapshot/duplication refusal, context
binding refusal, parent signal exhaustion, creation failure,
setup failure before `CreateNewProc` returns, setup failure with delayed exit,
and successful admission, with and without existing parent flags. An unrelated
child's exit result must remain available. A separate controlled-stack return
releases the child at final acknowledgement and frees the shared data before
returning the saved PID.

Six compiling controls independently restore signal leakage, lost parent flags,
missing reap, missing join, lookup through a dead Task, and lookup after shared
data is freed. Each must fail for the recorded assertion or sanitizer cause.

This is host evidence for the extracted paths, not execution of the launcher,
AROS assembly jumps, library startup, SMP or the linked guest product. Those
contracts require their corresponding build and runtime qualification.


The launcher/proxy protocol has a separate concurrent runner:

```
python3 developer/debug/test/crt/vfork_lifetime_host/handshake.py --output /tmp/vfork-handshake
python3 developer/debug/test/crt/vfork_lifetime_host/handshake.py --tsan --output /tmp/vfork-handshake-tsan
```

It executes the complete production launcher, failed-exec proxy and final parent
return on two host threads. Modeled signals use mutexes and condition variables;
library open/close, exec bodies and parent restoration are fixture boundaries.
The four scenarios check exit status 23, successful exec status 37, 128 failed
exec attempts followed by exit status 43, and 128 failures followed by successful
exec. Six launcher setup failures, including namespace/cwd capture refusal, must
publish the stable child ID before
teardown. The scheduler delays the child after each final acknowledgement until
the parent has freed shared data, making later accesses observable by ASan.

Seven compiling controls check post-publication accesses, premature close,
post-free PID lookup, missing exit status, missing failure acknowledgement,
missing failure reset and late child identity publication. Each must fail for its
named cause; timeouts do not count as successful controls. TSan runs production
and the five controls that do not deliberately dereference freed memory. These
checks do not execute CreateNewProc, assembly jumps or a guest runtime.

## Owned proxy scope

```
python3 developer/debug/test/crt/vfork_lifetime_host/scope.py --output /tmp/vfork-scope
python3 developer/debug/test/crt/vfork_lifetime_host/scope.py --tsan --output /tmp/vfork-scope-tsan
```

This fixture recovers the proxy-scope regression against the current owned
context contract. It compiles the actual vfork binding header, runtime binding
layout and push/pop/snapshot functions, plus both retained heap engines.
Two thousand scope changes cover nesting, LIFO restoration, foreign base/Task
refusal, active operation scope refusal, reference-count overflow and caller
reference release before scope exit. An independently attached worker keeps its
original directory and descriptor table while the creator switches contexts,
and continues after creator release. Final allocations and native lock copies
must balance.

Four compiling mutations remove namespace retention, namespace restoration,
context pop or the owner-Task check. Both sanitizers require a named assertion;
timeout or compilation failure cannot pass a control. Exec task storage, DOS
packets, library disposal and runtime attachment are modeled boundaries. This
checks the scope implementation, not provider admission, generated library
startup, real vfork assembly, forced Task removal or guest execution.
