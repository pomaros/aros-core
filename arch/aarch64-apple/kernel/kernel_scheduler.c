/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include "smp.h"
#define CORE_SCHEDULER_BUG(...) _bug(NULL, __VA_ARGS__)
#define CORE_SCHEDULER_TIME_US() AROSAppleSMPTimeUS()
#define CORE_SCHEDULER_TIME_AVAILABLE() 1
#include "../../aarch64-native/kernel/kernel_scheduler.c"
