/* src/linker.ld */
ENTRY(_start)

SECTIONS
{
	/* Le bootloader charge le kernel à 1MB */
	. = 1M;

	/* Symbole de début du kernel */
	_kernel_start = .;

	/* Section Code (.text) */
	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
		*(.text)
	}

	/* Section Données Lecture Seule (.rodata) */
	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	}

	/* Section Données (.data) */
	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data)
	}

	/* Section Variables non initialisées et Stack (.bss) */
	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
	}

	/* Symbole de fin du kernel (aligné sur 4K) */
	. = ALIGN(4K);
	_kernel_end = .;
}