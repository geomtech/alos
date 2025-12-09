# CLAUDE.md - AI Assistant Guide for ALOS

This document provides comprehensive guidance for AI assistants working with the ALOS (Alexy Operating System) codebase.

## Project Overview

**ALOS** is a minimalist x86-64 operating system kernel written in C and Assembly, designed for educational purposes and running on QEMU/VirtualBox. It implements core OS concepts including memory management, multitasking, filesystem support, and a full TCP/IP networking stack.

### Key Characteristics
- **Language**: C (GNU99 standard) + x86-64 Assembly (NASM)
- **Architecture**: x86-64 with kernel memory model
- **Bootloader**: Limine (v10.x)
- **Comments**: Written in French (code documentation)
- **Lines of Code**: ~19,000+ lines (kernel, MM, FS, networking)
- **Target Platform**: QEMU, VirtualBox (UEFI/BIOS compatible)

### Project Maturity
This is a learning/educational OS with working implementations of:
- ✅ Core kernel (GDT, IDT, interrupts, TSS)
- ✅ Memory management (PMM, VMM, heap)
- ✅ Multitasking with priority scheduler
- ✅ User space (Ring 3) with system calls
- ✅ VFS with Ext2 read/write support
- ✅ Full TCP/IP stack (Ethernet → TCP/HTTP)
- ✅ Linux binary compatibility (i386 static binaries)
- ✅ GUI with mouse support
- ✅ Interactive shell with persistent history

## Architecture Overview

### System Layers (Bottom-Up)

```
┌────────────────────────────────────────────────────────┐
│                  User Space (Ring 3)                   │
│          ELF Programs  │  Userland libc  │  Shell      │
├────────────────────────────────────────────────────────┤
│            System Calls (int 0x80) + Linux Compat      │
├────────────────────────────────────────────────────────┤
│                 Kernel Space (Ring 0)                  │
├────────────────────────────────────────────────────────┤
│  VFS Layer          │  Network Stack   │   GUI/Input   │
│  (open/read/write)  │  (socket/send)   │  (framebuffer)│
├─────────────────────┼──────────────────┼───────────────┤
│  Ext2 Filesystem    │  TCP/UDP/ICMP    │  Mouse/Kbd    │
├─────────────────────┼──────────────────┼───────────────┤
│  ATA Driver         │  IPv4/ARP/DHCP   │  PS/2 Driver  │
├─────────────────────┼──────────────────┼───────────────┤
│  PCI Bus            │  Ethernet Drv    │  VGA/FB       │
├────────────────────────────────────────────────────────┤
│          Process Scheduler + Memory Management         │
├────────────────────────────────────────────────────────┤
│                Hardware (x86-64)                       │
└────────────────────────────────────────────────────────┘
```

### Memory Model
- **Kernel**: `-mcmodel=kernel` (upper 2GB address space)
- **User Space**: Ring 3 with TSS-based privilege switching
- **Paging**: Identity mapping (currently no demand paging)
- **Heap**: Custom kernel heap allocator (`kmalloc`/`kfree`)

## Codebase Structure

### Directory Layout

