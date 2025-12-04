# ALOS Drivers Architecture

This document explains how the ALOS kernel talks to hardware devices, with a focus on:

- The generic MMIO subsystem
- The PCI/MMIO bridge
- The PCNet network driver
- The VirtIO network driver
  - PCI Legacy transport (PIO)
  - PCI Modern transport (MMIO)
  - Platform MMIO transport (virtio-mmio)

The goal is to give a **simple mental model** with **enough technical detail** so you can extend or debug the drivers with confidence.

---

## 1. Basic concepts

### 1.1 PIO vs MMIO

There are two classic ways to talk to devices on x86:

- **PIO (Port I/O)**
  - Uses special CPU instructions: `in`, `out`, `inw`, `outw`, `inl`, `outl`.
  - Device registers live in a separate I/O space (0x0000–0xFFFF).
  - Typical for old PCI devices (e.g., PCNet, VirtIO PCI Legacy).

- **MMIO (Memory-Mapped I/O)**
  - Device registers look like normal memory.
  - You access them with loads/stores at special physical addresses.
  - Those physical ranges are *not* normal RAM; they are mapped to the device.
  - Needs proper page table attributes (uncached, non-executable, etc.).

In ALOS we now favor **MMIO** for new drivers because:

- It is the natural model on modern architectures.
- It works well with VirtIO 1.0+ and most modern PCI devices.
- It avoids sprinkling `in/out` instructions everywhere.

PIO is still supported where the hardware requires it (PCNet and VirtIO PCI Legacy fallback).

---

## 2. MMIO Subsystem

### 2.1 Files

- `src/kernel/mmio/mmio.h`
- `src/kernel/mmio/mmio.c`

These files implement a small MMIO framework.

### 2.2 Responsibilities

- Maintain a **virtual address window** reserved for MMIO mappings.
- Provide a simple `ioremap`/`iounmap` API:
  - `void* ioremap(uint32_t phys_addr, uint32_t size);`
  - `void  iounmap(void *virt_addr, uint32_t size);`
- Provide **typed access helpers** with the right memory barriers:
  - `mmio_read8_off(base, offset)` / `mmio_write8_off(base, offset, value)`
  - Same for 16-bit and 32-bit.
- Track MMIO regions and avoid overlapping mappings.

### 2.3 Mapping policy

- MMIO regions are mapped into a fixed virtual window (configured in `mmio_init`).
- Pages are mapped **uncached** to avoid the CPU caching device registers.
- Each `ioremap` returns a `volatile` pointer that the driver uses for all accesses.

If you write a new MMIO driver, **never access physical addresses directly**.
Always go through `ioremap` and the MMIO helpers.

---

## 3. PCI + MMIO bridge

### 3.1 Files

- `src/drivers/pci.h`
- `src/drivers/pci.c`
- `src/kernel/mmio/pci_mmio.h`
- `src/kernel/mmio/pci_mmio.c`

### 3.2 PCI basics

For each PCI device, the kernel reads its **BARs** (Base Address Registers):

- Each BAR describes either:
  - An **I/O** range (PIO): `bar_value & 1 != 0`
  - Or a **memory** range (MMIO): `bar_value & 1 == 0`
- Bits 31:4 contain the base address.
- The driver uses this information to decide how to talk to the device.

### 3.3 `pci_mmio.c`

This file provides helper functions to:

- Parse all BARs of a device (`pci_parse_bars`).
- Detect which BARs are MMIO vs PIO.
- Compute BAR **size** by the standard probe trick (write `0xFFFFFFFF`, read back, restore).
- Map a BAR using the MMIO framework (`pci_map_bar`).

All network drivers use this logic to discover whether they can use MMIO or must fall back to PIO.

---

## 4. PCNet Driver

### 4.1 Files

- `src/drivers/net/pcnet.h`
- `src/drivers/net/pcnet.c`

PCNet is a classic AMD network card, widely emulated by QEMU.
Historically it was driven purely via **PIO**. We refactored it to support both **PIO** and **MMIO** transparently.

### 4.2 Access modes

The driver now has an `access_mode` field and two possible access paths:

- **PIO mode**
  - Access registers with `inw/outw` at the I/O base address.
