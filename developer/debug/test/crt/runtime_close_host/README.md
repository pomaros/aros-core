# Runtime close ownership

Run `python3 run.py --output /absolute/new/directory` with host Clang.
The fixture executes the actual POSIXC `runtime_can_close` guard with modeled
Task identity, active binding and generated per-task open-count metadata.
Thirty-two combinations check that a foreign Task cannot close the runtime,
a temporary pin can close during an active scope, and final close requires
the current binding with no active operation or vfork context scope. It also
checks missing owner identity and failed-open rollback. Three compiled
mutations must fail for their named assertion under ASan/UBSan.

The generated metadata getter and OpenLib/CloseLib machinery have their separate
[generator regression](../../genmodule/canclose_host/README.md). Neither fixture
qualifies a complete admitted worker, loader teardown or guest scheduling.

Run `python3 source.py --output /absolute/new/directory` for the actual POSIXC
provider selection/capture wrappers and admission capture/leave. It changes the
current instance between selection and capture, checks owned context/table
references, retires the selected creator before Leave and exercises allocation,
reference and owner refusal. Three compiled controls must fail for their named
cause under ASan/UBSan. Binding, reference and allocator primitives are modeled.

Run `python3 bootstrap.py --output /absolute/new/directory` for the actual native
initial-directory helper. A predecessor's logical resolver must not override a
new Process's native cwd; native duplication refusal must not fall back to a
different directory. Boot-root and plain-Task helper paths and helper failure
are checked. Two compiling controls must fail for their named cause. DOS and
context-helper operations are models; this does not qualify full runtime open.
