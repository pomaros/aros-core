<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me> -->
<!-- Licensed under the AROS Public License (APL) Version 1.1. -->

# Continuation identity transition fixture

The runner extracts the production code-identity helpers and the SUBMIT,
COLLECT, CALLBACK_RETURN, FAULT_HANDLER and FAULT_RESUME cases. Synthetic page
identity and service functions provide deterministic replacement/refusal cases.
No hardware MMU, scheduler, native exception entry or SMP behavior is modeled.

Run `python3 run.py --out /absolute/fresh/path/outside/source`.
The sanitized baseline must preserve whole saved frames and reject stale page,
generation, reservation, permission and unaligned-PC identities. Eight separately
compiled semantic mutations must fail the intended assertion. A failed submit
must preserve an existing pending callback identity. An active handler cannot
unregister; a revoked registered handler identity fails later validation.

Guest acceptance must separately exercise dynamically generated handlers,
faulting code, callback entry and return while a native controller replaces or
revokes those pages. This fixture is not evidence of those AROS runtime paths.
