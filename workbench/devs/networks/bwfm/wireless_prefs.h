/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Metadata-only Wireless.prefs. Credentials live in keyring.library.
 *       Shared parser/formatter for the driver, editors and host tests.
 */

#ifndef WIRELESS_PREFS_H
#define WIRELESS_PREFS_H

#include <stddef.h>

#define WIRELESS_PREFS_PATH "Sys/Wireless.prefs"
#define WIRELESS_SSID_MAX 32
#define WIRELESS_PSK_MAX 63

struct wireless_prefs {
    char ssid[WIRELESS_SSID_MAX + 1];
    unsigned ssid_len;
    unsigned protected_network;
};

static inline size_t wireless_prefs_skip_space(const char *text, size_t at,
    size_t end)
{
    while (at < end && (text[at] == ' ' || text[at] == '\t'))
        ++at;
    return at;
}

/* Copy a quoted or bare value at `at` into out; returns the length. */
static inline unsigned wireless_prefs_value(const char *text, size_t at,
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

static inline int wireless_prefs_line_is(const char *text, size_t at, size_t end,
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

static inline int wireless_prefs_key_is(const char *text,size_t at,size_t end,const char *key)
{
    if(!wireless_prefs_line_is(text,at,end,key)) return 0;
    while(*key) { at++;key++; }
    at=wireless_prefs_skip_space(text,at,end);
    return at<end && text[at]=='=';
}
static inline int wireless_prefs_unsupported_secret(const char *text,size_t at,size_t end)
{
    static const char *names[]={"password","private_key_passwd","private_key2_passwd","pin","pin2","new_password"};
    size_t i;
    if(wireless_prefs_line_is(text,at,end,"blob-base64-")) return 1;
    for(i=0;i<sizeof(names)/sizeof(names[0]);i++)
        if(wireless_prefs_key_is(text,at,end,names[i])) return 1;
    return 0;
}

/* Runtime consumers require the explicit one-shot importer for old files. */
static inline int wireless_prefs_has_plaintext(const char *text, size_t length)
{
    size_t at=0;
    if(!text) return 0;
    while(at<length) {
        size_t line=wireless_prefs_skip_space(text,at,length);
        if(line<length && text[line]=='#') line=wireless_prefs_skip_space(text,line+1,length);
        if(wireless_prefs_key_is(text,line,length,"psk") ||
           wireless_prefs_line_is(text,line,length,"wep_key") ||
           wireless_prefs_unsupported_secret(text,line,length)) return 1;
        while(at<length && text[at]!='\n') at++;
        at++;
    }
    return 0;
}

/*
 * Parse the first network block. Returns 1 when an SSID was found. A
 * key_mgmt=WPA-PSK identifies a protected network.
 */
static inline int wireless_prefs_parse(const char *text, size_t length,
    struct wireless_prefs *prefs)
{
    size_t at = 0;
    int in_block = 0;
    unsigned quoted;

    prefs->ssid[0] = '\0';
    prefs->ssid_len = 0;
    prefs->protected_network = 0;
    if (text == NULL || wireless_prefs_has_plaintext(text,length))
        return 0;
    while (at < length) {
        size_t line = wireless_prefs_skip_space(text, at, length);

        if (!in_block) {
            if (wireless_prefs_line_is(text, line, length, "network={"))
                in_block = 1;
        } else if (line < length && text[line] == '}') {
            break;
        } else if (wireless_prefs_line_is(text, line, length, "psk=") ||
                   wireless_prefs_line_is(text, line, length, "wep_key")) {
            return 0; /* Explicit migration imports obsolete cleartext files. */
        } else if (wireless_prefs_line_is(text, line, length, "ssid=")) {
            prefs->ssid_len = wireless_prefs_value(text, line + 5, length,
                prefs->ssid, WIRELESS_SSID_MAX, &quoted);
        } else if (wireless_prefs_line_is(text, line, length, "key_mgmt=WPA-PSK")) {
            prefs->protected_network = 1;
        } else if (wireless_prefs_line_is(text, line, length, "keyring_security=WEP")) {
            prefs->protected_network = 2;
        }
        while (at < length && text[at] != '\n')
            ++at;
        ++at;
    }
    return prefs->ssid_len > 0;
}

/* Write one network block; returns the length, or 0 when out is too small. */
static inline size_t wireless_prefs_format(const char *ssid, unsigned protected_network,
    char *out, size_t size)
{
    static const char head[] = "network={\n\tssid=\"";
    static const char psk_tail[] = "\"\n\tkey_mgmt=WPA-PSK\n}\n";
    static const char open_tail[] = "\"\n\tkey_mgmt=NONE\n}\n";
    const char *parts[5];
    size_t count = 0;
    size_t total = 0;
    size_t index;

    if (ssid == NULL || ssid[0] == 0) return 0;
    for (index = 0; ssid[index]; ++index)
        if (index >= WIRELESS_SSID_MAX || (unsigned char)ssid[index] < 32 ||
            ssid[index] == '"' || ssid[index] == '\\') return 0;
    parts[count++] = head;
    parts[count++] = ssid;
    if (protected_network) {
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
