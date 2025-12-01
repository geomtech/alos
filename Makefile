# Makefile
# Adaptez le chemin si votre cross-compiler est ailleurs
CC = ~/opt/cross/bin/i686-elf-gcc
AS = nasm

CFLAGS = -std=gnu99 -ffreestanding -O0 -g -Wall -Wextra
ASFLAGS = -felf32 -g
LDFLAGS = -ffreestanding -O0 -nostdlib -lgcc

# Sources
SRC = src/boot.s src/kernel.c src/gdt.c src/idt.c src/interrupts.s src/keyboard.c src/pmm.c src/kheap.c src/console.c src/pci.c src/drivers/pcnet.c
OBJ = src/boot.o src/kernel.o src/gdt.o src/idt.o src/interrupts.o src/keyboard.o src/pmm.o src/kheap.o src/console.o src/pci.o src/drivers/pcnet.o

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
	rm -f src/*.o src/drivers/*.o alos.bin

# Test rapide avec QEMU (avec carte réseau AMD PCnet connectée en mode user)
run: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M -netdev user,id=net0 -device pcnet,netdev=net0

# Run avec capture de paquets (pour Wireshark)
run-pcap: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M -netdev user,id=net0 -device pcnet,netdev=net0 -object filter-dump,id=dump0,netdev=net0,file=alos-network.pcap
	@echo "Capture sauvée dans alos-network.pcap"

# Run avec TAP (paquets visibles sur l'hôte via Wireshark sur tap0)
# Prérequis: sudo ip tuntap add dev tap0 mode tap user $USER && sudo ip link set tap0 up
run-tap: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device pcnet,netdev=net0

# Debug avec QEMU (attend GDB sur port 1234)
debug: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M -netdev user,id=net0 -device pcnet,netdev=net0 -s -S &
	@echo "QEMU lancé. Connectez GDB avec: target remote localhost:1234"