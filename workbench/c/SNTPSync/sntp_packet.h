/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: SNTP (RFC 4330) request builder, reply validator and time
 *       conversion, free of OS dependencies so a host test can run it.
 */

#ifndef SNTP_PACKET_H
#define SNTP_PACKET_H

#include <stddef.h>
#include <stdint.h>

#define SNTP_PACKET_SIZE 48
#define SNTP_PORT 123
#define SNTP_UNIX_EPOCH_OFFSET UINT64_C(2208988800)   /* 1900 -> 1970 */
#define SNTP_AMIGA_EPOCH_OFFSET UINT64_C(252460800)    /* 1970 -> 1978 */

enum sntp_status {
    SNTP_OK = 0,
    SNTP_SHORT,          /* fewer than 48 bytes */
    SNTP_MODE,           /* not a server or broadcast reply */
    SNTP_VERSION,        /* NTP version outside 1..4 */
    SNTP_UNSYNCHRONIZED, /* leap indicator 3: clock not synchronized */
    SNTP_STRATUM,        /* kiss-o'-death or invalid stratum */
    SNTP_ORIGINATE,      /* originate timestamp is not our transmit one */
    SNTP_TRANSMIT        /* zero transmit timestamp */
};

struct sntp_time {
    uint32_t seconds;   /* NTP seconds since 1900, modulo 2^32 */
    uint32_t fraction;  /* 2^-32 s */
};

struct sntp_reply {
    unsigned leap;
    unsigned version;
    unsigned mode;
    unsigned stratum;
    struct sntp_time receive;
    struct sntp_time transmit;
};

/* Fill a 48-byte client request; nonce becomes the transmit timestamp. */
void sntp_build_request(uint8_t packet[SNTP_PACKET_SIZE],
    struct sntp_time nonce);

/* Validate a reply against the nonce we sent; fills reply on success. */
enum sntp_status sntp_parse_reply(const uint8_t *packet, unsigned length,
    struct sntp_time nonce, struct sntp_reply *reply);

/*
 * NTP seconds to Unix seconds with era disambiguation: values with the
 * top bit clear belong to era 1 (2036 onwards), the rest to era 0.
 */
uint64_t sntp_to_unix_seconds(uint32_t ntp_seconds);

/* 2^-32 fraction to microseconds, 0..999999. */
uint32_t sntp_fraction_to_micro(uint32_t fraction);

/* Civil date of a Unix time, for the log line. */
void sntp_civil(uint64_t unix_seconds, unsigned *year, unsigned *month,
    unsigned *day, unsigned *hour, unsigned *minute, unsigned *second);

const char *sntp_status_name(enum sntp_status status);

#endif /* SNTP_PACKET_H */