```
/home/user/alos/
├── src/                      # Source code root
│   ├── arch/                 # Architecture-specific code
│   │   ├── x86/              # Legacy 32-bit (deprecated)
│   │   └── x86_64/           # Current 64-bit implementation
│   │       ├── gdt.c/h       # Global Descriptor Table
│   │       ├── idt.c/h       # Interrupt Descriptor Table
│   │       ├── interrupts.s  # Interrupt handlers (ASM)
│   │       ├── switch.s      # Context switching (ASM)
│   │       ├── tss.c/h       # Task State Segment
│   │       ├── usermode.c/h  # Ring 3 setup
│   │       ├── cpu.c/h       # CPU detection/features
│   │       └── linker.ld     # Linker script
│   │
│   ├── kernel/               # Kernel core
│   │   ├── kernel.c          # Main entry point (_start)
│   │   ├── console.c/h       # VGA text console
│   │   ├── fb_console.c/h    # Framebuffer console
│   │   ├── keyboard.c/h      # PS/2 keyboard driver
│   │   ├── keymap.c/h        # Keyboard layout (AZERTY)
│   │   ├── mouse.c/h         # PS/2 mouse driver
│   │   ├── input.c/h         # Input event system
│   │   ├── timer.c/h         # PIT timer + RTC
│   │   ├── klog.c/h          # Kernel logging system
│   │   ├── process.c/h       # Process management
│   │   ├── thread.c/h        # Threading implementation
│   │   ├── sync.c/h          # Synchronization primitives
│   │   ├── workqueue.c/h     # Deferred work
│   │   ├── syscall.c/h       # System call dispatcher
│   │   ├── elf.c/h           # ELF32/64 loader
│   │   ├── linux_compat.c/h  # Linux syscall compatibility
│   │   ├── string.c          # String utilities
│   │   └── mmio/             # Memory-mapped I/O
│   │       ├── mmio.c/h      # MMIO abstractions
│   │       └── pci_mmio.c/h  # PCI MMIO access
│   │
│   ├── mm/                   # Memory Management
│   │   ├── pmm.c/h           # Physical Memory Manager
│   │   ├── vmm.c/h           # Virtual Memory Manager (paging)
│   │   └── kheap.c/h         # Kernel heap allocator
│   │
│   ├── drivers/              # Hardware drivers
│   │   ├── pci.c/h           # PCI enumeration
│   │   ├── ata.c/h           # ATA/IDE disk driver (PIO mode)
│   │   ├── net/              # Network drivers
│   │   │   ├── pcnet.c/h     # AMD PCnet (QEMU default)
│   │   │   ├── virtio_net.c/h# VirtIO network
│   │   │   └── e1000e.c/h    # Intel E1000E (VirtualBox)
│   │   └── virtio/           # VirtIO subsystem
│   │       ├── virtio_transport.c/h
│   │       ├── virtio_pci_modern.c/h
│   │       └── virtio_mmio.c/h
│   │
│   ├── fs/                   # Filesystems
│   │   ├── vfs.c/h           # Virtual File System layer
│   │   └── ext2.c/h          # Ext2 implementation
│   │
│   ├── net/                  # Network stack (OSI layers)
│   │   ├── core/             # Network infrastructure
│   │   │   ├── net.c/h       # Core networking
│   │   │   └── netdev.c/h    # Network device abstraction
│   │   ├── l2/               # Layer 2 (Data Link)
│   │   │   ├── ethernet.c/h  # Ethernet frames
│   │   │   └── arp.c/h       # Address Resolution Protocol
│   │   ├── l3/               # Layer 3 (Network)
│   │   │   ├── ipv4.c/h      # Internet Protocol v4
│   │   │   ├── icmp.c/h      # ICMP (ping)
│   │   │   └── route.c/h     # Routing table
│   │   └── l4/               # Layer 4 (Transport)
│   │       ├── udp.c/h       # User Datagram Protocol
│   │       ├── tcp.c/h       # Transmission Control Protocol
│   │       ├── dhcp.c/h      # DHCP client
│   │       ├── dns.c/h       # DNS resolver (with cache)
│   │       ├── http.c/h      # HTTP client
│   │       └── httpd.c/h     # HTTP server
│   │
│   ├── shell/                # Interactive shell
│   │   ├── shell.c/h         # Shell core (readline, history)
│   │   └── commands.c/h      # Built-in commands
│   │
│   ├── gui/                  # Graphical User Interface
│   │   ├── gui.c/h           # GUI core
│   │   ├── render.c/h        # Rendering engine
│   │   ├── wm.c/h            # Window manager
│   │   ├── compositor.c/h    # Compositor
│   │   ├── events.c/h        # Event handling
│   │   ├── font.c/h          # Font rendering
│   │   └── ...               # Additional GUI components
│   │
│   ├── config/               # Configuration system
│   │   └── config.c/h        # Config file parser
│   │
│   ├── userland/             # User space programs
│   │   ├── libc/             # Minimal C library
│   │   ├── threads-test.c    # Threading test program
│   │   └── gui/              # GUI applications
│   │
│   └── include/              # Shared headers
│       ├── limine.h          # Limine boot protocol
│       ├── multiboot.h       # Multiboot specification
│       ├── elf.h             # ELF format definitions
│       ├── string.h          # String functions
│       └── memlayout.h       # Memory layout constants
│
├── disk_structure/           # Disk image template
│   ├── config/               # Config files (/config)
│   ├── system/logs/          # System logs (/system/logs)
│   └── www/                  # Web server root (/www)
│
├── fs_root/                  # Generated: filesystem staging
├── disk.img                  # Generated: Ext2 disk image (64MB)
├── alos.elf                  # Generated: kernel binary
├── alos.iso                  # Generated: bootable ISO
├── iso_root/                 # Generated: ISO staging directory
├── limine/                   # Downloaded: Limine bootloader
│
├── Makefile                  # Build system
├── limine.conf               # Bootloader configuration
├── Dockerfile                # Docker build environment
├── README.md                 # User documentation
├── LINUX-COMPAT.md          # Linux compatibility guide
└── MULTIPROCESSING-OSDEV.org # Development notes
```

