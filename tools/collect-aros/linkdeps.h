/* Copyright (C) 2026 John KNIPPER. All rights reserved.
 * SPDX-License-Identifier: APL-1.1
 * Keep the real linker's inputs across both collect-aros link stages.
 */
static const char *depfile, *deptarget, *depoutput;
static char *depstage, *deppending;
static char **depinputs;
static size_t depcount;
static int depcomplete;

static int dependency_same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    const char *base_a, *base_b;
    if (!b)
        return 0;
    if (!strcmp(a, b))
        return 1;
    base_a = strrchr(a, '/');
    base_b = strrchr(b, '/');
    /* Linkers normalize paths (including TMPDIR's trailing slash). Avoid
     * stat calls for ordinary inputs, then compare temporary-file identity.
     */
    if (strcmp(base_a ? base_a + 1 : a, base_b ? base_b + 1 : b))
        return 0;
    return !stat(a, &sa) && !stat(b, &sb) &&
           sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static void dependency_add(const char *name)
{
    size_t i;
    char **inputs;

    if (dependency_same_file(name, tempoutput) ||
        dependency_same_file(name, ldscriptname))
        return;
    for (i = 0; i < depcount; ++i)
        if (!strcmp(depinputs[i], name))
            return;
    inputs = xmalloc((depcount + 1) * sizeof(*inputs));
    if (depcount)
        memcpy(inputs, depinputs, depcount * sizeof(*inputs));
    free(depinputs);
    depinputs = inputs;
    depinputs[depcount] = xmalloc(strlen(name) + 1);
    strcpy(depinputs[depcount++], name);
}

/* GNU ld and lld write one continued rule, followed by empty rules for
 * removed inputs. Decode its prerequisites before comparing temporary paths.
 */
static void dependency_read(void)
{
    FILE *file = fopen(depstage, "rb");
    long length = 0;
    char *data, *p, *word, *w;

    if (!file)
        fatal(depstage, strerror(errno));
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET))
        fatal(depstage, "cannot size linker dependencies");
    data = xmalloc((size_t)length + 1);
    if (fread(data, 1, (size_t)length, file) != (size_t)length)
        fatal(depstage, "cannot read linker dependencies");
    fclose(file);
    data[length] = 0;
    for (p = data; *p; ++p)
    {
        if (*p == '\\' && p[1])
            ++p;
        else if (*p == ':' && (p[1] == ' ' || p[1] == '\t' || p[1] == '\n'))
            break;
    }
    if (!*p)
        fatal(depstage, "missing linker dependency rule");
    ++p;
    word = xmalloc((size_t)length + 1);
    while (*p && *p != '\n' && *p != '\r')
    {
        if (*p == ' ' || *p == '\t')
        {
            ++p;
            continue;
        }
        if (p[0] == '\\' && (p[1] == '\n' || p[1] == '\r'))
        {
            p += 2;
            if (p[-1] == '\r' && *p == '\n')
                ++p;
            continue;
        }
        w = word;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
        {
            if (*p == '\\' && p[1])
            {
                if (p[1] == '\n' || p[1] == '\r')
                    break;
                ++p;
            }
            else if (*p == '$' && p[1] == '$')
                ++p;
            *w++ = *p++;
        }
        *w = 0;
        if (*word)
            dependency_add(word);
    }
    free(word);
    free(data);
}

static void dependency_quote(FILE *file, const char *name)
{
    for (; *name; ++name)
    {
        if (*name == '$')
            fputc('$', file);
        else if (*name == ' ' || *name == '\t' || *name == '#' ||
                 *name == ':' || *name == '\\')
            fputc('\\', file);
        fputc(*name, file);
    }
}

static void dependency_finish(void)
{
    FILE *file;
    size_t i;

    if (!depfile)
        return;
    deppending = xmalloc(strlen(depfile) + sizeof(".tmp"));
    sprintf(deppending, "%s.tmp", depfile);
    file = fopen(deppending, "w");
    if (!file)
        fatal(deppending, strerror(errno));
    dependency_quote(file, deptarget);
    fputc(':', file);
    for (i = 0; i < depcount; ++i)
    {
        fputs(" \\\n  ", file);
        dependency_quote(file, depinputs[i]);
    }
    fputc('\n', file);
    for (i = 0; i < depcount; ++i)
    {
        dependency_quote(file, depinputs[i]);
        fputs(":\n", file);
    }
    if (ferror(file) || fclose(file))
        fatal(deppending, "cannot write linker dependencies");
    if (rename(deppending, depfile))
        fatal(depfile, strerror(errno));
    depcomplete = 1;
}
