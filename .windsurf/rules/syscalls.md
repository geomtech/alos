---
trigger: always_on
---

Ne jamais mettre à jour TSS.RSP0 quand on switch vers un thread user déjà dans le kernel
Ne pas faire cpu_sti() avant le retour complet en user mode - laisser iretq restaurer RFLAGS
La couche réseau (ethernet → ipv4 → tcp) est appelée depuis un IRQ : pas de mutex/condvar, utiliser spinlock_irqsave et wait_queue_wake_*