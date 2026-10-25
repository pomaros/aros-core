<!-- Copyright (C) 2026 John KNIPPER <code@jkn.me>. -->
<!-- Licensed under the AROS Public License (APL), Version 1.1. -->

# Descriptors of concurrent threads stay their own

`fd_threads.c` builds `FdThreads [ROUNDS [DIR [THREADS]]]` (defaults 2000,
`RAM:`, 4). Each thread first opens a file of its own and holds it while the
others open theirs: the program prints the numbers they got and counts a
number held by two threads. Then each thread opens its file, reads it back
through the descriptor, duplicates and closes it, and every eighth round
passes a byte through a pipe of its own. A descriptor that reads another
thread's file or a close that fails means the table handed one number to two
threads. It prints `FDTHREADS result=PASS`.
