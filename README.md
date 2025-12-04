# ALOS - Alexy Operating System

ALOS is a minimalist operating system kernel written in C and x86 Assembly, designed for learning purposes and running on QEMU. It implements core OS concepts including memory management, interrupt handling, storage, filesystem support, and a TCP/IP networking stack.

> **Note:** Code comments in this project are written in **French**. The codebase serves as both a learning resource and a functional kernel.

## Features implemented and future plans

### Core System ✓
- [x] GDT/IDT setup
- [x] Physical Memory Manager
- [x] Kernel Heap
- [x] Virtual Memory (Paging) - Identity mapping
- [x] RTC Real-Time Clock
- [x] PIT Programmable Interval Timer
- [x] Kernel Logging System (file-based, /system/logs)
- [x] System logs
- [ ] ACPI Support (power management, shutdown/reboot)
- [ ] SMP/Multi-core support
- [ ] APIC/x2APIC (Advanced interrupt handling)
- [ ] Kernel modules loading (dynamic drivers)

### Memory Management
- [x] Virtual Memory (Paging) - Identity mapping
- [ ] Demand paging / Page fault handler
- [ ] Copy-on-Write (COW)
- [ ] Memory-mapped files (mmap)
- [ ] Shared memory (SHM)
- [ ] Swap support

### Process & Scheduling
- [x] Multitasking (Round Robin scheduler)
- [x] Context Switching (kernel threads)
- [x] User Space (Ring 3) with TSS
- [x] System Calls (POSIX/BSD-like interface)
- [ ] Priority-based scheduler
- [ ] Process groups and sessions
- [ ] Fork/exec implementation
- [ ] Signals (POSIX-like)
- [ ] Real-time scheduling (SCHED_FIFO, SCHED_RR)

### IPC (Inter-Process Communication)
- [ ] Pipes (anonymous and named/FIFO)
- [ ] Message queues
- [ ] Semaphores
- [ ] Shared memory segments
- [ ] Unix domain sockets

### Storage & File Systems
- [x] PCI Enumeration
- [x] ATA/IDE Driver (PIO)
- [x] VFS Layer
- [x] Ext2 Read Support
- [x] Ext2 Write Support
- [x] File/Directory Creation (vfs_create, vfs_mkdir)
- [ ] AHCI/SATA driver (DMA support)
- [ ] NVMe driver
- [ ] FAT32 support
- [ ] ISO9660 (CD-ROM filesystem)
- [ ] File locking mechanisms
- [ ] Inotify/fsnotify (file change notifications)
- [ ] RAID support (software)

### Network Stack
- [x] Ethernet/ARP
- [x] IPv4/ICMP
- [x] UDP/DHCP
- [x] DNS Resolver (A, PTR, CNAME + cache)
- [x] Ping with DNS support
- [x] TCP Implementation (full state machine)
- [x] Simple HTTP server based on storage
- [ ] IPv6 support
- [ ] Network bridging/routing
- [ ] Firewall/packet filtering (iptables-like)
- [ ] Raw sockets
- [ ] Unix domain sockets
- [ ] TLS/SSL
- [ ] SSH server/client
- [ ] FTP/SFTP
- [ ] NFS client

### Device Drivers
- [x] VGA Console
- [x] PS/2 Keyboard
- [x] Azerty keyboard (with keymap abstraction)
- [ ] USB stack (XHCI/EHCI/UHCI)
- [ ] USB HID (keyboard/mouse)
- [ ] USB Mass Storage
- [ ] Audio driver (AC97/Intel HDA)
- [ ] Ethernet driver Intel I219-V
- [ ] WiFi support (with WPA2/WPA3)
- [ ] Graphics card drivers (Intel/AMD/NVIDIA)
- [ ] Serial port (COM1-4) advanced support
- [ ] Parallel port support

### User Interface
- [x] Interactive Shell with history
- [x] Persistent history (`/config/history`)
- [ ] GUI + Mouse
- [ ] Window manager/compositor
- [ ] OpenGL support
- [ ] Framebuffer console (VESA/GOP)
- [ ] UTF-8 string and console
- [ ] Font rendering (TrueType/FreeType)
- [ ] Desktop environment
- [ ] Widget toolkit
- [ ] Multi-monitor support

### User Space & Applications
- [x] ELF Loader (32-bit executables)
- [x] **Linux Binary Compatibility** (run statically-linked Linux i386 binaries)
- [ ] ELF 64-bit support
- [ ] Dynamic linking (shared libraries .so)
- [ ] Standard C library (libc)
- [ ] POSIX thread library (pthread)
- [ ] Math library (libm)
- [ ] Compression libraries (zlib, gzip)
- [ ] Core utilities (ls, cp, mv, rm, cat, grep, etc.)
- [ ] Text editor (vi/nano-like)
- [ ] Package manager
- [ ] GCC/Compiler toolchain port