## Build System

### Prerequisites
- **Cross-compiler**: `x86_64-elf-gcc` (or native GCC as fallback)
- **Assembler**: `nasm` (for .s files)
- **Linker**: `x86_64-elf-ld`
- **ISO creation**: `xorriso`
- **Disk utilities**: `mkfs.ext2` (e2fsprogs)
- **Emulator**: `qemu-system-x86_64` or VirtualBox

### Build Targets

```bash
# Full build workflow
make clean      # Remove all build artifacts
make            # Compile kernel → alos.elf
make iso        # Create bootable ISO → alos.iso
make run        # Build + run in VirtualBox (default)
make run-qemu   # Build + run in QEMU with UEFI
make distclean  # Clean everything including Limine

# Disk image management
make disk.img   # Rebuild disk image from disk_structure/
make userland   # Build user space programs

# Debug and testing
make debug      # Run with GDB server (port 1234)
make run-pcap   # Run with packet capture
make run-tap    # Run with TAP networking (requires setup)
```

### Compilation Flags (CRITICAL)

```makefile
# x86-64 kernel flags - DO NOT MODIFY without understanding
CFLAGS = -std=gnu99 -ffreestanding -O2 -g -Wall -Wextra \
         -m64 -march=x86-64 -mcmodel=kernel \
         -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
         -fno-stack-protector -fno-pic -fno-pie
```

**Critical Flags Explanation**:
- `-mno-red-zone`: **REQUIRED** - Disables 128-byte red zone (incompatible with interrupts)
- `-mno-sse*`: Prevents Invalid Opcode exceptions (no FPU init in kernel)
- `-mcmodel=kernel`: Places kernel in upper 2GB address space
- `-fno-stack-protector`: No libc available for `__stack_chk_fail`

### Assembly Files
```makefile
ASFLAGS = -f elf64 -g
```
- `src/arch/x86_64/interrupts.s`: IDT handlers, system call entry
- `src/arch/x86_64/switch.s`: Context switching assembly

## Development Workflows

### Adding a New Feature

1. **Plan the feature** - Understand which subsystems are affected
2. **Read existing code** - Check similar implementations first
3. **Modify incrementally** - Make small, testable changes
4. **Test frequently** - Use `make run` after each change
5. **Check logs** - Monitor `/system/logs/kernel.log` in the OS

### Common Development Tasks

#### Adding a New System Call

**Location**: `src/kernel/syscall.c` + `src/kernel/syscall.h`

1. Define syscall number in `syscall.h`:
   ```c
   #define SYS_MYNEWCALL 200  /* Description */
   ```

2. Implement handler in `syscall.c`:
   ```c
   int64_t sys_mynewcall(int64_t arg1, int64_t arg2) {
       // Implementation
       return 0;
   }
   ```

3. Register in `syscall_table[]`:
   ```c
   [SYS_MYNEWCALL] = (void*)sys_mynewcall,
   ```

4. Add to userland libc wrapper (`src/userland/libc/syscall.s`)

#### Adding a Hardware Driver

**Location**: `src/drivers/`

1. Create `mydriver.c` and `mydriver.h`
2. Implement `mydriver_init()` function
3. Register PCI device ID if needed (`src/drivers/pci.c`)
4. Call init from `kernel_main()` in `src/kernel/kernel.c`
5. Add to Makefile `DRIVERS_SRC` and `DRIVERS_OBJ`

