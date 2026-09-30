# Native getentropy: deployment and integration contract

The driver and syscall are integrated. Native QEMU `qemu64` regression runs
passed with and without the configured device: the former exercised concurrent
readers and API limits, the latter verified ENOSYS and unchanged destinations.
This is not an established Chromium cryptographic runtime. There is no CPU RNG fallback,
kernel PRNG, timing-based entropy, or statistical proof of source quality.

## Trusted source prerequisite

Use QEMU with a host-backed RNG:

```text
-object rng-builtin,id=alos_rng
-device virtio-rng-pci,rng=alos_rng,disable-modern=on
```

**Never use `-seed`, deterministic replay, or a fake RNG backend for security.**
QEMU's guest-random layer can otherwise use GLib's Mersenne Twister or replayed
bytes instead of fresh cryptographic randomness. These settings are invisible
to the guest; enforcing them is a deployment responsibility.

The installed Windows QEMU reports `11.1.0`,
`v11.1.0-12130-ge470268ff4`. Read-only PE inspection found its `gnutls_rnd`
import from `libgnutls-30.dll`. That DLL reports GnuTLS 3.8.13 and imports
`BCryptGenRandom` and `BCryptOpenAlgorithmProvider`, with diagnostic strings
matching its BCrypt backend. This is evidence of the intended source design,
**not authentication of these binaries or proof that they match upstream**.
The exact QEMU revision was unavailable from the upstream GitHub mirror.

Relevant upstream sources:

- [QEMU rng-builtin](https://github.com/qemu/qemu/blob/9c88811b81a935a172c5c873b2ada2d9b38a5f74/backends/rng-builtin.c)
  calls `qemu_guest_getrandom_nofail`.
- [QEMU guest-random](https://github.com/qemu/qemu/blob/9c88811b81a935a172c5c873b2ada2d9b38a5f74/util/guest-random.c)
  normally calls `qcrypto_random_bytes`; errors reach `error_fatal`.
- [QEMU GnuTLS backend](https://github.com/qemu/qemu/blob/9c88811b81a935a172c5c873b2ada2d9b38a5f74/crypto/random-gnutls.c)
  uses `gnutls_rnd(GNUTLS_RND_RANDOM)` and propagates errors.
- [GnuTLS 3.8.13 RNG](https://github.com/gnutls/gnutls/blob/3.8.13/lib/nettle/rnd.c)
  seeds/reseeds its ChaCha generator from system entropy; timestamps are
  reseed scheduling/nonce inputs, not replacements for the OS seed.
- [GnuTLS BCrypt backend](https://github.com/gnutls/gnutls/blob/3.8.13/lib/nettle/sysrng-bcrypt.c)
  opens `BCRYPT_RNG_ALGORITHM` and fails on unsuccessful BCrypt calls.
- [Microsoft BCrypt documentation](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom)
  documents the default provider's CTR_DRBG design.

Trust includes the host OS RNG, host administration, QEMU/GnuTLS binaries,
VM isolation, and a nondeterministic launch configuration. The guest checks
completion bounds and protocol state, not entropy content or host compromise.

## Integrated surfaces and deployment requirements

1. Add `src/drivers/virtio_rng.c` and `src/kernel/entropy.c` to kernel sources.
2. Include `entropy_abi.h` in the two syscall-number headers, or add matching
   `#define SYS_GETENTROPY 261`. Number 261 is registered in both syscall headers.
3. Include `entropy.h` in the dispatcher and add:

   ```c
   case SYS_GETENTROPY:
     result = entropy_getentropy((void *)regs->rdi, (size_t)regs->rsi);
     break;
   ```

4. Route **every matching PCI INTx IRQ**, including shared network IRQs, through
   `virtio_rng_handle_irq(irq)` before PIC EOI. Do not replace existing handlers.
   The generic IDT IRQ path and any installed specialized IRQ11 path must both
   preserve their existing interrupt/context layout.
   In the specialized ASM network stub (also installed on other PCI lines),
   declare `extern virtio_rng_handle_shared_irq` and call
   `virtio_rng_handle_shared_irq` after the existing network calls, before EOI.
   It delegates to the actual RNG line and checks device ISR internally; it
   does not assume that the stub's name identifies the current vector.
5. Once these hooks, PCI enumeration, PMM, wait queues, and **network device gate
   installation** are complete, call
   `virtio_rng_init()`. `-ENOSYS` means absent device; other errors must be logged.
   Do not mark RNG ready on failure. The driver unmasks its actual PIC line and
   slave cascade IRQ2, preserving other masks under UP interrupt exclusion.
6. Add the libc source if source discovery does not already include it. Declare
   `int getentropy(void *, size_t);` inside unistd.h's existing C linkage section.
   `sys/random.h` also declares it; `getrandom` is not implemented.
7. Wire `entropy-test` into userland/staging and run it with the real device;
   run `entropy-test --unavailable` in a separate boot without the device.

## Lifetime and API

Legacy PCI ID 1af4:1005 with entropy subsystem ID 4 and a PIO BAR is supported.
Modern-only devices and oversized/non-power-of-two queues are rejected. The
driver uses its own PMM-backed contiguous, page-aligned split ring: existing
generic buffer submission truncates virtual addresses and is not used for RNG.
All DMA addresses are physical.
The actual PMM implementation returns HHDM virtual allocation pointers;
the driver converts them with `pmm_virt_to_phys` for DMA and uses the original
pointers for CPU access/free (the historical pmm.h allocation comment differs).
One descriptor and a separate buffer stay
driver-owned and permanently pinned; no user or requester-stack address is
submitted to hardware. IRQ completion wakes a shared wait queue. Readers
consume disjoint bytes under UP interrupt exclusion and block, not busy-spin.
Exiting threads retain no DMA ownership or driver callback pointer.

Each call has a ten-second wait budget. Malformed completions, device failures,
or timeout reset the device and latch an error, waking waiters. DMA pages remain
pinned even if reset fails, preventing late DMA into reused memory. This costs
four pages until reboot and intentionally offers no hotplug/reinitialization.
No SMP safety is claimed.

Requests above 256 bytes fail with EIO; zero length succeeds without accessing
the pointer or source. Nonzero invalid/nonwritable buffers fail with EFAULT,
before consuming entropy, including demand-paged range checks. Valid requests
without a configured device fail with ENOSYS. Positive-length success means
exactly the requested bytes were obtained. Partial internal reads never become
partial user success; the destination is copied only after completion and
revalidated after sleep. Libc returns 0 or -1 with errno.

The host contract test covers size/pointer checks, unavailable/error propagation,
copy failure and no-copy-on-source-error using explicitly nonentropy fixtures.
The separate libc contract test verifies ABI 261 and 0/-1/errno translation.
Native tests cover boundaries, protected/cross-page destinations, unavailable
source, and concurrent readers. They do not prove cryptographic security.
Timeout, malformed completion and termination behavior additionally require
driver-level fault injection and native validation before production reliance.

Isolated host contract commands (inside the supplied Linux runtime):

```sh
gcc -std=gnu99 -O2 -Wall -Wextra -Werror \
  tests/entropy-contract-test.c src/kernel/entropy.c \
  -o tests/entropy-contract-test.host
tests/entropy-contract-test.host
gcc -std=gnu99 -O2 -Wall -Wextra -Werror -Isrc/userland/libc/src \
  tests/entropy-libc-contract-test.c src/userland/libc/src/sys/getentropy.c \
  -o tests/entropy-libc-contract-test.host
tests/entropy-libc-contract-test.host
rm tests/entropy-contract-test.host tests/entropy-libc-contract-test.host
```
