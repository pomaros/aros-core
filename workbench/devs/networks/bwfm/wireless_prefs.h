/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Reader and writer for ENV:Sys/Wireless.prefs, the wpa_supplicant-style
 *       file the network preferences write:
 *
 *           network={
 *               ssid="name"
 *               psk="passphrase"
 *               key_mgmt=WPA-PSK
 *           }
 *
 *       Header-only and free of OS calls so bwfm.device, the Wi-Fi
 *       preferences and a host test share one implementation. Only the
 *       first network block is used; a quoted psk is a passphrase, a bare
 *       64-digit psk is a raw PSK the firmware supplicant does not take.
 */

#ifndef WIRELESS_PREFS_H
#define WIRELESS_PREFS_H

#include <stddef.h>

#define WIRELESS_PREFS_PATH "Sys/Wireless.prefs"
#define WIRELESS_SSID_MAX 32
#define WIRELESS_PSK_MAX 63

struct wireless_prefs {
    char ssid[WIRELESS_SSID_MAX + 1];
    char psk[WIRELESS_PSK_MAX + 1];
    unsigned ssid_len;
    unsigned psk_len;
    unsigned psk_is_hex;   /* bare 64 hex digits: a raw PSK, unsupported */
};

static size_t wireless_prefs_skip_space(const char *text, size_t at,
    size_t end)
{
    while (at < end && (text[at] == ' ' || text[at] == '\t'))
        ++at;
    return at;
}

/* Copy a quoted or bare value at `at` into out; returns the length. */
static unsigned wireless_prefs_value(const char *text, size_t at,
    size_t end, char *out, unsigned max, unsigned *quoted)
{
    unsigned length = 0;

    *quoted = 0;
    if (at < end && text[at] == '"') {
        *quoted = 1;
        ++at;
        while (at < end && text[at] != '"' && text[at] != '\n' &&
                text[at] != '\r') {
            if (length < max)
                out[length++] = text[at];
            ++at;
        }
    } else {
        while (at < end && text[at] != '\n' && text[at] != '\r' &&
                text[at] != ' ' && text[at] != '\t') {
            if (length < max)
                out[length++] = text[at];
            ++at;
        }
    }
    out[length] = '\0';
    return length;
}

static int wireless_prefs_line_is(const char *text, size_t at, size_t end,
    const char *key)
{
    size_t index = 0;

    while (key[index] != '\0') {
        if (at + index >= end || text[at + index] != key[index])
            return 0;
        ++index;
    }
    return 1;
}

/*
 * Parse the first network block. Returns 1 when an SSID was found. A
 * block without psk is an open network (psk_len 0).
 */
static int wireless_prefs_parse(const char *text, size_t length,
    struct wireless_prefs *prefs)
{
    size_t at = 0;
    int in_block = 0;
    unsigned quoted;
    unsigned index;

    prefs->ssid[0] = '\0';
    prefs->psk[0] = '\0';
    prefs->ssid_len = 0;
    prefs->psk_len = 0;
    prefs->psk_is_hex = 0;
    if (text == NULL)
        return 0;
    while (at < length) {
        size_t line = wireless_prefs_skip_space(text, at, length);

        if (!in_block) {
            if (wireless_prefs_line_is(text, line, length, "network={"))
                in_block = 1;
        } else if (line < length && text[line] == '}') {
            break;
        } else if (wireless_prefs_line_is(text, line, length, "ssid=")) {
            prefs->ssid_len = wireless_prefs_value(text, line + 5, length,
                prefs->ssid, WIRELESS_SSID_MAX, &quoted);
        } else if (wireless_prefs_line_is(text, line, length, "psk=")) {
            prefs->psk_len = wireless_prefs_value(text, line + 4, length,
                prefs->psk, WIRELESS_PSK_MAX, &quoted);
            if (!quoted && prefs->psk_len == 63) {
                /* 64 hex digits were cut to 63: a raw PSK. */
                prefs->psk_is_hex = 1;
            } else if (!quoted) {
                prefs->psk_is_hex = 1;
                for (index = 0; index < prefs->psk_len; ++index) {
                    char c = prefs->psk[index];

                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F')))
                        prefs->psk_is_hex = 0;
                }
            }
        }
        while (at < length && text[at] != '\n')
            ++at;
        ++at;
    }
    return prefs->ssid_len > 0;
}

/* Write one network block; returns the length, or 0 when out is too small. */
static size_t wireless_prefs_format(const char *ssid, const char *psk,
    char *out, size_t size)
{
    static const char head[] = "network={\n\tssid=\"";
    static const char psk_head[] = "\"\n\tpsk=\"";
    static const char psk_tail[] = "\"\n\tkey_mgmt=WPA-PSK\n}\n";
    static const char open_tail[] = "\"\n\tkey_mgmt=NONE\n}\n";
    const char *parts[5];
    size_t count = 0;
    size_t total = 0;
    size_t index;

    parts[count++] = head;
    parts[count++] = ssid;
    if (psk != NULL && psk[0] != '\0') {
        parts[count++] = psk_head;
        parts[count++] = psk;
        parts[count++] = psk_tail;
    } else {
        parts[count++] = open_tail;
    }
    for (index = 0; index < count; ++index) {
        const char *part = parts[index];

        while (*part != '\0') {
            if (total + 1 >= size)
                return 0;
            out[total++] = *part++;
        }
    }
    out[total] = '\0';
    return total;
}

#endif /* WIRELESS_PREFS_H */
