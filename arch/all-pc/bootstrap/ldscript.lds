/* Keep the uninitialised paging tables out of the code's file segment.
 * A single -N load segment pads .text past 2 MiB with LLD, hiding the
 * Multiboot headers from the bootloader's first-32-KiB search. */
PHDRS
{
    tables PT_LOAD FLAGS(6);
    code PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x00100000;

    .tables :
    {
	*(.aros.tables)
    } :tables
    
    _prot_lo = . ;
    
    .bsstables :
    {
	*(.bss.aros.tables)
    } :tables
    
    _prot_hi = . ;
    
    .text ALIGN(4096) :
    {
        *(.aros.startup)
        *(.text)
        *(.text.*)
        *(.stub)
        *(.gnu.warning)
        *(.gnu.linkonce.t.*)
    } :code = 0x90909090

    .rodata ALIGN(0x10) :
    {
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
    }
    .rodata1 : { *(.rodata1) }

    .sdata2 : { *(.sdata2) *(.sdata2.*) *(.gnu.linkonce.s2.*) }
    .sbss2  : { *(.sbss2) *(.sbss2.*) *(.gnu.linkonce.sb2.*) }

    .data ALIGN(0x10) :
    {
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
    }

    .data1              : { *(.data1) }
    .eh_frame           : { KEEP (*(.eh_frame)) }
    .gcc_except_table   : { *(.gcc_except_table) }

    .sdata ALIGN(0x10) :
    {
        *(.sdata)
        *(.sdata.*)
        *(.gnu.linkonce.s.*)
    }

    .sbss ALIGN(0x10) :
    {
        *(.sbss)
        *(.sbss.*)
        *(.gnu.linkonce.sb.*)
        *(.scommon)
    }

    .bss ALIGN(0x10) :
    {
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
    }

    _end = . ;
}

