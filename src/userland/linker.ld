/* src/userland/linker.ld - Linker script for userland programs */

ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(7);
    tls PT_TLS FLAGS(4);
}

SECTIONS
{
    /* Userland programs start at 4MB usually or 0x400000 like Linux */
    . = 0x400000;

    .text : {
        *(.text)
        *(.text.*)
    } :image

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :image

    .data : {
        *(.data)
        *(.data.*)
    } :image

    .preinit_array : {
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
    } :image
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } :image
    .fini_array : {
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
    } :image
    .tdata : {
        *(.tdata .tdata.*)
    } :image :tls
    .tbss : {
        *(.tbss .tbss.*)
    } :image :tls

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } :image
}
