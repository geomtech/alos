# AGENTS.md - AI Assistant Guide for ALOS

This guide applies to the entire repository. Code and build scripts are the
source of truth; documentation can lag behind active development.

## Project and current priorities

ALOS (Alexy Operating System) is an x86-64 OS written in GNU99 C
and NASM assembly, booted by Limine v10.x on QEMU or VirtualBox. Code comments
are normally in French; identifiers and commit messages are in English.

Preserve scheduler correctness, Ring 0/Ring 3 transitions, TSS handling and
`iretq` restoration before expanding functionality. Current work includes a
multiprocess userland desktop and the native runtime needed to bootstrap a
Chromium port.

Implemented surfaces include:

- GDT/IDT/TSS, interrupts, PMM, VMM and kernel heap.
- Separate user process address spaces, static ELF loading, `fork`, `execve`,
  `waitpid`, per-process working directories and file descriptors.
- Sparse demand-paged mmap regions, memory protections and shared memory.
- User threads, static ELF TLS, SIMD context preservation, private futexes
  and a basic pthread implementation.
- VFS/Ext2 read/write, ATA PIO and PCI/MMIO abstractions.
- Ethernet/ARP/IPv4/ICMP/UDP/TCP/DHCP/DNS, HTTP tools and network drivers.
- Userland shell, display server, compositor and independent GUI applications.

Do not infer full POSIX compliance or a working Chromium browser from these
features. See `docs/chromium-port.md` for contracts, limitations and recorded
milestones; some introductory README checklists are older than the code.

## Repository map

| Path | Responsibility |
| --- | --- |
| `src/arch/x86_64/` | GDT, IDT, TSS, interrupt/context assembly, CPU and xstate |
| `src/kernel/` | Boot, scheduler, processes, threads, syscalls, ELF, input and logging |
| `src/kernel/uaccess.c` | Checked kernel/user memory transfers |
| `src/kernel/tls.c`, `futex.c`, `thread_lifecycle.c` | Native thread runtime and lifecycle |
| `src/kernel/ipc.c`, `shared_memory.c`, `display.c` | Local IPC, SHM and display ownership |
| `src/kernel/mmio/` | MMIO and PCI MMIO helpers |
| `src/mm/` | PMM, VMM, kernel heap and per-process VM regions (`vm.c`) |
| `src/fs/` | VFS, Ext2 and open-file descriptions (`file.c`) |
| `src/drivers/` | PCI, ATA, network drivers and VirtIO transports |
| `src/net/core/`, `l2/`, `l3/`, `l4/` | Network infrastructure and protocol layers |
| `src/shell/` | Kernel shell and built-in commands |
| `src/config/` | Kernel configuration parser |
| `src/userland/cmd/` | Installed userland utilities, including `/bin/sh` |
| `src/userland/libc/` | ALOS libc, headers, CRT and syscall wrappers |
| `src/userland/desktop/` | Official desktop/display server |
| `src/userland/libgui/` | Client GUI library |
| `src/userland/gui/` | Legacy GUI, rendering helpers, fonts and FreeType |
| `src/userland/*-test.c`, `*-test.cc` | Native regression programs |
| `src/include/` | Shared kernel headers and ABI definitions |
| `disk_structure/` | Filesystem template, configuration and application manifests |
| `ports/chromium/` | Runtime Dockerfile, port scripts, patches and licenses |
| `docs/` | GUI/API, drivers, MMIO and Chromium documentation |

Build outputs include `alos.elf`, `alos.iso`, object files, userland binaries,
`fs_root/`, `iso_root/`, `disk.img` and `disk.vdi`. Some generated files are
tracked: inspect status and avoid accidentally including unrelated rebuilds.
`limine/` contains the bootloader binaries/utility.

## Working safely

1. Inspect `git status --short`, recent commits and the relevant implementation
   before editing. Preserve existing local work, including untracked sources.
2. Make focused changes and update directly related documentation. Follow the
   local formatting rather than imposing one indentation width on all files.
3. Keep the current checkout/branch unless the user requests a different one.
   Do not automatically create branches, stash, reset, rebase, commit or push.
