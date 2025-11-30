# Makefile
# Adaptez le chemin si votre cross-compiler est ailleurs
CC = ~/opt/cross/bin/i686-elf-gcc
AS = nasm

CFLAGS = -std=gnu99 -ffreestanding -O0 -g -Wall -Wextra
ASFLAGS = -felf32 -g
LDFLAGS = -ffreestanding -O0 -nostdlib -lgcc

# Sources
SRC = src/boot.s src/kernel.c src/gdt.c src/idt.c src/interrupts.s src/keyboard.c src/pmm.c src/kheap.c src/console.c
OBJ = src/boot.o src/kernel.o src/gdt.o src/idt.o src/interrupts.o src/keyboard.o src/pmm.o src/kheap.o src/console.o

# Cible finale
alos.bin: $(OBJ)
	$(CC) -T src/linker.ld -o alos.bin $(LDFLAGS) $(OBJ)
	@echo "Build success! Run 'make run' to test."

# Règles de compilation
src/boot.o: src/boot.s
	$(AS) $(ASFLAGS) $< -o $@

src/gdt.o: src/gdt.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/kernel.o: src/kernel.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/interrupts.o: src/interrupts.s
	$(AS) $(ASFLAGS) $< -o $@

# Nettoyage
clean:
	rm -f src/*.o alos.bin

# Test rapide avec QEMU
run: alos.bin
	qemu-system-i386 -kernel alos.bin

# Debug avec QEMU (attend GDB sur port 1234)
debug: alos.bin
	qemu-system-i386 -kernel alos.bin -s -S &
	@echo "QEMU lancé. Connectez GDB avec: target remote localhost:1234"