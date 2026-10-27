# Retained directory context

Run `python3 run.py --output /absolute/new/directory`, adding `--tsan` for
ThreadSanitizer. The actual context engine runs with modeled DOS packets,
allocation and semaphore operations. It checks owned lock copies, snapshots
surviving replacement and creator release, 10,000 concurrent changes/reads,
allocation refusal and packet-error rollback. Three compiled mutations remove
retention or lose the handler error; all must fail at runtime.

A capture input is a live caller-owned lock. Retaining an object requires an
existing reference; no caller may race its last release using a borrowed
pointer. Handler operations and final lock release run outside the context
semaphore. A snapshot owns both directory references until explicitly released.
These checks do not qualify runtime bindings, DOS path resolution, pthread
admission or guest execution. Product qualification must exercise those users.
