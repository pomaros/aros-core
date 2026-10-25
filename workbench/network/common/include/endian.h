#ifndef _ENDIAN_H_
#define	_ENDIAN_H_

/*
    Copyright (C) 2003-2026, The AROS Development Team. All rights reserved.
    $Id$
*/

/*
    BSD systems like to see BYTE_ORDER and friends.

    FreeBSD 5 in its pedantic namespace also has versions with underscores
    to make it easier to port stuff, create them as well.
*/

#include <aros/system.h>

#if defined(_BIG_ENDIAN)
#   undef  _BIG_ENDIAN
#endif

#define _LITTLE_ENDIAN      1234
#define _BIG_ENDIAN         4321
#define _PDP_ENDIAN         3412

#if AROS_BIG_ENDIAN
#   define _BYTE_ORDER  _BIG_ENDIAN
#else
#   define _BYTE_ORDER  _LITTLE_ENDIAN
#endif

#if __BSD_VISIBLE
#   define LITTLE_ENDIAN    _LITTLE_ENDIAN
#   define BIG_ENDIAN       _BIG_ENDIAN
#   define PDP_ENDIAN       _PDP_ENDIAN
#   define BYTE_ORDER       _BYTE_ORDER
#endif

/*
    Byte-order conversions of POSIX.1-2024, also those of glibc and the BSDs:
    host to big- or little-endian and back, for 16, 32 and 64 bits.
*/
#if !defined(htobe16)
#   if AROS_BIG_ENDIAN
#       define htobe16(x)   ((__UINT16_TYPE__)(x))
#       define htole16(x)   __builtin_bswap16(x)
#       define be16toh(x)   ((__UINT16_TYPE__)(x))
#       define le16toh(x)   __builtin_bswap16(x)
#       define htobe32(x)   ((__UINT32_TYPE__)(x))
#       define htole32(x)   __builtin_bswap32(x)
#       define be32toh(x)   ((__UINT32_TYPE__)(x))
#       define le32toh(x)   __builtin_bswap32(x)
#       define htobe64(x)   ((__UINT64_TYPE__)(x))
#       define htole64(x)   __builtin_bswap64(x)
#       define be64toh(x)   ((__UINT64_TYPE__)(x))
#       define le64toh(x)   __builtin_bswap64(x)
#   else
#       define htobe16(x)   __builtin_bswap16(x)
#       define htole16(x)   ((__UINT16_TYPE__)(x))
#       define be16toh(x)   __builtin_bswap16(x)
#       define le16toh(x)   ((__UINT16_TYPE__)(x))
#       define htobe32(x)   __builtin_bswap32(x)
#       define htole32(x)   ((__UINT32_TYPE__)(x))
#       define be32toh(x)   __builtin_bswap32(x)
#       define le32toh(x)   ((__UINT32_TYPE__)(x))
#       define htobe64(x)   __builtin_bswap64(x)
#       define htole64(x)   ((__UINT64_TYPE__)(x))
#       define be64toh(x)   __builtin_bswap64(x)
#       define le64toh(x)   ((__UINT64_TYPE__)(x))
#   endif
#endif

#endif /* _ENDIAN_H_ */
