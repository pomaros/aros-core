/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#ifndef AROS_TEST_VIRGL_PAIR_PROTOCOL_H
#define AROS_TEST_VIRGL_PAIR_PROTOCOL_H
#include <exec/types.h>
#include <exec/ports.h>
/* The child owns this message until the parent replies. Its GL context
 * remains live throughout that interval. */
struct avgl_pair_ready {
    struct Message message;
    ULONG role;
};
#endif
