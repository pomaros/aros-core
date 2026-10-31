# Variable stack mapping component check

Run `python3 run.py --out /absolute/fresh/path/outside/source`.

The sanitized HOST harness extracts the actual stack preparation and unmapping
helpers. It exercises a 17-page stack crossing a table boundary, a three-page
fault stack and TLS. Each possible partial mapping failure must unmap only its
owned pages; an unmap refusal or changed physical identity retains backing for
retry. Missing tables must request replenishment before any mapping mutation.
The same fixture checks three-page immutable image preparation, each partial
mapping failure and retained clone ownership. The separate lifecycle harness checks variable intervals, retained reaping
addresses, guards, size overflow and lifetime accounting.

These are serialized component checks with simulated page tables. They do not
qualify SMP, a native allocator, fault return or AROS execution. The guest
RestrictedTasks large-stacks phase supplies the runtime assertions.