4. Commit only when requested, with an English message describing the change.
   There is no repository-wide requirement for a `claude/` branch prefix or a
   hardcoded session-specific push destination.
5. Do not modify vendored FreeType or imported musl code casually. Preserve
   copyright notices and license files for any imported implementation.

## Build workflows

### Windows: PowerShell, Docker and QEMU

Use Windows paths for host commands. The Makefiles use POSIX shell utilities;
run them inside the supplied Linux Docker environment rather than assuming a
native Windows GCC produces suitable ELF binaries.

```powershell
docker build -t alos-runtime -f ports\chromium\build\Dockerfile.runtime .
.\run.ps1 build
.\run.ps1 run
.\run.ps1 debug
Get-Content .\serial.log -Wait
```

`run.ps1` uses `alos-runtime`, QEMU/EDK2 under `C:\Program Files\qemu`,
virtio-net and host TCP port 8080 forwarded to guest port 80. Debug mode adds
`-s -S` for GDB on port 1234. Its `-smp 2` option does not establish kernel
SMP support; native regression runs use one CPU.

**Data-loss warning:** `run.ps1 build` invokes `make clean iso`. The `iso`
target depends on `disk.img`; filesystem staging is rebuilt and `disk.img`
is recreated from `disk_structure/`. `run.ps1 disk` also recreates this disk.
Do not use these commands on a disk containing user data without approval or
a backup. Prefer the isolated regression runner below when preserving it.

### Make targets (Linux or Docker)

| Command | Effect |
| --- | --- |
| `make` / `make alos.elf` | Compile and link the kernel |
| `make userland` | Build libc, applications and native tests |
| `make iso` | Build bootable ISO, disk image and GUI image checks |
| `make fs_root` | Recreate filesystem staging from template and userland |
| `make disk.img` | Recreate the 64 MiB Ext2 disk image |
| `make verify-gui-staging` | Check the official desktop in staging |
| `make verify-gui-image` | Check the official desktop inside the disk image |
| `make run` / `make run-vbox` | Build ISO and start/configure VirtualBox |
| `make run-qemu` / `make run-uefi` | QEMU launch variants; inspect flags first |
| `make run-pcap` / `make run-tap` | Packet capture / TAP networking |
| `make debug` | QEMU with GDB server |
| `make clean` | Remove kernel/ISO/userland build outputs |
| `make distclean` | Also remove Limine |

The Makefile selects `x86_64-elf-gcc`/`ld` when available, otherwise native
GCC/binutils. Other requirements include NASM, xorriso, e2fsprogs, Git and
QEMU or VirtualBox. The runtime image additionally supplies Clang/C++ tools.

### Kernel flags: preserve these invariants

```makefile
CFLAGS = -std=gnu99 -ffreestanding -O2 -g -Wall -Wextra \
         -m64 -march=x86-64 -mcmodel=kernel \
         -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
         -fno-stack-protector -fno-pic -fno-pie
ASFLAGS = -f elf64 -g
LDFLAGS = -nostdlib -z max-page-size=0x1000
```

The kernel cannot use the red zone, host libc or implicit SIMD instructions.
Userland SIMD is supported through xstate save/restore; that is not a reason
to enable SSE in kernel C. Preserve separate kernel and userland flags and
their respective linker scripts. C++ runtime tests use Clang targeting
`x86_64-unknown-none-elf`, static linking, local-exec TLS, no exceptions/RTTI
and hosted language mode with `-fno-builtin`; do not import a host CRT/libc.

## Architectural contracts

### Memory and process lifecycle

- `vmm.c` handles page tables; `vm.c` tracks per-process regions and faults.
  Do not describe all user memory as identity-mapped or unprotected.
- mmap supports sparse anonymous private reservations, private file mappings
  and SHM/shared anonymous mappings. The mmap arena is `[4 GiB, 64 TiB)`.
- RW/NX permissions are enforced for these regions; RWX is rejected.
  `mprotect` does not yet cover ELF segments or the brk heap.
- Ordinary file `MAP_SHARED` is unsupported. Shared backing is currently
  eager and limited to 16 MiB per object; shared `MADV_DONTNEED` is unsupported.
