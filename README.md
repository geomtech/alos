# ALOS - Alexy Operating System

ALOS is a minimalist operating system kernel written in C and x86 Assembly, designed for learning purposes and running on QEMU. It implements core OS concepts including memory management, interrupt handling, storage, filesystem support, and a TCP/IP networking stack.

> **Note:** Code comments in this project are written in **French**. The codebase serves as both a learning resource and a functional kernel.

## Features

### Architecture (x86)

- **GDT (Global Descriptor Table)**: Segment descriptor management for kernel mode
- **IDT (Interrupt Descriptor Table)**: Interrupt and exception handling
- **Multiboot compliant**: Boots with GRUB or any Multiboot-compliant bootloader

### Memory Management

- **Physical Memory Manager (PMM)**
  - Bitmap-based physical page allocator
  - 4 KiB block/page size
  - Parses Multiboot memory map
  - Contiguous block allocation support

- **Kernel Heap (kheap)**
  - Dynamic memory allocation (`kmalloc`/`kfree`)
  - First-fit allocation algorithm
  - Block coalescing on free

- **Virtual Memory Manager (VMM)**
  - x86 Paging with 4 KiB pages
  - Identity mapping of first 16 MB
  - Page Directory and Page Tables management
  - Page Fault handler with CR2 address display
  - Foundation for User Space (Ring 3) support

### Storage & Filesystems

- **ATA/IDE Driver (PIO Mode)**
  - Primary Master disk detection
  - LBA28 addressing mode
  - Sector read/write operations
  - IRQ14 interrupt handling

- **Virtual File System (VFS)**
  - Abstraction layer for filesystem drivers
  - Unified API: `vfs_open`, `vfs_read`, `vfs_close`, `vfs_readdir`
  - Mount point management
  - Multiple filesystem support

- **Ext2 Filesystem Driver**
  - Superblock and block group descriptor parsing
  - Inode reading and management
  - Directory entry traversal
  - File content reading
  - Support for regular files and directories
  - **File creation** (`vfs_create`)
  - **Directory creation** (`vfs_mkdir`)

### Shell

- **Interactive Command Interpreter**
  - Line editing with backspace support
  - Command history (Up/Down arrows, 16 entries)
  - Current working directory (CWD) state
  - Command parsing with argument support
  - Built-in commands: `help`, `ping`, `ps`, `tasks`, `usermode`, `exec`, `elfinfo`, `netinfo`
  - Extensible command system

### Multitasking

- **Round Robin Scheduler**
  - Preemptive multitasking via timer interrupt
  - Context switching (callee-saved registers)
  - Kernel threads with dedicated 4KB stacks
  - Circular linked list of processes
  - Process states: READY, RUNNING, BLOCKED, TERMINATED
  - `ps` command to list processes
  - `tasks` command for testing (launches A/B threads)

### User Space (Ring 3)

- **Task State Segment (TSS)**
  - Hardware-assisted privilege level switching
  - Kernel stack (ESP0/SS0) for interrupts from Ring 3
  - TSS entry in GDT (selector 0x28)

- **User Mode Support**
  - Ring 3 code execution with restricted privileges
  - User stack allocation and management
  - Page table entries with User bit for accessible memory
  - `usermode` shell command for testing

### System Calls

- **POSIX/BSD-like Interface**
  - `int 0x80` software interrupt mechanism
  - File operations: `open`, `read`, `write`, `close`
  - Process control: `exit`, `getpid`
  - Socket operations: `socket`, `bind`, `listen`, `accept`, `send`, `recv`
  - File descriptor table with stdin/stdout/stderr (fd 0, 1, 2)

### ELF Loader

- **32-bit ELF Executable Support**
  - Magic number and header validation
  - i386 architecture verification
  - Program header parsing (PT_LOAD segments)
  - Memory allocation and segment loading
  - Entry point extraction
  - `exec` and `elfinfo` shell commands

### Kernel Logging System

- **File-based logging** (`/system/logs/kernel.log`)
  - Early buffer (8KB) for logs before VFS is ready
  - Automatic directory creation (`/system/logs`)
  - Timestamps with millisecond precision
  - Log levels: DEBUG, INFO, WARN, ERROR
  - Configurable minimum log level

### Drivers

- **VGA Console**
  - 80x25 text mode with colors
  - Virtual buffer with scrolling (100 lines history)
  - Hexadecimal and decimal output

- **Keyboard**
  - PS/2 keyboard driver
  - Interrupt-driven input
  - Circular buffer for key events
  - Blocking `keyboard_getchar()` function

- **PCI Bus**
  - PCI device enumeration
  - BAR (Base Address Register) handling
  - Support for common vendor IDs (AMD, Intel, NVIDIA, Realtek, QEMU)