- **MMIO mode**
  - Use `ioremap` on the MMIO BAR and access registers through memory reads/writes.

Helper functions dispatch based on `access_mode`:

- `pcnet_read_csr()` / `pcnet_write_csr()` call either the PIO or MMIO versions.

### 4.3 Initialization logic

During `pcnet_init(PCIDevice *pci_dev)`:

1. Parse BARs with `pci_parse_bars`.
2. If a **MMIO BAR** is available and valid:
   - Map it with `pci_map_bar` → returns a `mmio_base` pointer.
   - Set `access_mode = MMIO`.
3. Else if a PIO BAR exists:
   - Use the PIO base (`io_base`) and keep `access_mode = PIO`.
4. Initialize descriptor rings, enable interrupts, etc.

From the rest of the driver’s point of view, **the difference between PIO and MMIO is hidden** behind the read/write helpers.

---

## 5. VirtIO Overview

VirtIO is a **paravirtualized** device standard. In ALOS we focus on VirtIO-Net (virtual NIC).

We support three transports:

1. **PCI Legacy (Transitional)**
   - Registers in I/O space (PIO).
   - Simple layout at fixed offsets.
2. **PCI Modern (VirtIO 1.0+ over PCI)**
   - Registers exposed via **PCI capabilities** and BARs MMIO.
   - Uses a `common_cfg` structure, notification area, ISR status area, and device config.
3. **Platform MMIO (virtio-mmio)**
   - Non-PCI device with all registers in a single MMIO range.
   - Used on embedded platforms or special QEMU machine types.

### 5.1 Files

- `src/drivers/net/virtio_net.h`
- `src/drivers/net/virtio_net.c`
- `src/drivers/virtio/virtio_transport.h`
- `src/drivers/virtio/virtio_transport.c`
- `src/drivers/virtio/virtio_mmio.h`
- `src/drivers/virtio/virtio_mmio.c`
- `src/drivers/virtio/virtio_pci_modern.h`
- `src/drivers/virtio/virtio_pci_modern.c`

---

## 6. VirtIO Transport Abstraction

The key idea: **separate the VirtIO core logic from the underlying transport**.

### 6.1 `VirtioDevice` and `VirtQueue`

`virtio_transport.h` defines:

- `VirtioDevice`
  - `transport_type`:
    - `VIRTIO_TRANSPORT_PCI_LEGACY`
    - `VIRTIO_TRANSPORT_PCI_MODERN`
    - `VIRTIO_TRANSPORT_MMIO`
  - `ops`: pointer to a `VirtioTransportOps` vtable.
  - Common fields: `device_id`, `vendor_id`, `irq`, `initialized`.
  - A union `transport` with:
    - PCI-specific data (BARs, MMIO pointers for Modern).
    - MMIO-specific data (base address, version, etc.).

- `VirtQueue`
  - Holds the Descriptor Table, Available Ring, Used Ring, and associated metadata.
  - Allocated as one physically contiguous block and shared with the device.

### 6.2 Operations (`VirtioTransportOps`)

Each transport implements the same set of operations:

- Basic register access: `read8/16/32`, `write8/16/32`.
- Feature negotiation: `get_features`, `set_features`.
- Status handling: `get_status`, `set_status`, `reset`.
- Queue setup and notification: `setup_queue`, `notify_queue`.
- Device-specific config reads: `read_config8/16/32`.
- Interrupt handling: `ack_interrupt`.

The VirtIO-Net driver uses this **generic API** and does not care whether it is talking to:

- PCI Legacy via PIO,
- PCI Modern via MMIO, or
- virtio-mmio via platform MMIO.

---

## 7. VirtIO PCI Legacy (PIO)

### 7.1 Layout

Legacy VirtIO PCI devices (transitional) expose a simple header in **I/O space**:

- `0x00` : `DeviceFeatures`
- `0x04` : `GuestFeatures`
- `0x08` : `QueueAddress` (PFN-based)
- `0x0C` : `QueueSize`
- `0x0E` : `QueueSelect`
- `0x10` : `QueueNotify`
- `0x12` : `DeviceStatus`
- `0x13` : `ISRStatus`
- `0x14+`: Device-specific config (e.g. MAC address for VirtIO-Net).

