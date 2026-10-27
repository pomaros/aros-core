# Exec context and cleanup

Run `python3 run.py --output /absolute/new/directory` with host Clang.
The fixture extracts the actual `__exec_do_regular` and `__exec_cleanup` bodies.
Modeled DOS calls verify captured cwd/PROGDIR selection, launcher restoration,
retained standard handles, CRT-base restoration after a nested program, command
failure and idempotent preparation rollback. Eight combinations exercise CLI
presence, a program changing directories and a failing command. Four compiled
mutations must fail for their named assertion under ASan/UBSan.

Capture, native lock operations, file reference acquisition, RunCommand and
library startup are boundary models. This does not qualify executable loading,
preparation, complete thread admission, scheduling or a guest.
