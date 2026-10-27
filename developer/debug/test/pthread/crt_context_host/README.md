# Pthread runtime admission

Run `python3 run.py --output /absolute/new/directory` with host Clang; add
`--sanitizer thread` for TSan instead of ASan/UBSan.

The fixture executes the actual shared CRT header, ThreadInfo, pthread_create,
StarterFunc, slot lookup and admission-token cleanup. Each modeled AROS Task
runs in a host thread. Exec signals, allocation, semaphores, namespace locking,
process creation and two runtime providers are boundary models.

Scenarios cover acknowledgement before successful creation, partial Enter
failure, capture/token/process-creation refusal, creator code lifetime through
admission, worker exit, detached slot retirement, module replacement during
capture, inactive providers, pin refusal and overlong names. Nine compiled
ASan/UBSan mutations and eight TSan mutations must fail for their named cause.
Timeouts and compilation failures never count as successful controls. Runtime
calls from worker teardown are instrumented to refuse calls after Leave.

Registry publication has its own [actual-header fixture](../crt_registry_host/README.md).
This fixture contains no version-1 callbacks or worker-marker compatibility.
It does not execute the POSIXC provider, generated library startup, DOS context
resolver, AROS scheduler or a guest. Those require separate integration gates.