- **AMD PCnet-PCI II (Am79C970A)**
  - Network card driver for QEMU's default NIC
  - Transmit and receive ring buffers
  - Interrupt-driven packet handling

### Networking Stack (OSI Model)

The networking stack follows the OSI model architecture:

#### Layer 2 - Data Link
- **Ethernet**: Frame handling, EtherType parsing (IPv4, ARP)
- **ARP**: Address Resolution Protocol
  - ARP cache with timeout
  - Request/Reply handling
  - Gratuitous ARP support

#### Layer 3 - Network
- **IPv4**: Internet Protocol v4
  - Packet parsing and validation
  - Checksum calculation
  - TTL handling
- **ICMP**: Internet Control Message Protocol
  - Echo Request/Reply (ping responder)
  - **Ping command** with DNS resolution: `ping("google.com")`
  - Destination Unreachable messages
- **Routing**: Basic routing table with gateway support

#### Layer 4 - Transport
- **UDP**: User Datagram Protocol
  - Connectionless packet transmission
  - Port multiplexing
- **TCP**: Transmission Control Protocol
  - Full RFC 793 state machine (11 states)
  - 3-way handshake (SYN, SYN-ACK, ACK)
  - Connection termination (FIN, FIN-ACK)
  - Sequence and acknowledgment number tracking
  - Receive buffer with circular queue (4KB per socket)
  - Checksum calculation with pseudo-header
  - Server socket support (bind, listen, accept)
  - Up to 16 concurrent sockets
- **DHCP**: Dynamic Host Configuration Protocol client
  - Full DHCP state machine (DISCOVER, OFFER, REQUEST, ACK)
  - Automatic IP configuration
  - Lease management
- **DNS**: Domain Name System resolver
  - A record resolution (hostname → IP)
  - PTR record resolution (reverse DNS: IP → hostname)
  - CNAME support (alias resolution)
  - DNS cache with TTL management
  - Integrates with DHCP-provided DNS server

#### Layer 3 - Network
- **IPv4**: Internet Protocol v4
  - Packet parsing and validation
  - Checksum calculation
  - TTL handling
- **ICMP**: Internet Control Message Protocol
  - Echo Request/Reply (ping responder)
  - **Ping command** with DNS resolution (`ping("google.com")`)
  - Destination Unreachable messages
- **Routing**: Basic routing table with gateway support

```
src/
├── arch/x86/          # x86-specific code (boot, GDT, IDT, TSS, usermode)
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
├── lib/               # Common utilities (string functions)
├── shell/             # Command interpreter
│   ├── shell.c/h      # Shell core (readline, history, parsing)
│   └── commands.c/h   # Built-in commands (help, ping, exec, etc.)
├── userland/          # User space programs (server, hello, test)
└── include/           # Shared headers (Multiboot, linker script, ELF)

userland/
└── libc.h             # Minimal C library for user space programs
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
# Run in QEMU with disk
make run

# Run with debug output (shows interrupts and exceptions)
make run-debug

# Run with network packet capture
make run-pcap
```

## QEMU Configuration

### Storage
- Primary Master IDE disk: `disk.img` (Ext2 formatted)

### Networking (SLIRP mode)
- Gateway: `10.0.2.2`
- DNS Server: `10.0.2.3`
- Default IP: `10.0.2.15`
- Network: `10.0.2.0/24`

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

## Roadmap

- [x] GDT/IDT setup
- [x] Physical Memory Manager
- [x] Kernel Heap
- [x] VGA Console
- [x] PS/2 Keyboard
- [x] PCI Enumeration
- [x] ATA/IDE Driver (PIO)
- [x] VFS Layer
- [x] Ext2 Read Support
- [x] Ethernet/ARP
- [x] IPv4/ICMP
- [x] UDP/DHCP
- [x] DNS Resolver (A, PTR, CNAME + cache)
- [x] Ping with DNS support
- [x] Ext2 Write Support
- [x] File/Directory Creation (vfs_create, vfs_mkdir)
- [x] Kernel Logging System (file-based, /system/logs)
- [x] Interactive Shell with history
- [x] Virtual Memory (Paging) - Identity mapping
- [x] Multitasking (Round Robin scheduler)
- [x] Context Switching (kernel threads)
- [x] User Space (Ring 3) with TSS
- [x] ELF Loader (32-bit executables)
- [x] TCP Implementation (full state machine)
- [x] System Calls (POSIX/BSD-like interface)
- [ ] Scripting files
- [ ] Network configuration files
- [ ] GUI
- [ ] OpenGL
- [ ] SSL
- [ ] SSH
- [ ] Telnet
- [ ] RTC Real-Time Clock
- [ ] PIT Programmable Interval Timer
- [ ] UTF-8 string and console

## License

This project is intended for educational purposes.

## Contributing

Contributions are welcome!