- There is no COW or swap. Fork copies resident private pages and preserves
  shared backings; it rejects multithreaded processes. Waitpid still exposes
  the raw ALOS status rather than a complete POSIX wait encoding.
- Use checked uaccess helpers for user pointers, including fault-in where
  needed. Keep backing references and cleanup correct across fork/exec/exit.
- Check allocation failure, overflow, alignment and rollback paths. Avoid
  large stack buffers/deep recursion; kernel stacks are bounded.
- mincore reports actual residency without faulting queried data, including
  sparse/PROT_NONE regions and mapped ELF/stack/heap pages. System-memory
  reporting uses boot-usable PMM-managed capacity/free bytes and zero swap,
  not maximum physical address, DIMM inventory or complete process RSS.

### Scheduling and runtime

- Ring 3 threads are preempted; historical Ring 0 sections remain cooperative.
  SMP is not implemented. Do not assume UP critical sections are SMP-safe.
- Preserve interrupt frame layout, TSS stacks and `iretq` restoration.
  Keep the idle thread out of ordinary run queues and priority boosting.
- Save/restore per-thread FS/TLS and x87/SSE/AVX state. XSAVE/XRSTOR is used
  when supported, otherwise FXSAVE/FXRSTOR.
- TLS is static ELF Variant II, not dynamic TLS/dlopen. Thread-local errno
  also has a dedicated kernel-backed location.
- Private futexes and basic pthread create/join/detach, mutexes, condvars,
  reader/writer locks, once and keys exist. Rwlocks use mutex/condvar/futex
  blocking, concurrent readers and exclusive writers, with reader preference
  and no FIFO guarantee. Try operations use a nonblocking trylock even on
  the internal gate. Rwlock attributes, timed locks and process-shared
  operation are unsupported; cancellation and barriers remain incomplete.
  Creation attributes support init/destroy, stack size and detach state;
  pthread_getattr_np queries native stacks. Caller-supplied stacks and the
  full scheduling/attribute API are not implemented. gettid is a native
  numeric thread ID, not a cast of the opaque pthread_t.
- CLOCK_BOOTTIME=2, CLOCK_MONOTONIC_RAW=3 and CLOCK_MONOTONIC_COARSE=4
  share CLOCK_MONOTONIC kernel uptime at millisecond resolution; do not
  imply suspend/resume or NTP support. gettimeofday uses realtime and
  returns zero UTC fields for a supplied struct timezone.
  CLOCK_THREAD_CPUTIME_ID=5 uses current-thread PIT CPU accounting, not an
  uptime alias; avoid double-counting elapsed runtime at context switches.
  CLOCK_REALTIME_COARSE=6 uses realtime at millisecond resolution.
  usleep converts microseconds to seconds/nanoseconds and blocks through
  nanosleep; it is not a busy wait.
  Musl-derived gmtime/gmtime_r, timegm and strftime/strftime_l
  provide UTC calendar support. time, localtime/localtime_r and mktime
  also exist, with UTC as the only local-time profile, not timezone rules.
  strptime uses C-locale parsing; tzset keeps UTC and sets ENOTSUP for
  unsupported TZ names, not a pretend local timezone configuration.
- sscanf/vsscanf, fgets and getc/putc/ungetc (one-byte pushback consumed by fread)
  exist, without a claim of full POSIX scanning/stdio compliance. Locale
  support is limited to C/POSIX/UTF-8 names and C formats.
  sysconf(_SC_NPROCESSORS_CONF) returns one for the configured UP kernel,
  not the host/guest CPUID physical CPU count.
- Prefer real blocking waits with correct wake/timeout/exit cleanup over
  polling or success-shaped stubs.

### Syscall changes

The ABI uses `int 0x80`: RAX is the number, RDI/RSI/RDX/R10/R8/R9 are the six
arguments, and RAX holds the signed 64-bit result. Numbers are ALOS-defined,
not interchangeable with Linux despite some Linux-like values.

For a new syscall, inspect existing numbers in `src/kernel/syscall.h`, add
the handler and **switch case** in `src/kernel/syscall.c`, and update the
userland declarations/wrappers in `src/userland/libc/include/sys/syscall.h`
and `src/userland/libc/src/sys/syscalls.c` or the relevant libc module.
There is no `syscall_table[]` registration pattern to copy.

