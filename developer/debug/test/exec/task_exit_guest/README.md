# Detached-task guest probe

Build `test-exec-task-exit` and `test-exec-task-exit-negative` through the normal
AROS build to produce `TaskExitProbe` and `TaskExitNoObserver`. Run only in a
disposable supervised guest with a bounded external timeout and debugger.
The positive requires Exec 51.12, task.resource 2.0 and a platform advertising
the post-stack self-exit handoff. Unsupported platforms fail before workers.

Each worker waits for an Exec signal before self-removal. The parent publishes
its identity and independent atomic observer record before signalling it; this
supports scheduling the worker immediately on another CPU. Slots are reused
only after all identities retire and the corresponding callbacks arrive.
The positive requires 160 starts, 160 notifications and no return from RemTask.
Free-memory samples are diagnostics; concurrent system allocations make them
unsuitable as a standalone SMP leak verdict.

The negative omits observer registration. It starts a cohort of 16 self-removing
workers and must fail after the notification deadline with zero notifications.
A failed cohort deliberately retains the command, hook and records for debugger
inspection; no forced remote removal or unload is attempted. The supervisor
must capture the failure and terminate the disposable guest. Refused hook
removal also retains the command. An ordinary successful run removes its hook
synchronously, closes the timer and returns success.

A runner must bind exact Exec/resource/probe binaries and source revisions,
observe the complete command outcome and orderly guest termination for success,
and distinguish the expected negative failure from boot or transport failure.
Compilation alone does not establish guest acceptance. These probes do not
prove remote RemTask quiescence, actor-operation completion or hot reload.