#### Adding a Network Protocol

**Location**: `src/net/l{2,3,4}/`

1. Choose OSI layer (L2=Ethernet, L3=IP, L4=TCP/UDP)
2. Create protocol handler (`myproto.c/h`)
3. Register with lower layer (e.g., `ipv4_register_protocol()`)
4. Add to Makefile `NET_L{N}_SRC` and `NET_L{N}_OBJ`

#### Modifying the Shell

**Location**: `src/shell/commands.c`

1. Add command handler function:
   ```c
   static void cmd_mycommand(int argc, char **argv) {
       printf("Executing mycommand\n");
   }
   ```

2. Register in `commands[]` array:
   ```c
   {"mycommand", cmd_mycommand, "Description"},
   ```

### Debugging Workflow

#### Serial Logging
All kernel messages go to serial port (COM1):
```bash
# VirtualBox captures to serial.log
tail -f serial.log

# QEMU prints to stdio
```

#### Kernel Logging
Use the klog system (`src/kernel/klog.h`):
```c
KLOG_INFO("Module initialized: value=%d", value);
KLOG_ERROR("Failed to allocate memory");
KLOG_DEBUG("Detailed trace info");
```

Log levels (adjust in `klog.h`):
- `KLOG_LEVEL_ERROR`: Critical errors only
- `KLOG_LEVEL_WARNING`: + Warnings
- `KLOG_LEVEL_INFO`: + Informational (default)
- `KLOG_LEVEL_DEBUG`: + Verbose debugging

#### GDB Debugging
```bash
# Terminal 1
make debug  # Starts QEMU with -s -S

# Terminal 2
gdb alos.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

#### Common Debug Points
- `_start` (src/arch/x86_64/linker.ld): First kernel code
- `kernel_main()`: Main initialization
- `schedule()`: Scheduler entry point
- `syscall_dispatcher()`: System call handling
- `page_fault_handler()`: Memory faults

### Testing Procedures

#### Quick Smoke Test
```bash
make run
# In ALOS shell:
> help          # Shell works?
> ls /          # VFS works?
> ping 8.8.8.8  # Network works?
> exec /bin/threads-test  # Multitasking works?
```

#### Network Testing
```bash
# Enable DHCP
> dhcp

# Test DNS
> ping google.com

# Test HTTP
> http get http://example.com

# Test HTTP server
> httpd start
# On host: curl http://10.0.2.15/
```

#### Linux Compatibility Testing
```bash
> linux on
> exec /bin/busybox ls
```

## Coding Conventions

### Style Guidelines

1. **Indentation**: Spaces (appears to be 2-space in most files)
2. **Naming**:
   - Functions: `snake_case` (e.g., `process_create()`)
   - Types: `snake_case_t` (e.g., `process_t`)
   - Macros: `UPPER_SNAKE_CASE` (e.g., `MAX_PROCESSES`)
   - Global variables: Avoid when possible
3. **Comments**: French language (but code identifiers are English)
4. **Headers**: Include guards using `#ifndef FILENAME_H`

### Code Patterns

#### Memory Allocation
```c
// Kernel memory
void *ptr = kmalloc(size);
if (!ptr) {
    KLOG_ERROR("kmalloc failed");
    return -ENOMEM;
}
// Use ptr...
kfree(ptr);
```

#### Error Handling
```c
// Return negative errno on error
int my_function() {
    if (error_condition) {
        return -EINVAL;  // Invalid argument
    }
    return 0;  // Success
}
```

#### Locking (when implemented)
```c
spin_lock(&lock);
// Critical section
spin_unlock(&lock);
```

### File Header Template
```c
/* src/subsystem/module.c - Brief description */
#include "module.h"
#include <stdint.h>
// Other includes...

// Private definitions
static void internal_function(void) {
    // ...
}

// Public API
void public_function(void) {
    // ...
}
```

## Important Subsystems

### Memory Management

**PMM** (`src/mm/pmm.c`): Physical memory allocator
- `pmm_init()`: Initialize from Limine memory map
- `pmm_alloc_page()`: Allocate 4KB physical page
- `pmm_free_page()`: Free physical page

