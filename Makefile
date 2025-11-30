# Makefile
# Adaptez le chemin si votre cross-compiler est ailleurs
CC = ~/opt/cross/bin/i686-elf-gcc
AS = nasm

CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -ffreestanding -O2 -nostdlib -lgcc

# Sources
SRC = src/boot.s src/kernel.c src/gdt.c src/idt.c src/interrupts.s src/keyboard.c
OBJ = src/boot.o src/kernel.o src/gdt.o src/idt.o src/interrupts.o src/keyboard.o

# Cible finale
alos.bin: $(OBJ)
	$(CC) -T src/linker.ld -o alos.bin $(LDFLAGS) $(OBJ)
	@echo "Build success! Run 'make run' to test."

# Règles de compilation
src/boot.o: src/boot.s
	$(AS) -felf32 $< -o $@

src/gdt.o: src/gdt.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/kernel.o: src/kernel.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/interrupts.o: src/interrupts.s
	$(AS) -felf32 $< -o $@

# Nettoyage
clean:
	rm -f src/*.o alos.bin

# Test rapide avec QEMU
run: alos.bin
	qemu-system-i386 -kernel alos.bin