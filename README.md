# ALOS - Alexy Operating System

ALOS is a minimalist x86-64 operating system kernel written in C and x86-64 Assembly, designed for learning and active experimentation. It boots through Limine and runs on QEMU or VirtualBox. It implements core OS concepts including memory management, privilege separation, multitasking, storage, filesystem support, a TCP/IP networking stack, and a graphical user interface.

> **Note:** Code comments in this project are written in **French**. The codebase serves as both a learning resource and a functional kernel.

## Current development focus

The current priority is to keep the x86-64 scheduler, Ring 0/Ring 3 transitions, TSS handling, and `iretq` context restoration regression-free while developing the native runtime and multiprocess desktop.

`fork`, `execve`, `waitpid`, sparse demand-paged `mmap`, static ELF TLS, SIMD context preservation and basic pthread support are implemented. Automated QEMU regression scripts exercise these features on actual ALOS executables.

Next major milestones:
1. Continue the Chromium/C++ runtime bootstrap and upstream build integration
2. Complete missing POSIX services: pipes, signals, socket APIs and event multiplexing
3. Copy-on-Write and coherent shared file mappings
4. AHCI/SATA DMA, then NVMe

**Chromium is not yet runnable on ALOS.** Target LLVM 18.1.8 `libc++`/`libc++abi` archives and a native C++ integration test have been built, but the experimental Chromium GN/platform patch still has dependency and configuration blockers. This is not a working Blink, V8, Mojo or Ozone port. See [the detailed port status](docs/chromium-port.md).

## Features implemented and future plans

### Core System ✓
- [x] GDT/IDT setup
- [x] Physical Memory Manager
- [x] Kernel Heap
- [x] Virtual Memory (Paging) and separate user process address spaces
- [x] RTC Real-Time Clock
- [x] PIT Programmable Interval Timer
- [x] Kernel Logging System (file-based, /system/logs)
- [x] System logs
- [ ] ACPI Support (power management, shutdown/reboot)
- [ ] SMP/Multi-core support
- [ ] APIC/x2APIC (Advanced interrupt handling)
- [ ] Kernel modules loading (dynamic drivers)

### Memory Management
- [x] Per-process page tables and VM region management
- [x] Sparse demand paging for mmap regions
- [ ] Copy-on-Write (COW)
- [x] Anonymous mmap and private file mappings
- [x] mmap protections, munmap and private MADV_DONTNEED
- [x] Shared memory objects and shared anonymous mappings
- [ ] Coherent shared mappings of ordinary files
- [ ] Swap support

### Process & Scheduling
- [x] Multitasking (Round Robin scheduler)
- [x] x86-64 Context Switching (kernel and user threads)
- [x] User Space (Ring 3) with TSS
- [x] Ring 0/Ring 3 `iretq` context restoration
- [x] System Calls (POSIX/BSD-like interface)
- [x] Priority-based scheduler
- [ ] Process groups and sessions
- [x] Fork, execve and waitpid (with current ALOS limitations)
- [x] Static ELF TLS and per-thread FS base
- [x] x87/SSE/AVX context preservation (FXSAVE or XSAVE)
- [ ] Signals (POSIX-like)
- [ ] Real-time scheduling (SCHED_FIFO, SCHED_RR)

### IPC (Inter-Process Communication)
- [ ] Pipes (anonymous and named/FIFO)
- [ ] Message queues
- [ ] Semaphores
- [x] Native named IPC channels with blocking waits
- [x] Shared memory objects and controlled SHM descriptor transfer
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
- [x] Multiprocess userland desktop + mouse
- [x] Window manager/compositor with independent ELF applications
- [ ] OpenGL support
- [x] Framebuffer console (VESA/GOP)
- [ ] UTF-8 string and console
- [x] Font rendering (TrueType/FreeType)
- [x] Damage-based compositor rendering and GUI event-loop optimizations
- [x] Basic GUI component/widget framework
- [x] Desktop environment (launcher, taskbar, maximize/minimize, split screen)
- [ ] Advanced widget toolkit (layout, theming, richer controls)
- [ ] Multi-monitor support

### User Space & Applications
- [x] Static ELF64 loading
- [ ] Dynamic linking (shared libraries .so)
- [x] Native libc subset with thread-local errno
- [x] Basic pthread create/join/detach, mutexes, condition variables, once and keys
- [x] C/C++ constructors, global/TLS destructors and static TLS runtime
- [x] Target libc++/libc++abi bootstrap (limited profile)
- [ ] Math library (libm)
- [ ] Compression libraries (zlib, gzip)
- [x] Basic utilities (sh, ls, cat, mkdir, touch, rm, rmdir, ps, ping, wget, etc.)
- [ ] Broader POSIX utility coverage (cp, mv, grep, etc.)
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
- [x] RW/NX protections for mmap regions; RWX mappings rejected
- [ ] Complete executable-memory protection policy across ELF/heap mappings
- [ ] Cryptographic entropy service
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
- [x] GUI client API documentation
- [ ] User manual
- [x] Automated native QEMU VM/runtime regression suites
- [ ] Continuous integration
- [ ] Benchmarking suite

