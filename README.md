# ALOS - A Learning Operating System

ALOS is a minimalist operating system kernel written in C and x86 Assembly, designed for learning purposes and running on QEMU. It implements core OS concepts including memory management, interrupt handling, and a TCP/IP networking stack.

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

### Drivers

- **VGA Console**
  - 80x25 text mode with colors
  - Virtual buffer with scrolling (100 lines history)
  - Hexadecimal and decimal output

- **Keyboard**
  - PS/2 keyboard driver
  - Interrupt-driven input

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
  - Echo Request/Reply (ping)
  - Destination Unreachable messages
- **Routing**: Basic routing table with gateway support

#### Layer 4 - Transport
- **UDP**: User Datagram Protocol
  - Connectionless packet transmission
  - Port multiplexing
- **DHCP**: Dynamic Host Configuration Protocol client
  - Full DHCP state machine (DISCOVER, OFFER, REQUEST, ACK)
  - Automatic IP configuration
  - Lease management

## Project Structure

```
src/
├── arch/x86/          # x86-specific code (boot, GDT, IDT, I/O)
├── kernel/            # Kernel core (main, console, keyboard)
├── mm/                # Memory Management (PMM, heap)
├── drivers/           # Hardware drivers
│   └── net/           # Network drivers (PCnet)
├── net/               # Network stack
│   ├── core/          # Network infrastructure
│   ├── l2/            # Layer 2 (Ethernet, ARP)
│   ├── l3/            # Layer 3 (IPv4, ICMP, Routing)
│   └── l4/            # Layer 4 (UDP, DHCP)
└── include/           # Shared headers (Multiboot, linker script)
```

## Building

### Prerequisites

- Cross-compiler: `i686-elf-gcc` (in `~/opt/cross/bin/`)
- Assembler: `nasm`
- Emulator: `qemu-system-i386`

### Compilation

```bash
# Build the kernel
make

# Clean build artifacts
make clean

# Full rebuild
make clean && make
```

### Running

```bash
# Run in QEMU
make run

# Debug mode (QEMU waits for GDB on port 1234)
qemu-system-i386 -kernel alos.bin -s -S
```

## QEMU Networking

When running in QEMU with user networking (SLIRP mode):
- Gateway: `10.0.2.2`
- DNS Server: `10.0.2.3`
- Default IP: `10.0.2.15`
- Network: `10.0.2.0/24`

## License

This project is intended for educational purposes.

## Contributing

Contributions are welcome!
