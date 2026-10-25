/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: EntropyDiag - whether a program gets randomness on this machine:
          entropy.resource is resident, its GetEntropy() fills a buffer,
          and posixc's getentropy(), which OpenSSL and curl seed from,
          answers 0 for 32 bytes. Two reads must differ.

    One verdict line:

      [entropydiag] result=PASS resource=yes info=0x... getentropy=0 distinct=yes

    No byte of the output is printed: only whether reads succeeded and
    differ, and the resource's state word.
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/entropy.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

struct Library *EntropyBase;

int main(void)
{
    UBYTE first[32], second[32], raw[32];
    ULONG info = 0;
    LONG got = -1;
    int rc1, rc2, err = 0, distinct, pass;

    EntropyBase = OpenResource("entropy.resource");
    if (EntropyBase) {
        info = GetEntropyInfo();
        got = GetEntropy(raw, sizeof(raw));
    }
    memset(first, 0, sizeof(first));
    memset(second, 0, sizeof(second));
    errno = 0;
    rc1 = getentropy(first, sizeof(first));
    if (rc1 != 0)
        err = errno;
    rc2 = getentropy(second, sizeof(second));
    distinct = rc1 == 0 && rc2 == 0 && memcmp(first, second, sizeof(first)) != 0;
    pass = EntropyBase != NULL && got >= 0 && rc1 == 0 && rc2 == 0 && distinct;
    Printf("[entropydiag] result=%s resource=%s info=0x%08lx GetEntropy=%ld getentropy=%ld errno=%ld distinct=%s\n",
        (IPTR)(pass ? "PASS" : "FAIL"), (IPTR)(EntropyBase ? "yes" : "no"), (IPTR)info,
        (IPTR)got, (IPTR)rc1, (IPTR)err, (IPTR)(distinct ? "yes" : "no"));
    memset(first, 0, sizeof(first));
    memset(second, 0, sizeof(second));
    memset(raw, 0, sizeof(raw));
    return pass ? RETURN_OK : RETURN_FAIL;
}
