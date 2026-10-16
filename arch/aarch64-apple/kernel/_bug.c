/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: The kernel's own formatter, behind every formatted string it
 *       produces. AROS resolves __vcformat() and vsprintf() through StdCBase,
 *       which the kernel never opens, so each call jumped through a null base
 *       and took a data abort at a negative offset. That would have cost the
 *       panic message and KrnFormatStr() exactly when they were needed. This
 *       formatter depends on nothing but the sink it is handed, and it
 *       defines those two names so no call can leave the kernel.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#if defined(__AROS__)
#include <aros/config.h>
#endif

struct KernelBase;

int krnPutC(int chr, struct KernelBase *KernelBase);

/* The formatter writes through this and knows nothing else about its sink. */
typedef int (*bug_sink)(int chr, void *data);

struct bug_spec {
    int left;
    int zero;
    int alternate;
    int width;
    int longs;
};

static int put_string(
    bug_sink out, void *data, const char *text, const struct bug_spec *spec)
{
    int length = 0;
    int written = 0;
    int pad;

    while (text[length] != 0)
        ++length;
    pad = spec->width > length ? spec->width - length : 0;
    if (!spec->left)
        while (pad-- > 0 && ++written)
            out(' ', data);
    while (*text != 0 && ++written)
        out(*text++, data);
    if (spec->left)
        while (pad-- > 0 && ++written)
            out(' ', data);
    return written;
}

static int put_number(
    bug_sink out, void *data, uint64_t value, int negative, unsigned radix,
    int upper, const struct bug_spec *spec)
{
    static const char lower[] = "0123456789abcdef";
    static const char capital[] = "0123456789ABCDEF";
    const char *digits = upper ? capital : lower;
    char buffer[24];
    int length = 0;
    int prefix = 0;
    int pad;
    int written = 0;

    do {
        buffer[length++] = digits[value % radix];
        value /= radix;
    } while (value != 0 && length < (int)sizeof(buffer));
    if (negative)
        prefix = 1;
    else if (spec->alternate && radix == 16)
        prefix = 2;
    pad = spec->width > length + prefix ? spec->width - length - prefix : 0;
    if (!spec->left && !spec->zero)
        while (pad-- > 0 && ++written)
            out(' ', data);
    if (negative && ++written)
        out('-', data);
    else if (prefix == 2) {
        out('0', data);
        out(upper ? 'X' : 'x', data);
        written += 2;
    }
    if (!spec->left && spec->zero)
        while (pad-- > 0 && ++written)
            out('0', data);
    while (length > 0 && ++written)
        out(buffer[--length], data);
    if (spec->left)
        while (pad-- > 0 && ++written)
            out(' ', data);
    return written;
}

static int format_string(
    bug_sink out, void *data, const char *format, va_list args)
{
    int written = 0;

    if (format == NULL)
        return 0;
    while (*format != 0) {
        struct bug_spec spec = {0, 0, 0, 0, 0};
        char conversion;

        if (*format != '%') {
            out(*format++, data);
            ++written;
            continue;
        }
        ++format;
        for (;;) {
            if (*format == '-')
                spec.left = 1;
            else if (*format == '0')
                spec.zero = 1;
            else if (*format == '#')
                spec.alternate = 1;
            else
                break;
            ++format;
        }
        while (*format >= '0' && *format <= '9')
            spec.width = spec.width * 10 + (*format++ - '0');
        if (*format == '.') {
            ++format;
            while (*format >= '0' && *format <= '9')
                ++format;
        }
        while (*format == 'l' || *format == 'z' || *format == 'h' ||
                *format == 'j' || *format == 't') {
            if (*format == 'l' || *format == 'z' || *format == 'j' ||
                    *format == 't')
                ++spec.longs;
            ++format;
        }
        conversion = *format;
        if (conversion == 0)
            break;
        ++format;
        switch (conversion) {
        case '%':
            out('%', data);
            ++written;
            break;
        case 'c':
            out((char)va_arg(args, int), data);
            ++written;
            break;
        case 's': {
            const char *text = va_arg(args, const char *);

            written += put_string(out, data, text != NULL ? text : "(null)", &spec);
            break;
        }
        case 'd':
        case 'i': {
            int64_t value = spec.longs ? va_arg(args, int64_t)
                                       : (int64_t)va_arg(args, int);
            int negative = value < 0;

            written += put_number(out, data,
                negative ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value,
                negative, 10U, 0, &spec);
            break;
        }
        case 'u': {
            uint64_t value = spec.longs ? va_arg(args, uint64_t)
                                        : (uint64_t)va_arg(args, unsigned);

            written += put_number(out, data, value, 0, 10U, 0, &spec);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t value = spec.longs ? va_arg(args, uint64_t)
                                        : (uint64_t)va_arg(args, unsigned);

            written += put_number(out, data, value, 0, 16U, conversion == 'X',
                &spec);
            break;
        }
        case 'p': {
            uint64_t value = (uint64_t)(uintptr_t)va_arg(args, void *);

            spec.alternate = 1;
            written += put_number(out, data, value, 0, 16U, 0, &spec);
            break;
        }
        default:
            out('%', data);
            out(conversion, data);
            written += 2;
            break;
        }
    }
    return written;
}

/* KrnBug() and everything behind it: the kernel's character sink. */
static int sink_kernel(int chr, void *data)
{
    return krnPutC(chr, (struct KernelBase *)data);
}


int krnBug(const char *format, va_list args, void *kernelBase)
{
    if (format == NULL)
        return 0;
    return format_string(sink_kernel, kernelBase, format, args);
}

/*
 * The two names AROS would otherwise resolve through StdCBase. Defining them
 * here keeps KrnFormatStr() and the panic printer inside the kernel, so the
 * message that explains a failure cannot itself be the second failure.
 */
int __vcformat(void *data, int (*outc)(int, void *), const char *format,
    va_list args);

int __vcformat(void *data, int (*outc)(int, void *), const char *format,
    va_list args)
{
    if (outc == NULL || format == NULL)
        return 0;
    return format_string(outc, data, format, args);
}

struct bug_buffer {
    char *cursor;
};

static int sink_buffer(int chr, void *data)
{
    struct bug_buffer *buffer = data;

    *buffer->cursor++ = (char)chr;
    return 1;
}

int vsprintf(char *destination, const char *format, va_list args);

int vsprintf(char *destination, const char *format, va_list args)
{
    struct bug_buffer buffer;
    int written;

    if (destination == NULL)
        return 0;
    buffer.cursor = destination;
    written = format == NULL ? 0
        : format_string(sink_buffer, &buffer, format, args);
    *buffer.cursor = 0;
    return written;
}