## Current limitations

ALOS remains an educational OS, not a production-secure or fully POSIX-compatible system:

- Fork rejects multithreaded processes and copies resident private pages rather than using COW. Waitpid returns the raw ALOS status.
- Demand paging and mprotect apply to managed mmap regions; mprotect does not yet cover ELF segments or the brk heap. Shared file mappings, swap and COW are not implemented.
- Shared anonymous/SHM backing is currently eager and limited to 16 MiB per object. Shared MADV_DONTNEED is unsupported.
- Ring 3 threads are preempted, but historical Ring 0 sections remain cooperative. SMP is not implemented, even when a launch script configures multiple virtual CPUs.
- TLS is static only. Pthread cancellation, rwlocks, barriers, creation attributes and process-shared synchronization are not complete.
- Native IPC is not Unix sockets or Mojo. Full POSIX socket, signal and event-wait APIs remain incomplete.
- Authentication, file permissions, sandboxing, ASLR and cryptographic entropy are not complete. Older libc wrappers do not all use uniform POSIX errno conventions.

## Project Structure

```
src/
├── arch/x86_64/       # GDT/IDT/TSS, interrupts, context switching, CPU and xstate
├── config/            # Kernel configuration
├── kernel/            # Boot, scheduling, syscalls, ELF, TLS, futex, IPC, SHM and display
├── mm/                # PMM, heap, VMM and per-process VM regions
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
├── shell/             # Kernel command interpreter
│   ├── shell.c/h      # Shell core (readline, history, parsing)
│   └── commands.c/h   # Built-in commands (help, ping, exec, etc.)
├── userland/
│   ├── cmd/           # Installed shell and utilities
│   ├── libc/          # Native libc, CRT and syscall wrappers
│   ├── desktop/       # Official desktop, window manager and compositor
│   ├── libgui/        # Client GUI library
│   ├── gui/           # Legacy GUI, rendering helpers, fonts and FreeType
│   └── *-test.c/cc    # Native regression programs
└── include/           # Shared kernel headers, Limine and ELF definitions

disk_structure/        # Filesystem template and application manifests
ports/chromium/        # Runtime build environment, scripts, patches and licenses
docs/                  # GUI/API, drivers, MMIO and Chromium port documentation
```

## Architecture Overview

The official GUI is a userland display server: `/bin/gui` is built from `src/userland/desktop/`. Independent ELF applications use `libgui.a`, native IPC and shared ARGB surfaces. Only the display owner accesses the framebuffer and hardware input. `/bin/gui-test` is the legacy GUI, not the official desktop.

The launcher reads `/share/applications/*.desktop` manifests. See [desktop architecture](docs/GUI.md) and [the client API](docs/GUI-API.md) for application integration.

```
┌─────────────────────────────────────────────────────────┐
│              User Space (Ring 3)                        │
│       Desktop + apps  │  Shell  │  libc / libgui          │
├─────────────────────────────────────────────────────────┤
│                  System Calls (int 0x80)                │
├─────────────────────────────────────────────────────────┤
│              Kernel Space (Ring 0)                      │
├─────────────────────────────────────────────────────────┤
│       VFS / VM / IPC / SHM / Display / Network APIs       │
│  (open, read, readdir)   │    (send, recv, socket)      │
├──────────────────────────┼──────────────────────────────┤
│   Ext2   │  (Future FS)  │  TCP/UDP  │ ICMP │ DHCP/DNS  │
├──────────────────────────┼──────────────────────────────┤
│      ATA Driver          │     IPv4  │  ARP  │ Ethernet │
├──────────────────────────┼──────────────────────────────┤
│      IDE Controller      │  VirtIO / PCnet / E1000E      │
├──────────────────────────┴──────────────────────────────┤
│                    PCI Bus                              │
├─────────────────────────────────────────────────────────┤
│             Hardware / VM (x86-64)                     │
└─────────────────────────────────────────────────────────┘
```

## Building

### Prerequisites