### Configuration & Scripts
- [x] Scripting files (`/config/startup.sh`)
- [x] Network configuration files (`/config/network.conf`)
- [x] Persistent history (`/config/history`)
- [ ] Init system (systemd/OpenRC-like)
- [ ] Service management
- [ ] Environment variables
- [ ] User authentication (/etc/passwd, /etc/shadow)
- [ ] Permissions and ACL
- [ ] Cron/scheduled tasks

### Security
- [ ] User/group management
- [ ] File permissions (chmod/chown)
- [ ] Access Control Lists (ACL)
- [ ] Sandboxing/containers
- [ ] Secure boot support
- [ ] ASLR (Address Space Layout Randomization)
- [ ] DEP/NX bit enforcement
- [ ] Encrypted filesystems
- [ ] SELinux/AppArmor-like MAC

### Development & Debugging
- [ ] GDB stub (remote debugging)
- [ ] Kernel debugger (kdb)
- [ ] System call tracing (strace-like)
- [ ] Performance profiling tools
- [ ] Memory leak detection
- [ ] Code coverage tools

### Advanced Features
- [ ] Virtualization support (KVM guest)
- [ ] Containers/namespaces
- [ ] Control groups (cgroups)
- [ ] Hibernation support
- [ ] Laptop features (battery, backlight)
- [ ] Bluetooth stack
- [ ] TPM support
- [ ] Hot-plug devices support

### Documentation & Testing
- [ ] API documentation
- [ ] User manual
- [ ] Automated testing suite
- [ ] Continuous integration
- [ ] Benchmarking suite

## Project Structure 

```
src/
├── arch/x86/          # x86-specific code (boot, GDT, IDT, TSS, usermode)
├── config/            # Kernel configuration
├── kernel/            # Kernel core (main, console, keyboard, syscalls, elf)
├── mm/                # Memory Management (PMM, heap, VMM)
├── drivers/           # Hardware drivers
│   ├── ata.c/h        # ATA/IDE disk driver
│   ├── pci.c/h        # PCI bus driver
│   └── net/           # Network drivers (PCnet)
├── fs/                # Filesystems
│   ├── vfs.c/h        # Virtual File System layer
│   └── ext2.c/h       # Ext2 filesystem driver
├── net/               # Network stack
│   ├── core/          # Network infrastructure
│   ├── l2/            # Layer 2 (Ethernet, ARP)
│   ├── l3/            # Layer 3 (IPv4, ICMP, Routing)
│   └── l4/            # Layer 4 (UDP, TCP, DHCP, DNS)
├── lib/               # Common utilities (string, libc for userland)
├── shell/             # Command interpreter
│   ├── shell.c/h      # Shell core (readline, history, parsing)
│   └── commands.c/h   # Built-in commands (help, ping, exec, etc.)
├── userland/          # User space programs (server, hello, test)
└── include/           # Shared headers (Multiboot, linker script, ELF)
```

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│              User Space (Ring 3)                        │
│         ELF Programs  │  Userland libc                  │
├─────────────────────────────────────────────────────────┤
│                  System Calls (int 0x80)                │
├─────────────────────────────────────────────────────────┤
│              Kernel Space (Ring 0)                      │
├─────────────────────────────────────────────────────────┤
│     VFS API              │         Network API          │
│  (open, read, readdir)   │    (send, recv, socket)      │
├──────────────────────────┼──────────────────────────────┤
│   Ext2   │  (Future FS)  │  TCP/UDP  │ ICMP │ DHCP/DNS  │
├──────────────────────────┼──────────────────────────────┤
│      ATA Driver          │     IPv4  │  ARP  │ Ethernet │
├──────────────────────────┼──────────────────────────────┤
│      IDE Controller      │       PCnet Driver           │
├──────────────────────────┴──────────────────────────────┤
│                    PCI Bus                              │
├─────────────────────────────────────────────────────────┤
│              Hardware (x86)                             │
└─────────────────────────────────────────────────────────┘
```

## Building

### Prerequisites

- Cross-compiler: `i686-elf-gcc` (in `~/opt/cross/bin/`)
- Assembler: `nasm`
- Emulator: `qemu-system-i386`
- Disk utilities: `e2fsprogs` (for creating Ext2 disk images)

### Compilation

```bash
# Build the kernel
make

# Clean build artifacts
make clean

# Full rebuild
make clean && make
```

### Creating a Disk Image

```bash
# Create a 32MB Ext2 disk image
dd if=/dev/zero of=disk.img bs=1M count=32
mkfs.ext2 disk.img

# Add files to the disk (using debugfs on macOS)
echo "Hello from ALOS!" > /tmp/hello.txt
debugfs -w disk.img -R "write /tmp/hello.txt hello.txt"

# Verify contents
debugfs disk.img -R "ls"
```

### Running

```bash
make          # Compile le kernel
make iso      # Crée l'ISO bootable (télécharge Limine si nécessaire)
make run      # Lance QEMU avec l'ISO
make clean    # Nettoie les fichiers compilés
make distclean # Nettoie tout, y compris Limine
```

## QEMU Configuration

### Storage
- Primary Master IDE disk: `disk.img` (Ext2 formatted)

## License

This project is intended for educational purposes.

## Contributing

Contributions are welcome!
