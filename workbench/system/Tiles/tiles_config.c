/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#include "tiles_config.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int space(unsigned char c) { return c == ' ' || c == '\t'; }
static unsigned char lower(unsigned char c)
{ return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static int equal(const char *a, const char *b)
{
    while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b)) { ++a; ++b; }
    return *a == *b;
}
static int copy(char *out, size_t capacity, const char *in)
{
    size_t n = 0;
    while (in[n]) {
        if (n + 1 >= capacity || (unsigned char)in[n] < 32 || (unsigned char)in[n] == 127) return 0;
        ++n;
    }
    memcpy(out, in, n + 1);
    return 1;
}
static char *trim(char *s)
{
    char *end;
    while (space((unsigned char)*s)) ++s;
    end = s + strlen(s);
    while (end > s && space((unsigned char)end[-1])) --end;
    *end = 0;
    return s;
}
static int decimal(const char *s, unsigned long *number)
{
    unsigned long n = 0;
    if (!*s) return 0;
    for (; *s; ++s) {
        unsigned digit = (unsigned char)*s - '0';
        if (digit > 9 || n > (ULONG_MAX - digit) / 10) return 0;
        n = n * 10 + digit;
    }
    *number = n;
    return 1;
}
static int portchar(unsigned char c)
{
    c = lower(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '.' || c == ':';
}
static int command(char *value, char *port, size_t portSize,
                   char *body, size_t bodySize, int setter)
{
    char *end = value;
    unsigned substitutions = 0;
    while (*end && !space((unsigned char)*end)) {
        if (!portchar((unsigned char)*end)) return 0;
        ++end;
    }
    if (end == value || !*end) return 0;
    *end++ = 0;
    while (space((unsigned char)*end)) ++end;
    if (!*end || !copy(port, portSize, value) || !copy(body, bodySize, end)) return 0;
    if (setter) {
        for (; *end; ++end) if (*end == '%') {
            if (*++end != 'v') return 0;
            ++substitutions;
        }
        if (substitutions != 1) return 0;
    }
    return 1;
}
static int envname(const char *name)
{
    const char *component = name;
    if (!*name || *name == '/') return 0;
    for (;;) {
        if (!*name || *name == '/') {
            size_t n = (size_t)(name - component);
            if (!n || (n == 1 && component[0] == '.') ||
                (n == 2 && component[0] == '.' && component[1] == '.')) return 0;
            if (!*name) return 1;
            component = name + 1;
        } else if (!portchar((unsigned char)*name) || *name == ':') return 0;
        ++name;
    }
}
static int failure(struct TileConfig *config, char *error, size_t size, const char *why)
{
    memset(config, 0, sizeof(*config));
    if (error && size) snprintf(error, size, "%s", why);
    return 0;
}
int TileConfigParse(const char *const *toolTypes, struct TileConfig *config,
                    char *error, size_t errorSize)
{
    static const char *const keys[] = {"TILE", "SIZE", "LABEL", "GLYPH", "GET", "SET", "ENV", "PANEL", "EVERY", "HOVER"};
    unsigned seen = 0, index;
    if (error && errorSize) error[0] = 0;
    if (!config) return 0;
    memset(config, 0, sizeof(*config));
    config->cellsW = config->cellsH = 1;
    config->every = 60;
    if (!toolTypes) return failure(config, error, errorSize, "missing ToolTypes");
    for (index = 0; index < TILE_TOOLTYPE_LIMIT && toolTypes[index]; ++index) {
        char line[TILE_TOOLTYPE_LENGTH], *key, *value, *equals;
        size_t length = 0;
        unsigned k;
        while (length < sizeof(line) && toolTypes[index][length]) ++length;
        if (length == sizeof(line)) return failure(config, error, errorSize, "ToolType too long");
        memcpy(line, toolTypes[index], length + 1);
        key = trim(line);
        if (!*key || *key == '(') continue; /* icon convention: disabled ToolType */
        equals = strchr(key, '=');
        if (equals) { *equals = 0; value = trim(equals + 1); key = trim(key); }
        else value = NULL;
        for (k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k) if (equal(key, keys[k])) break;
        if (k == sizeof(keys) / sizeof(keys[0])) continue;
        if (seen & (1u << k)) return failure(config, error, errorSize, "duplicate tile ToolType");
        seen |= 1u << k;
        if (!value || !*value) {
            if (k == 1) { config->warnings |= TILE_CONFIG_WARN_SIZE; continue; }
            return failure(config, error, errorSize, "empty tile ToolType");
        }
        switch (k) {
        case 0:
            if (equal(value, "toggle")) config->templateKind = TILE_TOGGLE;
            else if (equal(value, "gauge")) config->templateKind = TILE_GAUGE;
            else if (equal(value, "slider")) config->templateKind = TILE_SLIDER;
            else if (equal(value, "value")) config->templateKind = TILE_VALUE;
            else if (equal(value, "glyph")) config->templateKind = TILE_GLYPH;
            else if (equal(value, "eyes")) config->templateKind = TILE_EYES;
            else if (equal(value, "weather")) config->templateKind = TILE_WEATHER;
            else return failure(config, error, errorSize, "unknown tile template");
            break;
        case 1:
            if (strlen(value) == 3 && value[0] >= '1' && value[0] <= '4' &&
                (value[1] == 'x' || value[1] == 'X') && value[2] >= '1' && value[2] <= '4') {
                config->cellsW = value[0] - '0'; config->cellsH = value[2] - '0';
            } else config->warnings |= TILE_CONFIG_WARN_SIZE;
            break;
        case 2:
            if (!copy(config->label, sizeof(config->label), value)) goto oversized;
            break;
        case 3:
            if (!copy(config->glyph, sizeof(config->glyph), value)) goto oversized;
            break;
        case 4:
            if (!command(value, config->getPort, sizeof(config->getPort), config->getCommand, sizeof(config->getCommand), 0))
                return failure(config, error, errorSize, "malformed GET");
            break;
        case 5:
            if (!command(value, config->setPort, sizeof(config->setPort), config->setCommand, sizeof(config->setCommand), 1))
                return failure(config, error, errorSize, "malformed SET (one %v required)");
            break;
        case 6:
            if (!envname(value)) return failure(config, error, errorSize, "invalid ENV variable");
            if (!copy(config->env, sizeof(config->env), value)) goto oversized;
            break;
        case 7:
            if (!copy(config->panel, sizeof(config->panel), value)) goto oversized;
            break;
        case 9:
            if (equal(value, "OFF")) config->hoverCadence = 0;
            else if (equal(value, "ON")) config->pointerEnabled = 1;
            else if (equal(value, "FAST")) { config->pointerEnabled = 1; config->hoverCadence = 33; }
            else return failure(config, error, errorSize, "invalid HOVER (OFF/ON/FAST)");
            break;
        case 8:
            if (!decimal(value, &config->every)) return failure(config, error, errorSize, "invalid EVERY seconds");
            if (config->every < 60) config->every = 60;
            break;
        }
    }
    if (index == TILE_TOOLTYPE_LIMIT) return failure(config, error, errorSize, "too many ToolTypes");
    if (!(seen & 1u)) return failure(config, error, errorSize, "missing TILE template");
    if (config->templateKind == TILE_EYES) {
        if (config->getPort[0] || config->env[0] || config->setPort[0])
            return failure(config, error, errorSize, "eyes uses an internal local source");
        return 1;
    }
    if (!!config->getPort[0] == !!config->env[0])
        return failure(config, error, errorSize, "exactly one GET or ENV required");
    return 1;
oversized:
    return failure(config, error, errorSize, "tile field too long or contains controls");
}
int TileValueParse(enum TileTemplate kind, const char *text, struct TileValue *value)
{
    char buffer[256], *clean;
    size_t length = 0;
    unsigned long number;
    if (!value) return 0;
    memset(value, 0, sizeof(*value));
    if (!text || kind < TILE_TOGGLE || kind > TILE_WEATHER) return 0;
    while (length < sizeof(buffer) && text[length]) ++length;
    if (length == sizeof(buffer)) return 0;
    memcpy(buffer, text, length + 1);
    clean = trim(buffer);
    if (kind == TILE_EYES) return !*clean;
    if (!*clean || !copy(value->text, sizeof(value->text), clean)) goto invalid;
    if (kind == TILE_TOGGLE) {
        if (equal(clean, "on") || equal(clean, "1")) value->boolean = 1;
        else if (!equal(clean, "off") && !equal(clean, "0")) goto invalid;
        value->number = value->boolean;
    } else if (kind == TILE_GAUGE || kind == TILE_SLIDER) {
        if (!decimal(clean, &number) || number > 100) goto invalid;
        value->number = (long)number;
    }
    if (kind == TILE_WEATHER) {
        char *separator = strchr(clean, '|'), *digits = clean;
        int negative = 0;
        if (!separator || strchr(separator + 1, '|')) goto invalid;
        *separator++ = 0;
        if (*digits == '-' || *digits == '+') negative = *digits++ == '-';
        if (!decimal(digits, &number) || number > 100) goto invalid;
        if (!equal(separator, "sunny") && !equal(separator, "cloudy") &&
            !equal(separator, "rain") && !equal(separator, "snow") &&
            !equal(separator, "storm") && !equal(separator, "fog")) goto invalid;
        value->number = negative ? -(long)number : (long)number;
        copy(value->text, sizeof(value->text), separator);
        for (char *p = value->text; *p; ++p) *p = lower((unsigned char)*p);
    }
    return 1;
invalid:
    memset(value, 0, sizeof(*value));
    return 0;
}
