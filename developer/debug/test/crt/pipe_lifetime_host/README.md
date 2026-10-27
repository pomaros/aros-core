# Pipe creation rollback

Run `python3 run.py --output /absolute/path/outside/source`.

The complete production `pipe()` function executes under ASan/UBSan with modeled
Exec allocation, DOS handles and descriptor reservations. Fail each of four
allocations, either native open and either descriptor reservation. Failure must
release all acquired resources, preserve its original error, leave the caller's
output unchanged and publish neither endpoint. Success transfers both endpoints.
Cleanup deliberately changes both errno and DOS IoErr so accidental error loss
is observable. Two compiling controls remove file-block disposal or final error
restoration and must fail their corresponding invariant.

This does not test actual pipe-handler transport, descriptor concurrency or
native/guest execution. The separate pipe-name and pipe-readiness tests cover
other layers.
