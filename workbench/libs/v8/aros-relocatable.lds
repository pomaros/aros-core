/*
 * AROS loads ET_REL executables one allocatable section at a time. Large C++
 * programs otherwise retain hundreds of thousands of COMDAT sections, and
 * hosted executable-page allocations can place .eh_frame more than 2 GiB from
 * the code addressed by its signed PC-relative relocations.
 *
 * Coalesce the ordinary section families at the final relocatable link. Keep
 * unwind metadata and read-only constants with code so their PC-relative
 * references are intra-hunk. Writable data remains separate.
 * __lcxx_override holds libc++'s replaceable operator new/delete in the
 * clang (arm64) build; malloc_hook holds absl's LowLevelAlloc functions
 * (ABSL_ATTRIBUTE_SECTION), which call into .text: a separate code hunk
 * would be loaded out of their branches' reach.
 */
SECTIONS
{
  .text : {
    *(.text .text.* __lcxx_override malloc_hook)
    *(.rodata .rodata.*)
    *(.eh_frame)
    *(.gcc_except_table .gcc_except_table.*)
  }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
}
