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
ARCH_SRC = src/arch/x86/boot.s src/arch/x86/gdt.c src/arch/x86/idt.c src/arch/x86/interrupts.s src/arch/x86/switch.s src/arch/x86/tss.c src/arch/x86/usermode.c
ARCH_OBJ = src/arch/x86/boot.o src/arch/x86/gdt.o src/arch/x86/idt.o src/arch/x86/interrupts.o src/arch/x86/switch.o src/arch/x86/tss.o src/arch/x86/usermode.o

# Kernel core
KERNEL_SRC = src/kernel/kernel.c src/kernel/console.c src/kernel/keyboard.c src/kernel/keymap.c src/kernel/timer.c src/kernel/klog.c src/kernel/process.c src/kernel/thread.c src/kernel/sync.c src/kernel/workqueue.c src/kernel/syscall.c src/kernel/elf.c src/kernel/linux_compat.c
KERNEL_OBJ = src/kernel/kernel.o src/kernel/console.o src/kernel/keyboard.o src/kernel/keymap.o src/kernel/timer.o src/kernel/klog.o src/kernel/process.o src/kernel/thread.o src/kernel/sync.o src/kernel/workqueue.o src/kernel/syscall.o src/kernel/elf.o src/kernel/linux_compat.o

# MMIO subsystem
MMIO_SRC = src/kernel/mmio/mmio.c src/kernel/mmio/pci_mmio.c
MMIO_OBJ = src/kernel/mmio/mmio.o src/kernel/mmio/pci_mmio.o

# Memory management
MM_SRC = src/mm/pmm.c src/mm/kheap.c src/mm/vmm.c
MM_OBJ = src/mm/pmm.o src/mm/kheap.o src/mm/vmm.o

# Drivers
DRIVERS_SRC = src/drivers/pci.c src/drivers/ata.c src/drivers/net/pcnet.c src/drivers/net/virtio_net.c src/drivers/net/e1000e.c src/drivers/virtio/virtio_mmio.c src/drivers/virtio/virtio_transport.c src/drivers/virtio/virtio_pci_modern.c
DRIVERS_OBJ = src/drivers/pci.o src/drivers/ata.o src/drivers/net/pcnet.o src/drivers/net/virtio_net.o src/drivers/net/e1000e.o src/drivers/virtio/virtio_mmio.o src/drivers/virtio/virtio_transport.o src/drivers/virtio/virtio_pci_modern.o

# Network stack (par couche OSI)
NET_L2_SRC = src/net/l2/ethernet.c src/net/l2/arp.c
NET_L2_OBJ = src/net/l2/ethernet.o src/net/l2/arp.o

NET_L3_SRC = src/net/l3/ipv4.c src/net/l3/icmp.c src/net/l3/route.c
NET_L3_OBJ = src/net/l3/ipv4.o src/net/l3/icmp.o src/net/l3/route.o

NET_L4_SRC = src/net/l4/udp.c src/net/l4/dhcp.c src/net/l4/dns.c src/net/l4/tcp.c src/net/l4/http.c
NET_L4_OBJ = src/net/l4/udp.o src/net/l4/dhcp.o src/net/l4/dns.o src/net/l4/tcp.o src/net/l4/http.o

NET_CORE_SRC = src/net/core/net.c src/net/core/netdev.c
NET_CORE_OBJ = src/net/core/net.o src/net/core/netdev.o

# Filesystem (VFS + drivers)
FS_SRC = src/fs/vfs.c src/fs/ext2.c
FS_OBJ = src/fs/vfs.o src/fs/ext2.o

# Library (common utilities)
LIB_SRC = src/lib/string.c
LIB_OBJ = src/lib/string.o

# Shell
SHELL_SRC = src/shell/shell.c src/shell/commands.c
SHELL_OBJ = src/shell/shell.o src/shell/commands.o

# Configuration
CONFIG_SRC = src/config/config.c
CONFIG_OBJ = src/config/config.o

# Tous les objets
OBJ = $(ARCH_OBJ) $(KERNEL_OBJ) $(MMIO_OBJ) $(MM_OBJ) $(DRIVERS_OBJ) $(FS_OBJ) $(NET_L2_OBJ) $(NET_L3_OBJ) $(NET_L4_OBJ) $(NET_CORE_OBJ) $(LIB_OBJ) $(SHELL_OBJ) $(CONFIG_OBJ)

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

