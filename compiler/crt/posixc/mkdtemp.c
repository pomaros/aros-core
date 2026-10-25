/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 mkdtemp() function
*/

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/*****************************************************************************

    NAME */
#include <stdlib.h>

        char *mkdtemp(

/*  SYNOPSIS */
        char *template)

/*  FUNCTION
        Replaces the trailing XXXXXX of template with characters that make
        a new name, and creates a directory of that name, readable,
        writable and searchable by its owner only.

    INPUTS
        template - a name ending in at least six X characters; it is
                   modified in place

    RESULT
        template on success, NULL with errno set on error: EINVAL when
        the name does not end in six X characters, EEXIST when no free
        name was found, or the error of mkdir().

    NOTES
        The characters come from getentropy(), so two tasks creating
        directories from the same template at the same time do not race
        through the same sequence of names.

    EXAMPLE

    BUGS

    SEE ALSO
        mkstemp(), mkdir()

    INTERNALS

******************************************************************************/
{
    static const char letters[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    unsigned char noise[32];
    size_t length, count = 0;
    char *tail;
    int attempt;

    if (template == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    length = strlen(template);
    while (count < length && template[length - 1 - count] == 'X')
        count++;
    if (count < 6)
    {
        errno = EINVAL;
        return NULL;
    }
    if (count > sizeof(noise))
        count = sizeof(noise);
    tail = template + length - count;

    for (attempt = 0; attempt < 1000; attempt++)
    {
        size_t i;

        if (getentropy(noise, count) != 0)
            return NULL;
        for (i = 0; i < count; i++)
            tail[i] = letters[noise[i] & 63];
        if (mkdir(template, 0700) == 0)
            return template;
        if (errno != EEXIST)
            return NULL;
    }
    errno = EEXIST;
    return NULL;
} /* mkdtemp() */