Use kernel negative errno returns where appropriate. New POSIX-facing libc
APIs translate failures to their documented errno conventions; older wrappers
are not all normalized. Preserve each API's contract rather than assuming all
wrappers return raw kernel results or all return `-1`.

### GUI and shell

`/bin/gui` is `src/userland/desktop_app`, the official userland display
server. `/bin/gui-test` is the legacy GUI. Do not substitute one for the
other; keep staging/image checks intact.

Only the display owner maps the framebuffer and consumes hardware input.
Clients use `libgui.a`, SHM ARGB surfaces and bounded native IPC. Keep window
ownership tied to the connection; client exit must not kill the desktop.
Native IPC is not Mojo or generic Unix FD passing.

Add applications as separate ELF programs with manifests in
`disk_structure/share/applications/`; launcher `Exec` paths must be absolute
under `/bin`. See `docs/GUI.md` and `docs/GUI-API.md`.
Distinguish `src/shell/commands.c` kernel built-ins from the installed shell
and utilities in `src/userland/cmd/`.

### Drivers and network

Register source/object lists in the Makefile when adding a kernel module.
Reuse PCI/MMIO and network-device abstractions. Packet flow remains
NIC -> Ethernet -> IPv4 -> TCP/UDP and the reverse for transmit.
QEMU Windows/regression scripts use virtio-net; other launch targets differ.
Inspect their flags rather than assuming PCnet everywhere.

The native TCP/IP stack is not a complete POSIX socket API. Passive IPv4
sockets/poll are natively tested. Unnamed AF_UNIX stream pairs and SCM_RIGHTS
descriptor passing are implemented for the Chromium/Mojo transport path, but
named Unix sockets and peer credentials are not. A native active TCP connect
SYN path exists; keep it regression-tested before treating it as established,
and do not assume retransmission/timeout coverage that has not been observed.
getaddrinfo now has an IPv4 A-record resolver syscall backed by the native DNS
client; it serializes the resolver's single outstanding query and uses a wait
queue rather than userland polling. Keep external-DNS tests separate from the
deterministic fleet; use test-vm.ps1 -DnsOnly when the environment permits
real DNS. IPv6 and full socket-option support remain unsupported. Authentication, permissions, sandboxing and ASLR are not complete.
getentropy uses a trusted host-backed legacy VirtIO RNG,
blocks with a bounded wait and fails closed when unavailable; native tests
are not cryptographic health tests. See docs/entropy.md for deployment trust
and driver limits. Never replace required entropy with a deterministic PRNG.

## Validation and debugging

Run the smallest relevant build/test first. Kernel changes need kernel
compilation; libc/ABI changes also need affected userland programs and actual
ALOS/QEMU execution, not merely a host executable test. Documentation-only
changes do not require rebuilding the OS.

The regression runner builds kernel/userland together, refreshes staging and
creates a **separate test disk**, preserving the user's `disk.img`. It uses
both `alos-runtime` for compilation and `alos-build` for image creation.
The latter must provide `sh`, `truncate`, `mkfs.ext2` and `debugfs`; the root
Dockerfile does not explicitly install e2fsprogs. If this image is missing,
the runtime image can also be tagged for the image-creation step:

```powershell
docker tag alos-runtime alos-build
.\ports\chromium\scripts\test-vm.ps1
.\ports\chromium\scripts\test-vm.ps1 -Runtime -Cpu qemu64
.\ports\chromium\scripts\test-vm.ps1 -Runtime -SkipBuild -Cpu max
```

Use `-SkipBuild` only when outputs match the sources under test. The default
suite runs mmap/fork/exec/threads, VFS and gateway ping checks. `-Runtime`
adds SIMD, TLS, clocks, pthread, floating-point parsing/printf and C++ CRT/TLS
tests. Test both `qemu64` (FXSAVE) and `max` (XSAVE/AVX) for xstate changes.
Faults from deliberately invalid child accesses in mmap-test are expected;
judge the suite's assertions/completion markers, not the absence of faults.

