# SystemTagList requester policy

Run `python3 run.py --before-ref COMMIT --out /absolute/new/output` on the HOST.
The pre-fix reference is explicit. The runner extracts the actual SystemTagList
NP_WindowPtr expression and the actual tag traversal/application bodies, then
checks explicit suppression/window/default-screen values, chained/skipped tags,
first-match semantics and unchanged defaults. Plain and ASan/UBSan cases must
pass; the predecessor must fail the explicit asynchronous suppression assertion.

These are tag-policy component checks. Process creation, pthread inheritance,
DOS resolution, console policy and native requester suppression need separate
runtime qualification. The source checks only establish that CreateNewProc
inherits an explicit -1 and pthread_create supplies no competing window tag.