### 7.2 Implementation

In `virtio_transport.c`:

- The **PIO backend** (`pci_pio_ops`) uses `in*/out*` instructions.
- Queues are set up using the legacy PFN-based `QueueAddress` register.
- This is used as a **fallback** when Modern capabilities are not available or MMIO mapping fails.

The old VirtIO-Net driver used this path exclusively; now it can transparently use the Modern path when available.

---

## 8. VirtIO PCI Modern (MMIO)

### 8.1 Capabilities

Modern VirtIO over PCI is capability-based:

- The device exposes several **PCI vendor-specific capabilities** (cap_id = 0x09):
  - `VIRTIO_PCI_CAP_COMMON_CFG` : common configuration.
  - `VIRTIO_PCI_CAP_NOTIFY_CFG` : notification area.
  - `VIRTIO_PCI_CAP_ISR_CFG`   : ISR status register.
  - `VIRTIO_PCI_CAP_DEVICE_CFG`: device-specific config (e.g. MAC address).

Each capability tells us:

- Which BAR (`bar`)
- The offset within that BAR
- The length of the structure

`virtio_pci_modern.c` parses this list using `pci_config_read_byte/dword`.

### 8.2 BAR mapping

Once the capabilities are parsed, we:

1. For each BAR referenced by a capability:
   - Use the standard BAR-size probe to find the size.
   - Map the entire BAR with `ioremap`.
2. Compute pointers to:
   - `common_cfg` (Common configuration structure)
   - `notify_base` (base of the notification area)
   - `isr` (ISR status byte)
   - `device_cfg` (MAC, etc.)

These pointers are stored in `VirtioDevice.transport.pci` and used by `pci_modern_ops`.

### 8.3 Queue setup (Modern)

Using `common_cfg` and MMIO helpers:

1. Select queue: write to `queue_select`.
2. Read `queue_size` to know maximum supported entries.
3. Allocate and zero the virtqueue memory (descriptor + rings).
4. Write 64-bit physical addresses for:
   - `queue_desc` (Descriptor Table)
   - `queue_avail` (Available Ring)
   - `queue_used` (Used Ring)
5. Enable the queue by writing `queue_enable = 1`.

All these fields are part of `virtio_pci_common_cfg_t` in `virtio_pci_modern.h`.

### 8.4 Notifications and interrupts

- To notify a queue:
  - Read `queue_notify_off` from `common_cfg` for the selected queue.
  - Compute `notify_addr = notify_base + queue_notify_off * notify_off_multiplier`.
  - Write the queue index at `notify_addr`.

- To handle interrupts:
  - Read from `isr` (MMIO). Reading clears the bits.
  - Bits indicate whether it is a queue interrupt or a config change.

### 8.5 Feature negotiation and status

Using `common_cfg` MMIO:

- Feature negotiation uses `device_feature_select/device_feature` and
  `driver_feature_select/driver_feature`.
- Status flags (ACKNOWLEDGE, DRIVER, FEATURES_OK, DRIVER_OK) are managed through
  the `device_status` field.

The high-level logic in `virtio_init_device` and `virtio_finalize_init` is the same for all transports; only the underlying accessors change.

---

## 9. VirtIO MMIO (Platform Device)

### 9.1 Files

- `src/drivers/virtio/virtio_mmio.h`
- `src/drivers/virtio/virtio_mmio.c`

This is the **non-PCI** VirtIO MMIO transport, as described in the VirtIO 1.0 spec (section 4.2, `virtio-mmio`).

- A fixed MMIO region per device
- Register layout:
  - `MagicValue`, `Version`, `DeviceID`, `VendorID`
  - Feature registers: `DeviceFeatures`, `DeviceFeaturesSel`, `DriverFeatures`, ...
  - Queue selection and addresses (separate low/high 32-bit words)
  - `Status`, `InterruptStatus`, `InterruptACK`, `ConfigGeneration`, and device-specific config.

`virtio_mmio.c` provides:

- `VirtioMmioDevice *virtio_mmio_probe(phys, size, irq);`
- `int virtio_mmio_init_device(...)`
- `int virtio_mmio_setup_queue(...)`
- `uint32_t virtio_mmio_ack_interrupt(...)`

