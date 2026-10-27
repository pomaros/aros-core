# Retained descriptor namespace

Run `python3 run.py --output /absolute/new/directory`, adding `--tsan` for
ThreadSanitizer. The actual namespace engine runs with modeled allocation,
descriptors and registry close. Eight workers retain the namespace while its
creator releases it. Final disposal must close descriptors before releasing
the owned registry. Allocation refusal is checked. Three compiled mutations
remove retention, close the registry early or omit descriptor disposal; all
must fail at runtime.

Each runtime/admission must own a reference before publication. Retain requires
an existing reference; it cannot revive an unowned borrowed pointer racing its
last release. The disposal callback must remain executable through final release
and handle every populated table entry, including any reserved-entry sentinel
used by its eventual client. The engine invokes it without the table guard.
This fixture does not exercise POSIX descriptor operations, provider callbacks,
pthread admission or a guest. Namespace integration must verify those separately.
