/* Copyright (C) 2026 John KNIPPER.
 * Licensed under the AROS Public License (APL) Version 1.1. */

#include <aros/sha256.h>
#include <stdio.h>
#include <string.h>

/* Keep file size independent of memory use and of the task's stack size. */
static unsigned char buffer[65536];
static char line[8192];

static int hash_file(const char *name, char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    struct AROSSHA256Context context;
    unsigned char digest[AROS_SHA256_SIZE];
    FILE *file = fopen(name, "rb");
    size_t count, i;
    int failed;
    if (!file) {
        fprintf(stderr, "SHA256Sum: cannot open %s\n", name);
        return 0;
    }
    AROSSHA256Init(&context);
    while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0)
        AROSSHA256Update(&context, buffer, count);
    failed = ferror(file);
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        fprintf(stderr, "SHA256Sum: read failed: %s\n", name);
        return 0;
    }
    AROSSHA256Final(&context, digest);
    for (i = 0; i < sizeof(digest); ++i) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[64] = 0;
    return 1;
}

static int check_manifest(const char *name)
{
    FILE *manifest = fopen(name, "rb");
    unsigned long checked = 0, failed = 0, number = 0;
    char hex[65];
    size_t length, i;
    int malformed;
    if (!manifest) {
        fprintf(stderr, "SHA256Sum: cannot open manifest %s\n", name);
        return 20;
    }
    while (fgets(line, sizeof(line), manifest)) {
        ++number;
        length = strlen(line);
        /* Require complete records, including the final newline. */
        malformed = length < 68 || line[length - 1] != '\n';
        if (!malformed) {
            line[--length] = 0;
            if (length && line[length - 1] == '\r') line[--length] = 0;
            malformed = length < 67 || line[64] != ' ' || line[65] != ' ';
            for (i = 0; i < 64 && !malformed; ++i)
                malformed = !((line[i] >= '0' && line[i] <= '9') ||
                              (line[i] >= 'a' && line[i] <= 'f'));
        }
        if (malformed) {
            fprintf(stderr, "SHA256Sum: malformed manifest line %lu\n", number);
            ++failed;
            break;
        }
        ++checked;
        if (!hash_file(line + 66, hex) || memcmp(hex, line, 64) != 0) {
            fprintf(stderr, "SHA256Sum: FAILED: %s\n", line + 66);
            ++failed;
        }
    }
    if (ferror(manifest)) ++failed;
    if (fclose(manifest) != 0) ++failed;
    if (!checked || failed) {
        fprintf(stderr, "SHA256Sum: checked=%lu failed=%lu\n", checked, failed);
        return 20;
    }
    printf("SHA256Sum: verified %lu files\n", checked);
    return 0;
}

int main(int argc, char **argv)
{
    char hex[65];
    int i, result = 0;
    if (argc == 3 && strcmp(argv[1], "--check") == 0)
        result = check_manifest(argv[2]);
    else if (argc < 2 || strcmp(argv[1], "--check") == 0) {
        fprintf(stderr, "Usage: SHA256Sum FILE... | SHA256Sum --check MANIFEST\n");
        result = 20;
    } else {
        for (i = 1; i < argc; ++i) {
            if (strchr(argv[i], '\n') || strchr(argv[i], '\r')) {
                fprintf(stderr, "SHA256Sum: filename contains a newline\n");
                result = 20;
            } else if (hash_file(argv[i], hex))
                printf("%s  %s\n", hex, argv[i]);
            else result = 20;
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) result = 20;
    return result;
}
