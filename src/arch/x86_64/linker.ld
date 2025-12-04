/* src/arch/x86_64/linker.ld - Linker script for x86-64 higher half kernel */

/* Entry point - Limine will jump here */
ENTRY(kmain)

/* Higher half virtual address base */
/* Limine maps the kernel here by default */
KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;

/* Physical load address (1 MiB) */
KERNEL_PHYS_BASE = 0x100000;

PHDRS
{
    text    PT_LOAD FLAGS(5);   /* R-X */
    rodata  PT_LOAD FLAGS(4);   /* R-- */
    data    PT_LOAD FLAGS(6);   /* RW- */
}

SECTIONS
{
    /* Start at higher half address */
    . = KERNEL_VIRT_BASE;
    
    /* Kernel start symbol */
    _kernel_start = .;
    
    /* Limine requests section - MUST be first and in its own segment */
    .limine_requests : AT(ADDR(.limine_requests) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :text
    
    /* Code section */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.text .text.*)
    } :text
    
    /* Read-only data */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.rodata .rodata.*)
    } :rodata
    
    /* Read-write data */
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.data .data.*)
    } :data
    
    /* BSS (uninitialized data) */
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    } :data
    
    /* Kernel end symbol (aligned to page) */
    . = ALIGN(4K);
    _kernel_end = .;
    
    /* Calculate kernel size */
    _kernel_size = _kernel_end - _kernel_start;
    _kernel_phys_end = _kernel_end - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE;
    
    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
