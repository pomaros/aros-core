/*
    Copyright (C) 2016-2026, The AROS Development Team. All rights reserved.

    AArch64 (A64) atomic read-modify-write primitives.

    A64 has no teq, hence cbnz. The "w" modifier is required throughout:
    these instructions take a 32-bit register and plain "%0" prints the
    64-bit name.

    All operations are full compiler and hardware fences, including counters.

    Use newlines between instructions: Darwin's AArch64 assembler treats
    a semicolon as a comment, while ELF assemblers accept it as a separator.
 */

#define __AROS_ATOMIC_INC_B(var) \
do { \
   unsigned long temp; \
   int result; \
   __asm__ __volatile__("dmb ish\n1: ldxrb %w0, [%3]\n\tadd %w0, %w0, #1\n\tstxrb %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var) :"memory"); \
} while (0)
#define __AROS_ATOMIC_INC_W(var) \
do { \
   unsigned long temp; \
   int result; \
   __asm__ __volatile__("dmb ish\n1: ldxrh %w0, [%3]\n\tadd %w0, %w0, #1\n\tstxrh %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var) :"memory"); \
} while (0)
#define __AROS_ATOMIC_INC_L(var) \
do { \
   unsigned long temp; \
   int result; \
   __asm__ __volatile__("dmb ish\n1: ldxr %w0, [%3]\n\tadd %w0, %w0, #1\n\tstxr %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var) :"memory"); \
} while (0)

#define __AROS_ATOMIC_DEC_B(var) \
do { \
   unsigned long temp; \
   int result; \
   __asm__ __volatile__("dmb ish\n1: ldxrb %w0, [%3]\n\tsub %w0, %w0, #1\n\tstxrb %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var) :"memory"); \
} while (0)
#define __AROS_ATOMIC_DEC_W(var) \
do { \
   unsigned long temp; \
   int result; \
   __asm__ __volatile__("dmb ish\n1: ldxrh %w0, [%3]\n\tsub %w0, %w0, #1\n\tstxrh %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var) :"memory"); \
} while (0)
#define __AROS_ATOMIC_DEC_L(var) \
do { \
   unsigned long temp; \
   int result; \
   __asm__ __volatile__("dmb ish\n1: ldxr %w0, [%3]\n\tsub %w0, %w0, #1\n\tstxr %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var) :"memory"); \
} while (0)

/*
 * Bitwise atomic RMW with a full barrier: callers like signal.c set
 * tc_SigRecvd then read tc_State, and that load must not hoist above the
 * write. Mask in a register - A64's and/orr take a bitmask immediate.
 */
#define __AROS_ATOMIC_AND_B(var, mask) \
do { \
    unsigned long temp; int result; \
    __asm__ __volatile__("dmb ish\n1: ldxrb %w0, [%3]\n\tand %w0, %w0, %w4\n\tstxrb %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var), "r"(mask) \
                        :"memory"); \
} while(0)
#define __AROS_ATOMIC_AND_W(var, mask) \
do { \
    unsigned long temp; int result; \
    __asm__ __volatile__("dmb ish\n1: ldxrh %w0, [%3]\n\tand %w0, %w0, %w4\n\tstxrh %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var), "r"(mask) \
                        :"memory"); \
} while(0)
#define __AROS_ATOMIC_AND_L(var, mask) \
do { \
    unsigned long temp; int result; \
    __asm__ __volatile__("dmb ish\n1: ldxr %w0, [%3]\n\tand %w0, %w0, %w4\n\tstxr %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var), "r"(mask) \
                        :"memory"); \
} while(0)

#define __AROS_ATOMIC_OR_B(var, mask) \
do { \
    unsigned long temp; int result; \
    __asm__ __volatile__("dmb ish\n1: ldxrb %w0, [%3]\n\torr %w0, %w0, %w4\n\tstxrb %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var), "r"(mask) \
                        :"memory"); \
} while(0)
#define __AROS_ATOMIC_OR_W(var, mask) \
do { \
    unsigned long temp; int result; \
    __asm__ __volatile__("dmb ish\n1: ldxrh %w0, [%3]\n\torr %w0, %w0, %w4\n\tstxrh %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var), "r"(mask) \
                        :"memory"); \
} while(0)
#define __AROS_ATOMIC_OR_L(var, mask) \
do { \
    unsigned long temp; int result; \
    __asm__ __volatile__("dmb ish\n1: ldxr %w0, [%3]\n\torr %w0, %w0, %w4\n\tstxr %w1, %w0, [%3]\n\tcbnz %w1, 1b\n\tdmb ish" \
                        :"=&r"(result), "=&r"(temp), "+Qo"(var) \
                        :"r"(&var), "r"(mask) \
                        :"memory"); \
} while(0)
