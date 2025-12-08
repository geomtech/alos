/* src/userland/linker.ld - Linker script for userland programs */

ENTRY(_start)

SECTIONS
{
    /* Userland programs start at 4MB usually or 0x400000 like Linux */
    . = 0x400000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}
