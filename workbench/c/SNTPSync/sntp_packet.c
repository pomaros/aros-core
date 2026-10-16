/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: SNTP (RFC 4330) packet handling, OS-independent.
 */

#include "sntp_packet.h"

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint32_t get_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

void sntp_build_request(uint8_t packet[SNTP_PACKET_SIZE],
    struct sntp_time nonce)
{
    unsigned index;

    for (index = 0; index < SNTP_PACKET_SIZE; ++index)
        packet[index] = 0;
    /* LI 0, version 4, mode 3 (client). */
    packet[0] = (uint8_t)((4U << 3) | 3U);
    put_be32(packet + 40, nonce.seconds);
    put_be32(packet + 44, nonce.fraction);
}

enum sntp_status sntp_parse_reply(const uint8_t *packet, unsigned length,
    struct sntp_time nonce, struct sntp_reply *reply)
{
    struct sntp_reply parsed;

    if (packet == NULL || length < SNTP_PACKET_SIZE)
        return SNTP_SHORT;
    parsed.leap = (packet[0] >> 6) & 3U;
    parsed.version = (packet[0] >> 3) & 7U;
    parsed.mode = packet[0] & 7U;
    parsed.stratum = packet[1];
    parsed.receive.seconds = get_be32(packet + 32);
    parsed.receive.fraction = get_be32(packet + 36);
    parsed.transmit.seconds = get_be32(packet + 40);
    parsed.transmit.fraction = get_be32(packet + 44);
    if (parsed.mode != 4U && parsed.mode != 5U)
        return SNTP_MODE;
    if (parsed.version < 1U || parsed.version > 4U)
        return SNTP_VERSION;
    if (parsed.leap == 3U)
        return SNTP_UNSYNCHRONIZED;
    if (parsed.stratum == 0 || parsed.stratum > 15U)
        return SNTP_STRATUM;
    if (parsed.mode == 4U &&
        (get_be32(packet + 24) != nonce.seconds ||
         get_be32(packet + 28) != nonce.fraction))
        return SNTP_ORIGINATE;
    if (parsed.transmit.seconds == 0 && parsed.transmit.fraction == 0)
        return SNTP_TRANSMIT;
    if (reply != NULL)
        *reply = parsed;
    return SNTP_OK;
}

uint64_t sntp_to_unix_seconds(uint32_t ntp_seconds)
{
    uint64_t seconds = ntp_seconds;

    if ((ntp_seconds & UINT32_C(0x80000000)) == 0)
        seconds += UINT64_C(0x100000000);
    return seconds - SNTP_UNIX_EPOCH_OFFSET;
}

uint32_t sntp_fraction_to_micro(uint32_t fraction)
{
    return (uint32_t)(((uint64_t)fraction * UINT64_C(1000000)) >> 32);
}

void sntp_civil(uint64_t unix_seconds, unsigned *year, unsigned *month,
    unsigned *day, unsigned *hour, unsigned *minute, unsigned *second)
{
    /* Howard Hinnant's civil-from-days, shifted to a March-based year. */
    uint64_t days = unix_seconds / 86400U;
    uint64_t rem = unix_seconds % 86400U;
    int64_t z = (int64_t)days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint64_t doe = (uint64_t)(z - era * 146097);
    uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint64_t mp = (5 * doy + 2) / 153;
    unsigned d = (unsigned)(doy - (153 * mp + 2) / 5 + 1);
    unsigned m = (unsigned)(mp < 10 ? mp + 3 : mp - 9);

    if (m <= 2)
        ++y;
    *year = (unsigned)y;
    *month = m;
    *day = d;
    *hour = (unsigned)(rem / 3600U);
    *minute = (unsigned)((rem % 3600U) / 60U);
    *second = (unsigned)(rem % 60U);
}

const char *sntp_status_name(enum sntp_status status)
{
    switch (status) {
    case SNTP_OK: return "ok";
    case SNTP_SHORT: return "short reply";
    case SNTP_MODE: return "not a server reply";
    case SNTP_VERSION: return "unsupported NTP version";
    case SNTP_UNSYNCHRONIZED: return "server clock not synchronized";
    case SNTP_STRATUM: return "kiss-o'-death or bad stratum";
    case SNTP_ORIGINATE: return "originate timestamp mismatch";
    case SNTP_TRANSMIT: return "zero transmit timestamp";
    }
    return "unknown";
}
