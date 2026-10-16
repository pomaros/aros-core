/*
 * Copyright (C) 2026 John KNIPPER
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(AROSAppleStage2Entry)

SECTIONS
{
    . = 0;
    __stage2_image_start = .;

    .text : ALIGN(16)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }
    .vectors : ALIGN(2048)
    {
        KEEP(*(.vectors))
    }
    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }
    .data : ALIGN(4096)
    {
        __stage2_writable_start = .;
        *(.got .got.*)
        *(.data .data.*)
    }

    __stage2_file_end = .;
    .bss (NOLOAD) : ALIGN(16)
    {
        __stage2_bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __stage2_user_stack_start = .;
        . += 65536;
        __stage2_user_stack_end = .;
        . = ALIGN(16);
        __stage2_stack_start = .;
        . += 65536;
        __stage2_stack_end = .;
        __stage2_bss_end = .;
    }
    __stage2_image_end = ALIGN(4096);

    /DISCARD/ :
    {
        *(.comment .note .note.* .eh_frame .llvm_addrsig)
    }
}