**VMM** (`src/mm/vmm.c`): Virtual memory manager
- Identity mapping (1:1 physical-to-virtual)
- Page table management
- **Note**: No demand paging yet

**Heap** (`src/mm/kheap.c`): Kernel heap allocator
- `kmalloc(size)`: Allocate kernel memory
- `kfree(ptr)`: Free kernel memory
- Simple first-fit allocator

### Process Management

**Processes** (`src/kernel/process.c`):
- Process control blocks (PCB)
- Priority-based scheduler (0-3, 0=highest)
- Round-robin within priority levels
- `process_create()`: Create new process
- `process_exit()`: Terminate process

**Threads** (`src/kernel/thread.c`):
- Kernel threads
- User threads (via `SYS_CLONE`)
- Thread-local storage (TLS)
- Sleep/wakeup mechanisms

**Scheduling**:
- Cooperative + preemptive (timer-based)
- Context switch in `src/arch/x86_64/switch.s`
- States: READY, RUNNING, BLOCKED, TERMINATED

### File System

**VFS** (`src/fs/vfs.c`): Virtual File System
- Unified interface for all filesystems
- Operations: `vfs_open()`, `vfs_read()`, `vfs_write()`, `vfs_readdir()`
- Current working directory per process

**Ext2** (`src/fs/ext2.c`): Ext2 implementation
- Read and write support
- Inode-based filesystem
- Block groups, bitmaps, indirect blocks
- **Limitations**: No journaling, symbolic links

### Network Stack

**Architecture**: OSI model (L2 → L4)
- **L2**: Ethernet frames, ARP cache
- **L3**: IPv4, ICMP (ping), routing table
- **L4**: UDP, TCP (full state machine), DHCP client, DNS resolver

**TCP Implementation** (`src/net/l4/tcp.c`):
- Full RFC 793 state machine
- SYN/ACK handshake
- Retransmission timers
- Window management
- **Limitations**: Basic implementation, no congestion control

**Packet Flow**:
```
RX: NIC → Ethernet → IPv4 → TCP/UDP → Socket
TX: Socket → TCP/UDP → IPv4 → Ethernet → NIC
```

### System Calls

**Entry Point**: `int 0x80` (x86-64 uses same interface)
**Dispatcher**: `src/kernel/syscall.c:syscall_dispatcher()`

**Register Convention**:
- RAX: Syscall number
- RDI, RSI, RDX, R10, R8, R9: Arguments 1-6
- RAX: Return value (or -errno)

**Linux Compatibility**: `src/kernel/linux_compat.c`
- Translates Linux syscall numbers to ALOS
- Supports static i386 Linux binaries
- Enable with `linux on` shell command

## Key Files Reference

### Boot and Initialization
- `src/arch/x86_64/linker.ld`: Memory layout, entry point
- `src/kernel/kernel.c:_start()`: Limine entry, BSS clear
- `src/kernel/kernel.c:kernel_main()`: Main initialization sequence

### Critical Assembly
- `src/arch/x86_64/interrupts.s`: IDT handlers (IRQs, exceptions, syscall)
- `src/arch/x86_64/switch.s`: `switch_to_context()` - context switching

### Configuration Files
- `limine.conf`: Bootloader configuration (kernel path, framebuffer)
- `disk_structure/config/network.conf`: Static network configuration
- `disk_structure/config/startup.sh`: Boot script

### Memory Layout (`src/include/memlayout.h`)
```c
#define KERNEL_BASE 0xFFFFFFFF80000000  // -2GB
#define USER_BASE   0x0000000000400000  // 4MB
```

## Common Pitfalls and Gotchas

### 1. Red Zone Violation
**Problem**: Random crashes in interrupt handlers
**Cause**: Compiler uses red zone (128 bytes below RSP)
**Solution**: Already set with `-mno-red-zone` - DO NOT REMOVE