In practice, this is used on platforms where devices are described by a device tree or static configuration, not by PCI enumeration.

---

## 10. VirtIO-Net Driver

### 10.1 Files

- `src/drivers/net/virtio_net.h`
- `src/drivers/net/virtio_net.c`

The network driver is written **on top of the transport abstraction**.

High-level flow in `virtio_net_init(PCIDevice *pci_dev)`:

1. Create a `VirtioDevice` from the PCI device:
   - `VirtioDevice *vdev = virtio_create_from_pci(pci_dev);`
   - This automatically chooses PCI Legacy (PIO) or PCI Modern (MMIO).
2. Enable PCI bus mastering.
3. Initialize the VirtIO device (feature negotiation, status flags).
4. Set up RX and TX queues through `virtio_setup_queue`.
5. Read the MAC address from the device-specific config.
6. Configure interrupts and/or polling.
7. Register a `NetInterface` (`ethX`) with the network stack.

Sending and receiving packets is independent of the underlying transport:

- RX path:
  - Refill RX queue with buffers.
  - On interrupt or poll, read Used Ring and hand Ethernet frames to the L2 layer.

- TX path:
  - Build a VirtIO-Net header + packet data in a buffer.
  - Add it to the TX VirtQueue.
  - Notify the device.

The driver uses `VirtQueue` helpers, so it does not know if the device is PCI Legacy, PCI Modern, or MMIO-only.

---

## 11. Adding a new MMIO-friendly driver

To write a new driver that benefits from the MMIO work, the recommended steps are:

1. **Discover the device**
   - For PCI: extend the PCI scan logic and match `vendor_id`/`device_id`.
   - For platform MMIO: add a static table or device tree entry with phys address/size/IRQ.

2. **Map registers**
   - For PCI MMIO BARs: use `pci_mmio.c` helpers + `ioremap`.
   - For platform MMIO: call `ioremap` on the given phys range.

3. **Encapsulate access**
   - Write small inline helpers like `dev_read32(offset)`/`dev_write32(offset, value)`.
   - Internally they should use `mmio_read32_off` / `mmio_write32_off`.

4. **Separate transport from logic**
   - If applicable (e.g., VirtIO-like design), put transport-specific code in a separate module
     (`*_transport.c`) and keep the core driver transport-agnostic.

5. **Handle interrupts and polling**
   - Provide an IRQ handler that reads a status/ISR register and dispatches to queues.
   - Optionally support polling for early bring-up or debugging.

6. **Integrate with the network stack** (for NICs)
   - Create a `NetInterface` instance.
   - Implement a `send` callback and hook into `netdev_register()`.

---

## 12. Testing

### 12.1 QEMU PCNet (PIO + MMIO)

The Makefile already provides a `run` target that starts QEMU with PCNet.
PCNet can use PIO or MMIO depending on how the BARs are configured.

### 12.2 QEMU VirtIO-Net (PCI Legacy + Modern MMIO)

- `virtio_net.c` + `virtio_transport.c` automatically:
  - Detect VirtIO capabilities.
  - Map BARs for Modern MMIO if present.
  - Fall back to Legacy PIO otherwise.

You can check which transport is used by looking at the kernel log:

- `*** Using PCI Modern MMIO transport ***`
- or `Using PIO transport (VirtIO PCI Legacy)`.

### 12.3 Debugging tips

- Use the kernel log (`KLOG_INFO`, `KLOG_ERROR`) instead of VGA-only logs,
  so you can capture output via QEMU `-serial`.
- Enable `-d int` or other QEMU debug options if you suspect interrupt issues.
- Always verify that:
  - PCI Bus Mastering is enabled for DMA-based devices.
  - MMIO ranges do not overlap and are mapped uncached.
  - Virtqueues are properly aligned and sized according to the spec.

---

This document, together with `docs/MMIO-ARCHITECTURE.md`, should give you a
clear understanding of how drivers in ALOS use **PIO**, **MMIO**, **PCI BARs**,
**VirtQueues**, and **VirtIO** transports (Legacy, Modern, and MMIO-only).
