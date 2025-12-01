# Makefile - ALOS Kernel
# Adaptez le chemin si votre cross-compiler est ailleurs
CC = ~/opt/cross/bin/i686-elf-gcc
AS = nasm

# CFLAGS:
# -mno-sse -mno-sse2 -mno-mmx : Désactive les instructions SIMD (évite Invalid Opcode)
# -mno-red-zone : Désactive la red zone (nécessaire pour les interruptions)
# -fno-stack-protector : Pas de protection de pile (pas de libc)
CFLAGS = -std=gnu99 -ffreestanding -O0 -g -Wall -Wextra -mno-sse -mno-sse2 -mno-mmx -mno-red-zone -fno-stack-protector
ASFLAGS = -felf32 -g
LDFLAGS = -ffreestanding -O0 -nostdlib -lgcc

# ===========================================
# Structure du projet:
#   src/
#   ├── arch/x86/      - Code spécifique x86
#   ├── kernel/        - Cœur du kernel
#   ├── mm/            - Memory Management
#   ├── drivers/       - Pilotes matériels
#   │   └── net/       - Pilotes réseau
#   ├── net/           - Stack réseau (OSI)
#   │   ├── l2/        - Layer 2 (Data Link)
#   │   ├── l3/        - Layer 3 (Network)
#   │   └── core/      - Infrastructure
#   └── include/       - Headers partagés
# ===========================================

# Architecture (x86)
ARCH_SRC = src/arch/x86/boot.s src/arch/x86/gdt.c src/arch/x86/idt.c src/arch/x86/interrupts.s
ARCH_OBJ = src/arch/x86/boot.o src/arch/x86/gdt.o src/arch/x86/idt.o src/arch/x86/interrupts.o

# Kernel core
KERNEL_SRC = src/kernel/kernel.c src/kernel/console.c src/kernel/keyboard.c
KERNEL_OBJ = src/kernel/kernel.o src/kernel/console.o src/kernel/keyboard.o

# Memory management
MM_SRC = src/mm/pmm.c src/mm/kheap.c
MM_OBJ = src/mm/pmm.o src/mm/kheap.o

# Drivers
DRIVERS_SRC = src/drivers/pci.c src/drivers/ata.c src/drivers/net/pcnet.c
DRIVERS_OBJ = src/drivers/pci.o src/drivers/ata.o src/drivers/net/pcnet.o

# Network stack (par couche OSI)
NET_L2_SRC = src/net/l2/ethernet.c src/net/l2/arp.c
NET_L2_OBJ = src/net/l2/ethernet.o src/net/l2/arp.o

NET_L3_SRC = src/net/l3/ipv4.c src/net/l3/icmp.c src/net/l3/route.c
NET_L3_OBJ = src/net/l3/ipv4.o src/net/l3/icmp.o src/net/l3/route.o

NET_L4_SRC = src/net/l4/udp.c src/net/l4/dhcp.c
NET_L4_OBJ = src/net/l4/udp.o src/net/l4/dhcp.o

NET_CORE_SRC = src/net/core/net.c src/net/core/netdev.c
NET_CORE_OBJ = src/net/core/net.o src/net/core/netdev.o

# Filesystem (VFS + drivers)
FS_SRC = src/fs/vfs.c src/fs/ext2.c
FS_OBJ = src/fs/vfs.o src/fs/ext2.o

# Tous les objets
OBJ = $(ARCH_OBJ) $(KERNEL_OBJ) $(MM_OBJ) $(DRIVERS_OBJ) $(FS_OBJ) $(NET_L2_OBJ) $(NET_L3_OBJ) $(NET_L4_OBJ) $(NET_CORE_OBJ)

# Cible finale
alos.bin: $(OBJ)
	$(CC) -T src/include/linker.ld -o alos.bin $(LDFLAGS) $(OBJ)
	@echo "Build success! Run 'make run' to test."

# ===========================================
# Règles de compilation par module
# ===========================================

# Architecture x86
src/arch/x86/boot.o: src/arch/x86/boot.s
	$(AS) $(ASFLAGS) $< -o $@

src/arch/x86/interrupts.o: src/arch/x86/interrupts.s
	$(AS) $(ASFLAGS) $< -o $@

src/arch/x86/%.o: src/arch/x86/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Kernel
src/kernel/%.o: src/kernel/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Memory Management
src/mm/%.o: src/mm/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Drivers
src/drivers/%.o: src/drivers/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/drivers/net/%.o: src/drivers/net/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Network stack
src/net/l2/%.o: src/net/l2/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/net/l3/%.o: src/net/l3/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/net/l4/%.o: src/net/l4/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

src/net/core/%.o: src/net/core/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Filesystem
src/fs/%.o: src/fs/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Nettoyage
clean:
	rm -f src/arch/x86/*.o src/kernel/*.o src/mm/*.o
	rm -f src/drivers/*.o src/drivers/net/*.o
	rm -f src/net/l2/*.o src/net/l3/*.o src/net/l4/*.o src/net/core/*.o
	rm -f src/fs/*.o
	rm -f alos.bin

# Test rapide avec QEMU (avec carte réseau AMD PCnet connectée en mode user)
# SLIRP network: 10.0.2.0/24, gateway 10.0.2.2, DHCP range 10.0.2.15-10.0.2.31
run: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M \
		-netdev user,id=net0,net=10.0.2.0/24,dhcpstart=10.0.2.15 \
		-device pcnet,netdev=net0 \
		-drive file=disk.img,format=raw,index=0,media=disk

# Run avec debug CPU (affiche les exceptions et interrupts dans le terminal)
run-debug: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M \
		-netdev user,id=net0,net=10.0.2.0/24,dhcpstart=10.0.2.15 \
		-device pcnet,netdev=net0 \
		-d int,cpu_reset -no-reboot \
		-drive file=disk.img,format=raw,index=0,media=disk

# Run avec capture de paquets (pour Wireshark)
run-pcap: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M \
		-netdev user,id=net0,net=10.0.2.0/24,dhcpstart=10.0.2.15 \
		-device pcnet,netdev=net0 \
		-object filter-dump,id=dump0,netdev=net0,file=alos-network.pcap
	@echo "Capture sauvée dans alos-network.pcap"

# Run avec TAP (paquets visibles sur l'hôte via Wireshark sur tap0)
# Prérequis: sudo ip tuntap add dev tap0 mode tap user $USER && sudo ip link set tap0 up
run-tap: alos.bin
	qemu-system-i386 -kernel alos.bin -m 1024M -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device pcnet,netdev=net0

# Run avec vmnet-shared (macOS uniquement - même réseau que l'hôte)
# Nécessite: brew install qemu (version avec vmnet support)
# Note: Peut nécessiter sudo ou des permissions spéciales
run-vmnet: alos.bin
	sudo qemu-system-i386 -kernel alos.bin -m 128M \
		-netdev vmnet-shared,id=net0 \
		-device pcnet,netdev=net0 \
		-drive file=disk.img,format=raw,index=0,media=disk

# Run avec socket multicast (pour debug local sans réseau externe)
run-socket: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M \
		-netdev socket,id=net0,mcast=230.0.0.1:1234 \
		-device pcnet,netdev=net0 \
		-drive file=disk.img,format=raw,index=0,media=disk

# Debug avec QEMU (attend GDB sur port 1234)
debug: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M -netdev user,id=net0 -device pcnet,netdev=net0 -s -S &
	@echo "QEMU lancé. Connectez GDB avec: target remote localhost:1234"