### 2. SSE/FPU Exceptions
**Problem**: Invalid Opcode (#UD) on FPU/SSE instructions
**Cause**: CR0.EM or CR4.OSFXSR not set, or using SSE in kernel
**Solution**: `-mno-sse -mno-sse2 -mno-mmx` flags (already set)

### 3. Stack Overflow
**Problem**: Triple fault or page fault during deep call chains
**Cause**: Kernel stack is limited (typically 8KB per thread)
**Solution**:
- Avoid deep recursion
- Use heap allocation for large buffers
- Check stack usage in `src/kernel/thread.c`

### 4. Page Faults
**Problem**: Page fault (#PF) in kernel or user code
**Debug**:
- Check `page_fault_handler()` logs
- Verify address is mapped (use serial log)
- Check for NULL pointer dereferences

### 5. Network Issues
**Problem**: No DHCP response / packets not sent
**Check**:
- NIC driver initialized? (`lspci` command)
- Correct driver for emulator? (PCnet=QEMU, E1000=VirtualBox)
- QEMU networking: `-netdev user` should work
- VirtualBox: NAT or Bridged adapter

### 6. Filesystem Corruption
**Problem**: Cannot mount disk.img or read files
**Solution**:
```bash
# Check filesystem
e2fsck -f disk.img

# Rebuild from scratch
make disk.img
```

### 7. Build Errors
**Problem**: Undefined reference to `__stack_chk_fail`
**Solution**: Ensure `-fno-stack-protector` is in CFLAGS

**Problem**: Relocation errors
**Solution**: Ensure `-mcmodel=kernel -fno-pic -fno-pie`

## Git Workflow

### Branch Structure
- Feature branches: `claude/claude-md-*` (automatically created)
- Main branch: Empty in current status (orphan commit)

### Commit Guidelines
1. **Use clear, descriptive messages** in English (though codebase comments are French)
2. **Reference issue numbers** if applicable
3. **Commit frequently** - small, atomic changes
4. **Test before committing** - ensure `make` succeeds

### Push Protocol
```bash
# Always push to feature branch with -u flag
git push -u origin claude/claude-md-miyplikd8cmht2kg-018KBMxw2oyoe5AXSSJtzKyB

# Branch naming MUST start with 'claude/' and end with session ID
# Otherwise push will fail with HTTP 403
```

### Network Retry Policy
If git operations fail due to network issues:
- Retry up to 4 times with exponential backoff: 2s, 4s, 8s, 16s
- Applies to: `git fetch`, `git pull`, `git push`

## Documentation and Resources

### Internal Documentation
- `README.md`: User-facing documentation, feature checklist
- `LINUX-COMPAT.md`: Linux binary compatibility guide
- `MULTIPROCESSING-OSDEV.org`: Development notes (Org mode)
- This file (`CLAUDE.md`): AI assistant guide

### External Resources
- [OSDev Wiki](https://wiki.osdev.org/): Essential OS development reference
- [Intel x86-64 Manual](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html): Architecture specification
- [Linux Syscall Reference](https://man7.org/linux/man-pages/man2/syscalls.2.html): For Linux compatibility
- [Limine Boot Protocol](https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md): Bootloader interface
- [Ext2 Specification](https://www.nongnu.org/ext2-doc/ext2.html): Filesystem format

## Quick Reference Commands

### Shell Commands (in ALOS)
```
help         - List all commands
ls [path]    - List directory
cat [file]   - Display file contents
mkdir [path] - Create directory
cd [path]    - Change directory
exec [prog]  - Execute program
kbhit        - Keyboard test
clear        - Clear screen
meminfo      - Memory statistics
lspci        - List PCI devices
dhcp         - Request IP via DHCP
ping [host]  - Ping host (IP or DNS name)
httpd start  - Start HTTP server on port 80
linux [on|off] - Toggle Linux compatibility mode
```

### Makefile Targets
```
make          - Build kernel (alos.elf)
make iso      - Create bootable ISO
make run      - Run in VirtualBox (default)
make run-qemu - Run in QEMU with UEFI
make clean    - Clean build artifacts
make distclean - Clean everything
make debug    - Run with GDB server
make userland - Build user programs
make disk.img - Rebuild disk image
```

## AI Assistant Best Practices

### Before Making Changes
1. **Read the relevant files first** - Understand existing patterns
2. **Check dependencies** - Will this affect other subsystems?
3. **Review recent commits** - Understand recent changes
4. **Search for similar code** - Follow established patterns

### When Writing Code
1. **Match existing style** - Consistent indentation, naming
2. **Add error handling** - Check return values, handle NULL
3. **Use kernel logging** - Add KLOG statements for debugging
4. **Comment in French** - Match existing documentation style
5. **Keep it simple** - This is educational code

### Testing Strategy
1. **Build after each change** - `make`
2. **Test in emulator** - `make run`
3. **Check serial logs** - `tail -f serial.log`
4. **Test affected subsystems** - E.g., if changing network code, test `ping`
5. **Look for regressions** - Ensure old features still work

### Communication with User
1. **Explain what you're doing** - Don't just write code silently
2. **Show file locations** - Use `file:line` format
3. **Highlight trade-offs** - Explain design decisions
4. **Warn about risks** - Flag potentially breaking changes
5. **Suggest testing steps** - How should they verify the changes?

### When Stuck
1. **Search codebase** - Use Grep/Glob to find examples
2. **Check OSDev wiki** - Most common issues are documented
3. **Review similar code** - How did existing code solve this?
4. **Ask clarifying questions** - Don't guess at requirements
5. **Propose alternatives** - Suggest multiple approaches

## Security Considerations

### Current Status
ALOS is an **educational OS** with minimal security:
- ❌ No user authentication
- ❌ No file permissions
- ❌ No sandboxing
- ❌ No memory protection between processes (identity mapping)
- ❌ No ASLR/DEP
- ⚠️ Basic Ring 0/3 separation only

### What to Avoid
1. **Don't add backdoors** - Even for "testing"
2. **Validate user input** - Check buffer sizes, path traversal
3. **No hardcoded credentials** - Even if temporary
4. **Be careful with network code** - Buffer overflows are easy

### Input Validation Example
```c
// BAD - no bounds checking
void read_file(char *path) {
    char buf[256];
    strcpy(buf, path);  // Buffer overflow!
}

// GOOD - validate input
void read_file(const char *path) {
    if (!path || strlen(path) >= MAX_PATH) {
        return -EINVAL;
    }
    // Safe to use path...
}
```

## Performance Considerations

### Current Bottlenecks
1. **ATA driver**: PIO mode (no DMA) - very slow disk I/O
2. **Network**: Polling mode, no interrupt coalescing
3. **Scheduler**: O(n) scan of all threads
4. **Memory allocator**: Simple first-fit, no free list coalescing

### Optimization Guidelines
1. **Profile first** - Don't optimize without data
2. **Avoid premature optimization** - Clarity > speed for education
3. **Use appropriate data structures** - Consider O(n) vs O(1) lookup
4. **Minimize locks** - When threading is fully implemented
5. **Cache-friendly code** - Sequential access patterns

### Acceptable Latencies (for educational OS)
- Boot time: ~5 seconds
- Disk I/O: ~10ms per sector (PIO mode)
- Network ping: ~50ms (includes QEMU overhead)
- Context switch: <10μs

## Future Development Areas

### High Priority (from README.md)
- Fork/exec implementation
- Demand paging / page fault handler
- Pipes and file descriptors
- Dynamic linking (.so support)
- AHCI/SATA driver (DMA)

### Medium Priority
- USB stack (XHCI)
- Window manager improvements
- More filesystems (FAT32, ISO9660)
- IPv6 support
- Multi-core (SMP)

### Low Priority / Nice to Have
- Virtualization (KVM guest)
- Audio driver
- WiFi support
- TLS/SSL
- Compiler toolchain port

## Conclusion

ALOS is a well-structured educational operating system with working implementations of core OS concepts. When assisting with this codebase:

1. **Respect the educational purpose** - Keep code readable and well-commented
2. **Test thoroughly** - This is low-level code where bugs crash the system
3. **Document changes** - Future learners will read this code
4. **Follow existing patterns** - Consistency aids understanding
5. **Have fun!** - OS development is challenging but rewarding

For questions or clarifications about this guide, refer to the user documentation in README.md or ask the project maintainer.

---
*Document Version: 1.0*
*Last Updated: 2025-12-09*
*Target Audience: AI Assistants (Claude, etc.)*
