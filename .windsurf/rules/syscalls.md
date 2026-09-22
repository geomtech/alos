---
trigger: always_on
---

Toujours mettre TSS.RSP0 sur la kernel stack (rsp0) du thread user cible avant qu'il puisse retourner en Ring 3 ; modifier TSS.RSP0 ne modifie pas le RSP courant
Ne pas faire cpu_sti() avant le retour complet en user mode - laisser iretq restaurer RFLAGS
La couche réseau (ethernet → ipv4 → tcp) est appelée depuis un IRQ : pas de mutex/condvar, utiliser spinlock_irqsave et wait_queue_wake_*
En x86-64 long mode, conserver un frame IRETQ complet SS/RSP/RFLAGS/CS/RIP pour les switches coopératifs ; supprimer RSP/SS provoque un #GP au premier yield