For GUI changes also exercise the real desktop: launcher click, separate
application launch, input/focus, resizing and client termination. A successful
legacy gui-test run is not a desktop regression test.

Use `KLOG_INFO`, `KLOG_ERROR` and `KLOG_DEBUG` from `src/kernel/klog.h`.
Inspect `serial.log` or runner artifacts; do not hide failures by disabling
assertions or returning unsupported-operation success. GDB can load
`alos.elf` and connect with `target remote localhost:1234`.

## Chromium port boundaries

Keep Chromium/LLVM source checkouts and large build outputs **outside this
repository**. Store ALOS integration scripts/patches under `ports/chromium/`.
Keep `docs/chromium-port.md` synchronized with observed results.

The LLVM 18.1.8 bootstrap has produced target `libc++.a`/`libc++abi.a`.
The current profile enables localization, Unicode and C++ wide characters,
but disables exceptions/RTTI, filesystem, timezone database and random_device.
The runtime/wide-format suites and three consecutive `complex-cpp-test`
runs passed on both QEMU `qemu64` and `max` after rebuilding/linking.
swprintf/vswprintf use a musl-derived wchar_t sink; wide stream I/O uses
UTF-8 and one-wide-character pushback, not general orientation/fwide.
Console and writable-file wide output are supported; read-only writes fail
with EBADF. Native lseek, dup/dup2, partial fcntl, directory streams and
stat/lstat/fstat exist. fopen/fdopen support r/w/a, update and binary modes;
fseek/ftell account for byte pushback, but pending wide pushback makes
ftell/SEEK_CUR unsupported. pread/pwrite preserve the shared cursor and
ftruncate/ftruncate64 extend sparsely or reclaim blocks on shrink.
VFS offsets remain 32-bit bounded; advisory F_SETLK/F_GETLK/F_SETLKW
return ENOTSUP, not a pretend successful lock.
O_CREAT/O_EXCL/O_TRUNC/O_SYNC and fsync are implemented with Ext2 writes
and ATA cache flush. Anonymous pipes block and report EPIPE without SIGPIPE.
Temporary files/directories use exclusive PID/counter names, not random
names. access supports F_OK only; path-at APIs, full permission enforcement
and symlink following are unsupported. Process environment inheritance/
mutation and uname have native coverage; _exit remains thread-only, so
process-group termination tests explicitly use SYS_EXIT_GROUP.
Earlier
passes without localization remain a separate baseline. This is not a browser port.
The experimental Chromium 140.0.7339.80 patch introduces `OS_ALOS`/`is_alos`
and GN toolchain wiring. GN generates a graph and Ninja compiles real
`//base`/dependency objects, but the complete target has not been built,
linked or executed; Chromium `//base`, Blink and V8 are not established
working ALOS targets.
Do not claim working Ozone, Mojo or rendered Chromium content without evidence.

```powershell
.\ports\chromium\scripts\build-libcxx.ps1 -LLVMDirectory C:\external\llvm-project -OutputDirectory C:\external\alos-libcxx-build
.\ports\chromium\scripts\build-complex.ps1 -LibcxxBuildDirectory C:\external\alos-libcxx-build
.\ports\chromium\scripts\test-vm.ps1 -SkipBuild -Runtime -ComplexCpp -Cpu qemu64
```

The provisional C++ profile disables exceptions/RTTI and several library
features; consult the port document/scripts before expanding it. Imported
musl routines require their MIT notices (`ports/chromium/MUSL-LICENSE` and
`src/userland/libc/COPYRIGHT.musl`).

## Further references

- `README.md`: general overview and user workflows.
- `docs/chromium-port.md`: detailed native runtime and port status.
- `docs/GUI.md`, `docs/GUI-API.md`: desktop architecture and client API.
- `docs/MMIO-ARCHITECTURE.md`, `docs/DRIVERS.md`: hardware abstractions.
- `MULTIPROCESSING-OSDEV.org`, `OPTIMIZATIONS.md`: development notes.
- OSDev Wiki, Intel x86-64 manuals, Limine protocol and Ext2 specification:
  background references; verify behavior against the implementation.

Last updated: 2026-09-30.