- Windows workflow: PowerShell, Docker Desktop (Linux containers), QEMU and its bundled EDK2 firmware
- Compiler: `x86_64-elf-gcc` / `x86_64-elf-ld` when available (the Makefile can fall back to the native GCC toolchain)
- Assembler: `nasm`
- ISO tooling: `xorriso`
- Disk utilities: `e2fsprogs` / `mkfs.ext2`
- Emulator: `qemu-system-x86_64` and/or VirtualBox
- Bootloader: Limine v10.x (downloaded/built automatically by the Makefile)
- Runtime/C++ builds: Clang and related tools supplied by the runtime Docker image

### Windows quick start

Run from the repository root:

```powershell
docker build -t alos-runtime -f ports\chromium\build\Dockerfile.runtime .
.\run.ps1 build          # Build kernel, userland, disk and ISO via Docker
.\run.ps1 run            # Run existing images in QEMU
.\run.ps1 debug          # Rebuild, then start paused for GDB on port 1234
Get-Content .\serial.log -Wait
```

`run.ps1` expects QEMU under `C:\Program Files\qemu` and uses the bundled `share\edk2-x86_64-code.fd` firmware. Adjust these paths in the script for a different installation. No native Windows cross-toolchain is required with Docker.

> **Disk data warning:** `make disk.img`, `make iso` and `run.ps1 build` recreate the 64 MiB `disk.img` from the template and built applications. Back up guest data before rebuilding. The regression runner below creates a separate test disk instead.

### Linux / Docker compilation

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

`make run-qemu` currently invokes `qemu-system-amd64` and uses firmware paths under `/usr/share/OVMF/`; ensure the executable and firmware exist on your distribution. Fast targets configure host-side KVM/SDL/OpenGL, not an ALOS OpenGL implementation.

On Windows, `run-debug.ps1` builds and runs the OS in a second PowerShell window and follows `serial.log` using `logs.ps1`. It does not enable GDB pause mode; use `run.ps1 debug` for that.

## Regression testing

Build the runtime image as above. The runner also uses an image named `alos-build` for Ext2 disk creation; the runtime image provides the required utilities and can be tagged for this role:

```powershell
docker tag alos-runtime alos-build
.\ports\chromium\scripts\test-vm.ps1
.\ports\chromium\scripts\test-vm.ps1 -Runtime -Cpu qemu64
.\ports\chromium\scripts\test-vm.ps1 -Runtime -SkipBuild -Cpu max
```

The runner builds kernel/userland, refreshes staging, creates a separate temporary Ext2 disk and executes native tests in headless QEMU with one CPU and virtio-net. It preserves the working `disk.img` and writes build/image/serial logs into its output directory. `-SkipBuild` requires existing binaries and ISO to match the sources.

The default suite covers mmap, fork, exec, threads, VFS and SLIRP gateway ping. `-Runtime` adds SIMD context switching, TLS, clocks, pthread, floating-point parsing/printf and C++ CRT/TLS tests. `qemu64` covers the FXSAVE path; `max` exercises XSAVE/AVX. Some mmap tests deliberately fault child processes; completion markers and assertions determine success.

The optional `-ComplexCpp` suite requires a separately built `complex-cpp-test` linked against the target upstream C++ archives. See [Chromium runtime build instructions](docs/chromium-port.md).

For GUI changes, also exercise the official desktop interactively: launch GUI Demo from Apps, move/resize windows, check focus and input, and close clients. A legacy `/bin/gui-test` run does not validate the multiprocess desktop.

## QEMU Configuration

### Storage
- Ext2 disk image: `disk.img`
- Current storage driver: ATA/IDE PIO
- Planned next-generation storage: AHCI/SATA DMA, then NVMe

### Networking
- The Windows runner and default QEMU Make target use VirtIO-net with user-mode networking; TAP and other network targets are also available
- `run.ps1` and `make run-qemu` forward host TCP port `8080` to guest port `80` (for example, `curl http://localhost:8080/` when the guest HTTP server is running)
- PCnet and Intel E1000E drivers are also present in the tree

### Debugging
- Kernel serial output is available through `serial.log` or QEMU stdio depending on the target
- `make debug` exposes the QEMU GDB server on TCP port `1234`
- `run-debug.ps1` + `logs.ps1` provide a convenient live-debug workflow on Windows

## License

This project is intended for educational purposes. Educational use is not itself a license grant.

The tree contains third-party code, including FreeType and imported musl routines. Preserve their notices and consult the bundled licenses, including `ports/chromium/MUSL-LICENSE` and `src/userland/libc/COPYRIGHT.musl`.

## Contributing

Contributions are welcome!

Read [AGENTS.md](AGENTS.md) for repository conventions and architectural constraints. Keep kernel compilation flags intact, use French comments where consistent with surrounding code, and include native regression coverage for runtime/ABI changes.
