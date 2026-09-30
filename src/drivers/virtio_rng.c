/* VirtIO RNG legacy PCI, UP uniquement. Le backend hote est une frontiere
 * de confiance; ce pilote ne peut pas certifier son entropie. */
#include "virtio_rng.h"
#include "pci.h"
#include "virtio/virtio_transport.h"
#include "../arch/x86_64/io.h"
#include "../mm/pmm.h"
#include "../kernel/thread.h"
#include "../kernel/timer.h"
#include "../kernel/klog.h"
#include "../include/errno.h"
#include "../include/entropy_abi.h"
#include "../include/string.h"

#define RNG_TIMEOUT_MS 10000
#define RNG_QUEUE_PAGES 3
#define RNG_QUEUE_MAX 256

static struct {
    uint16_t port, size, submitted, completed;
    uint8_t irq;
    int state; /* 0 absent, 1 actif, negatif: erreur permanente */
    VirtqDesc *desc;
    volatile VirtqAvail *avail;
    volatile VirtqUsed *used;
    volatile uint8_t *bytes;
    unsigned remaining, offset;
    int pending;
    wait_queue_t waiters;
} rng;

static uint64_t irq_disable(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint64_t flags) {
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

static void barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static void unmask_irq_locked(void) {
    if (rng.irq >= 8) {
        outb(0xa1, inb(0xa1) & (uint8_t)~(1u << (rng.irq - 8)));
        outb(0x21, inb(0x21) & (uint8_t)~(1u << 2));
    } else {
        outb(0x21, inb(0x21) & (uint8_t)~(1u << rng.irq));
    }
}

static void fail_locked(int error) {
    KLOG_ERROR_DEC("RNG", "Failure errno: ", (uint32_t)-error);
    rng.state = error;
    outb(rng.port + 18, 0);
    /* Le reset peut echouer: ne jamais liberer/reutiliser les pages DMA.
     * Elles restent epinglees jusqu'au reboot, meme apres timeout. */
    rng.pending = 0;
    rng.remaining = 0;
    wait_queue_wake_all(&rng.waiters);
}

static void submit_locked(void) {
    rng.offset = 0;
    rng.avail->ring[rng.submitted % rng.size] = 0;
    barrier();
    rng.pending = 1;
    rng.submitted++;
    rng.avail->idx = rng.submitted;
    barrier();
    outw(rng.port + 16, 0);
}

int virtio_rng_init(void) {
    uint64_t flags = irq_disable();
    if (rng.state || rng.port) {
        int result = rng.state == 1 ? 0 : rng.state;
        irq_restore(flags);
        return result;
    }
    PCIDevice *pci = pci_get_device(0x1af4, 0x1005);
    if (!pci) {
        irq_restore(flags);
        return -ENOSYS;
    }
    /* Le device transitional doit annoncer le subsystem entropy (4).
     * Les devices modern-only 0x1044 ne sont pas pris en charge. */
    uint16_t subsystem = pci_config_read_word(pci->bus, pci->slot, pci->func, 0x2e);
    KLOG_INFO_HEX("RNG", "PCI device: ", pci->device_id);
    KLOG_INFO_HEX("RNG", "PCI subsystem: ", subsystem);
    KLOG_INFO_HEX("RNG", "PCI BAR0: ", pci->bar0);
    KLOG_INFO_DEC("RNG", "PCI IRQ: ", pci->interrupt_line);
    if (subsystem != VIRTIO_DEVICE_ENTROPY ||
        !(pci->bar0 & 1) || (pci->bar0 & ~3u) > 0xffe0 ||
        !(pci->bar0 & ~3u) || pci->interrupt_line < 3 ||
        pci->interrupt_line > 15) {
        rng.state = -ENOTSUP;
        KLOG_ERROR("RNG", "Unsupported subsystem, PIO BAR or PIC IRQ");
        irq_restore(flags);
        return rng.state;
    }
    rng.port = pci->bar0 & ~3u;
    rng.irq = pci->interrupt_line;
    wait_queue_init(&rng.waiters);
    uint32_t command = pci_config_read_dword(pci->bus, pci->slot, pci->func, 4);
    /* Ne pas reecrire les bits W1C du PCI status. Activer PIO/DMA et INTx. */
    pci_config_write_dword(pci->bus, pci->slot, pci->func, 4,
                          (command & 0xffffu & ~(1u << 10)) | 5u);
    outb(rng.port + 18, 0);
    if (inb(rng.port + 18) != 0) {
        KLOG_ERROR("RNG", "Device reset did not clear status");
        fail_locked(-EIO);
        irq_restore(flags);
        return -EIO;
    }
    outb(rng.port + 18, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    /* Aucune feature specifique RNG; pas de EVENT_IDX ni INDIRECT_DESC. */
    outl(rng.port + 4, 0);
    outw(rng.port + 14, 0);
    rng.size = inw(rng.port + 12);
    KLOG_INFO_DEC("RNG", "Queue size: ", rng.size);
    if (!rng.size || rng.size > RNG_QUEUE_MAX ||
        (rng.size & (rng.size - 1)) || inl(rng.port + 8)) {
        KLOG_ERROR("RNG", "Unsupported queue size or nonzero reset PFN");
        fail_locked(-ENOTSUP);
        irq_restore(flags);
        return -ENOTSUP;
    }
    /* Le PMM reel retourne des pointeurs HHDM (contrairement au commentaire
     * historique de pmm.h). Les frees prennent ces memes pointeurs. */
    uint8_t *queue = pmm_alloc_blocks(RNG_QUEUE_PAGES);
    void *buffer = pmm_alloc_block();
    uint64_t queue_phys = queue ? pmm_virt_to_phys(queue) : 0;
    uint64_t buffer_phys = buffer ? pmm_virt_to_phys(buffer) : 0;
    if (!queue || !buffer || !queue_phys || !buffer_phys ||
        (queue_phys >> 44) || (queue_phys & 4095) || (buffer_phys & 4095)) {
        if (queue) pmm_free_blocks(queue, RNG_QUEUE_PAGES);
        if (buffer) pmm_free_block(buffer);
        KLOG_ERROR("RNG", "DMA allocation or physical alignment/PFN range failed");
        fail_locked(-ENOMEM);
        irq_restore(flags);
        return -ENOMEM;
    }
    KLOG_INFO_HEX64("RNG", "Queue physical: ", queue_phys);
    KLOG_INFO_HEX64("RNG", "Buffer physical: ", buffer_phys);
    rng.bytes = buffer;
    memset(queue, 0, RNG_QUEUE_PAGES * 4096);
    memset((void *)rng.bytes, 0, 4096);
    rng.desc = (VirtqDesc *)queue;
    rng.avail = (VirtqAvail *)(queue + sizeof(VirtqDesc) * rng.size);
    size_t used_offset = ((size_t)((uint8_t *)rng.avail - queue) +
                          6 + 2 * rng.size + 4095) & ~(size_t)4095;
    rng.used = (VirtqUsed *)(queue + used_offset);
    rng.desc[0].addr = buffer_phys;
    rng.desc[0].len = ALOS_GETENTROPY_MAX;
    rng.desc[0].flags = VIRTQ_DESC_F_WRITE;
    barrier();
    outl(rng.port + 8, queue_phys >> 12);
    outb(rng.port + 18, VIRTIO_STATUS_ACKNOWLEDGE |
                         VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    if (inb(rng.port + 18) != (VIRTIO_STATUS_ACKNOWLEDGE |
                              VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK)) {
        KLOG_ERROR_HEX("RNG", "DRIVER_OK status rejected: ", inb(rng.port + 18));
        fail_locked(-EIO);
        irq_restore(flags);
        return -EIO;
    }
    rng.state = 1;
    unmask_irq_locked();
    submit_locked();
    KLOG_INFO("RNG", "Legacy PCI RNG initialized (trusted host backend required)");
    irq_restore(flags);
    return 0;
}

void virtio_rng_handle_irq(uint8_t irq) {
    if (rng.state != 1 || irq != rng.irq) return;
    uint64_t flags = irq_disable();
    uint8_t isr = inb(rng.port + 19);
    if (inb(rng.port + 18) & (VIRTIO_STATUS_DEVICE_NEEDS_RESET |
                              VIRTIO_STATUS_FAILED)) {
        fail_locked(-EIO);
    } else if (isr & 1) {
        barrier();
        uint16_t produced = rng.used->idx;
        if (produced != rng.completed) {
            if (!rng.pending || (uint16_t)(produced - rng.completed) != 1) {
                fail_locked(-EIO);
            } else {
                uint32_t id = rng.used->ring[rng.completed % rng.size].id;
                uint32_t length = rng.used->ring[rng.completed % rng.size].len;
                if (id != 0 || !length || length > ALOS_GETENTROPY_MAX) {
                    fail_locked(-EIO);
                } else {
                    barrier();
                    rng.completed = produced;
                    rng.pending = 0;
                    rng.remaining = length;
                    rng.offset = 0;
                    wait_queue_wake_all(&rng.waiters);
                }
            }
        }
    }
    irq_restore(flags);
}

void virtio_rng_handle_shared_irq(void) {
    virtio_rng_handle_irq(rng.irq);
}

static bool readable(void *unused) {
    (void)unused;
    return rng.state != 1 || rng.remaining != 0;
}

int virtio_rng_read(void *buffer, size_t length) {
    if (!length) return 0;
    if (!buffer || length > ALOS_GETENTROPY_MAX) return -EINVAL;
    thread_t *current = thread_current();
    if (!current) return -EIO;
    uint64_t flags = irq_disable();
    uint64_t deadline = timer_get_ticks() + RNG_TIMEOUT_MS;
    size_t copied = 0;
    int result = 0;
    while (copied < length) {
        if (current->should_terminate) {
            result = -EINTR;
            break;
        }
        if (rng.state != 1) {
            result = rng.state ? rng.state : -ENOSYS;
            break;
        }
        if (rng.remaining) {
            size_t take = length - copied;
            if (take > rng.remaining) take = rng.remaining;
            for (size_t i = 0; i < take; i++) {
                ((uint8_t *)buffer)[copied + i] = rng.bytes[rng.offset + i];
                rng.bytes[rng.offset + i] = 0;
            }
            rng.offset += take;
            rng.remaining -= take;
            copied += take;
            if (!rng.remaining) submit_locked();
            continue;
        }
        uint64_t now = timer_get_ticks();
        if (now >= deadline) {
            fail_locked(-EIO);
            result = -EIO;
            break;
        }
        /* Predicat + enqueue atomiques dans wait_queue_wait_timeout.
         * Aucun pointeur vers la pile du demandeur n'est garde par le pilote. */
        wait_queue_wait_timeout(&rng.waiters, readable, NULL,
                                (uint32_t)(deadline - now));
        if (current->waiting_queue == &rng.waiters)
            wait_queue_remove(&rng.waiters, current);
    }
    irq_restore(flags);
    return result;
}
