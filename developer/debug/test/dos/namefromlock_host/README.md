# NameFromLock duplication errors

Run `python3 run.py --output /absolute/path/outside/source` with a fresh output
directory. The gate compiles the actual production function body under
ASan/UBSan with fault-injected DupLock/namefrom_internal/UnLock boundaries.

Seven cases cover exclusive/OOM duplication failure, shared-lock success,
traversal failure, explicit root, an exactly fitting five-byte root buffer and
a short root buffer. Failed duplication must preserve the original error and
leave the output unchanged, without treating NULL as the boot volume. Traversal
errors must survive cleanup. Two compiling controls remove the duplication
check or error restoration and must fail their named assertions.

This checks wrapper behavior, not a native filesystem, concurrent directory
mutation or support for duplicating exclusive locks. An unsupported duplication
fails with the provider error instead of returning another volume's path.
