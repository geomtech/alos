# ALOS - Alexy Operating System

ALOS is a minimalist x86-64 operating system kernel written in C and x86-64 Assembly, designed for learning and active experimentation. It boots through Limine and runs on QEMU or VirtualBox. It implements core OS concepts including memory management, privilege separation, multitasking, storage, filesystem support, a TCP/IP networking stack, and a graphical user interface.

> **Note:** Code comments in this project are written in **French**. The codebase serves as both a learning resource and a functional kernel.

## Current development focus

The current priority is to keep the x86-64 scheduler, Ring 0/Ring 3 transitions, TSS handling, and `iretq` context restoration regression-free before expanding process semantics.

Next major milestones:
1. Automated QEMU smoke/regression tests
2. `fork()` / `exec()` / `wait()`
3. Pipes and POSIX-like signals
4. Demand paging, Copy-on-Write, and `mmap()`
5. AHCI/SATA, then NVMe

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
- [x] x86-64 Context Switching (kernel and user threads)
- [x] User Space (Ring 3) with TSS
- [x] Ring 0/Ring 3 `iretq` context restoration
- [x] System Calls (POSIX/BSD-like interface)
- [x] Priority-based scheduler
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
- [x] Ethernet driver Intel I219-V
- [ ] WiFi support (with WPA2/WPA3)
- [ ] Graphics card drivers (Intel/AMD/NVIDIA)
- [ ] Serial port (COM1-4) advanced support
- [ ] Parallel port support

### User Interface
- [x] Interactive Shell with history
- [x] Persistent history (`/config/history`)
- [x] GUI + Mouse
- [x] Window manager/compositor
- [ ] OpenGL support
- [x] Framebuffer console (VESA/GOP)
- [ ] UTF-8 string and console
- [x] Font rendering (TrueType/FreeType)
- [x] Damage-based compositor rendering and GUI event-loop optimizations
- [x] Basic GUI component/widget framework
- [ ] Desktop environment
- [ ] Widget toolkit
- [ ] Multi-monitor support

### User Space & Applications
- [x] ELF Loader (32-bit executables)
- [x] ELF 64-bit support
- [ ] Dynamic linking (shared libraries .so)
- [x] Standard C library (libc)
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
- [x] QEMU remote GDB workflow (`make debug`)
- [x] Serial logging and live log viewer (`serial.log`, `logs.ps1`, `run-debug.ps1`)
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
├── arch/x86_64/       # x86-64 code (GDT, IDT, TSS, interrupts, context switching, usermode)
├── config/            # Kernel configuration
├── kernel/            # Kernel core (main, console, keyboard, syscalls, elf)
├── mm/                # Memory Management (PMM, heap, VMM)
├── drivers/           # Hardware drivers
│   ├── ata.c/h        # ATA/IDE disk driver
│   ├── pci.c/h        # PCI bus driver
│   ├── virtio/        # VirtIO transport/device support
│   └── net/           # PCnet, VirtIO-net and Intel E1000E drivers
├── fs/                # Filesystems
│   ├── vfs.c/h        # Virtual File System layer
│   └── ext2.c/h       # Ext2 filesystem driver
├── net/               # Network stack
│   ├── core/          # Network infrastructure
│   ├── l2/            # Layer 2 (Ethernet, ARP)
│   ├── l3/            # Layer 3 (IPv4, ICMP, Routing)
│   └── l4/            # Layer 4 (UDP, TCP, DHCP, DNS)
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
│             Hardware / VM (x86-64)                     │
└─────────────────────────────────────────────────────────┘
```

## Building

### Prerequisites

- Compiler: `x86_64-elf-gcc` / `x86_64-elf-ld` when available (the Makefile can fall back to the native GCC toolchain)
- Assembler: `nasm`
- ISO tooling: `xorriso`
- Disk utilities: `e2fsprogs` / `mkfs.ext2`
- Emulator: `qemu-system-x86_64` and/or VirtualBox
- Bootloader: Limine v10.x (downloaded/built automatically by the Makefile)

### Compilation

```bash
# Build the kernel
make

# Clean build artifacts
make clean

# Full rebuild
make clean && make
```

### Creating the Ext2 Disk Image

The disk image is generated from `disk_structure/` plus the built userland binaries:

```bash
make disk.img
```

The current Makefile creates a 64 MiB Ext2 image and stages user applications under `/bin`.

### Running

```bash
make                     # Build the x86-64 kernel (alos.elf)
make iso                 # Build the bootable Limine ISO
make run                 # Run in VirtualBox (default target)
make run-qemu            # Run in QEMU with UEFI
make run-qemu-fast       # QEMU + KVM + accelerated VirtIO VGA/OpenGL
make run-qemu-fast-no-kvm # Accelerated display without KVM
make debug               # Start QEMU paused with a GDB server on :1234
make clean               # Remove build artifacts
make distclean           # Also remove Limine
```

On Windows, `run-debug.ps1` starts the runtime and follows `serial.log` using `logs.ps1`.

## QEMU Configuration

### Storage
- Ext2 disk image: `disk.img`
- Current storage driver: ATA/IDE PIO
- Planned next-generation storage: AHCI/SATA DMA, then NVMe

### Networking
- QEMU targets use VirtIO-net with user-mode networking
- The default QEMU configuration forwards host TCP port `8080` to guest port `80`
- PCnet and Intel E1000E drivers are also present in the tree

### Debugging
- Kernel serial output is available through `serial.log` or QEMU stdio depending on the target
- `make debug` exposes the QEMU GDB server on TCP port `1234`
- `run-debug.ps1` + `logs.ps1` provide a convenient live-debug workflow on Windows

## License

This project is intended for educational purposes.

## Contributing

Contributions are welcome!
