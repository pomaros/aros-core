/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
ENTRY(_start)
SECTIONS {
 . = 0x40080000;
 .text : { KEEP(*(.text.boot)) *(.text .text.*) }
 __kernel_text_end = .;
 . = ALIGN(4096);
 .guest : { *(.guest) }
 .rodata : { *(.rodata .rodata.*) }
 .data : { *(.data .data.*) }
 . = ALIGN(4096);
 __bss_start = .;
 .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
 __bss_end = .;
 . = ALIGN(16);
 . += 32768;
 __stack_top = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