src/arch/x86/switch.o: src/arch/x86/switch.s
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

# Library
src/lib/%.o: src/lib/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Shell
src/shell/%.o: src/shell/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Configuration
src/config/%.o: src/config/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# MMIO subsystem
src/kernel/mmio/%.o: src/kernel/mmio/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# VirtIO drivers
src/drivers/virtio/%.o: src/drivers/virtio/%.c
	$(CC) -c $< -o $@ $(CFLAGS)

# Nettoyage
clean:
	rm -f src/arch/x86/*.o src/kernel/*.o src/kernel/mmio/*.o src/mm/*.o
	rm -f src/drivers/*.o src/drivers/net/*.o src/drivers/virtio/*.o
	rm -f src/net/l2/*.o src/net/l3/*.o src/net/l4/*.o src/net/core/*.o
	rm -f src/fs/*.o src/lib/*.o src/shell/*.o src/config/*.o
	rm -f alos.bin

# Test rapide avec QEMU (avec carte réseau AMD PCnet connectée en mode user)
# SLIRP network: 10.0.2.0/24, gateway 10.0.2.2, DHCP range 10.0.2.15-10.0.2.31
run: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M \
		-netdev user,id=net0,net=10.0.2.0/24,dhcpstart=10.0.2.15,hostfwd=tcp::8080-:8080 \
		-device virtio-net-pci,netdev=net0 \
		-drive file=disk.img,format=raw,index=0,media=disk \
		-serial stdio

run-e1000: alos.bin
	qemu-system-i386 -kernel alos.bin -m 128M \
		-netdev user,id=net0,net=10.0.2.0/24,dhcpstart=10.0.2.15,hostfwd=tcp::8080-:8080 \
		-device e1000,netdev=net0 \
		-drive file=disk.img,format=raw,index=0,media=disk \
		-serial stdio

# Run avec capture de paquets (pour Wireshark)
run-pcap: alos.bin
	qemu-system-i386 -kernel alos.bin -m 1024M \
		-netdev user,id=net0,net=10.0.2.0/24,dhcpstart=10.0.2.15 \
		-device virtio-net-pci,netdev=net0 \
		-object filter-dump,id=dump0,netdev=net0,file=alos-network.pcap
	@echo "Capture sauvée dans alos-network.pcap"

# Run avec TAP (paquets visibles sur l'hôte via Wireshark sur tap0)
# Prérequis: sudo ip tuntap add dev tap0 mode tap user $USER && sudo ip link set tap0 up
run-tap: alos.bin
	qemu-system-i386 -kernel alos.bin -m 1024M \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		-device virtio-net-pci,netdev=net0 \
		-drive file=disk.img,format=raw,index=0,media=disk

# Run avec vmnet-shared (macOS uniquement - même réseau que l'hôte)
# Nécessite: brew install qemu (version avec vmnet support)
# Note: Peut nécessiter sudo ou des permissions spéciales
run-vmnet: alos.bin
	sudo qemu-system-i386 -kernel alos.bin -m 2048M \
		-netdev vmnet-shared,id=net0 \
		-device virtio-net-pci,netdev=net0 \
		-d int,cpu_reset -no-reboot \
		-drive file=disk.img,format=raw,index=0,media=disk

# Run avec socket multicast (pour debug local sans réseau externe)
run-socket: alos.bin
	qemu-system-i386 -kernel alos.bin -m 1024M \
		-netdev socket,id=net0,mcast=230.0.0.1:1234 \
		-device virtio-net-pci,netdev=net0 \
		-drive file=disk.img,format=raw,index=0,media=disk

# Debug avec QEMU (attend GDB sur port 1234)
debug: alos.bin
	qemu-system-i386 -kernel alos.bin -m 1024M -netdev user,id=net0 -device virtio-net-pci,netdev=net0 -s -S &
	@echo "QEMU lancé. Connectez GDB avec: target remote localhost:1234"

install-userland: src/userland/server.elf
	@echo "Installing userland programs to disk.img..."
	/sbin/debugfs -w -R "mkdir /bin" disk.img 2>/dev/null || true
	/sbin/debugfs -w -R "rm /bin/server" disk.img 2>/dev/null || true
	/sbin/debugfs -w -R "write src/userland/server.elf /bin/server" disk.img
	@echo "Done."
