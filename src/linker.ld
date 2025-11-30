/* src/linker.ld */
ENTRY(_start)

SECTIONS
{
	/* Le bootloader charge le kernel à 1MB */
	. = 1M;

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
}