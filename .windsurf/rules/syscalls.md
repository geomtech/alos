---
trigger: always_on
---

Toujours mettre TSS.RSP0 sur la kernel stack (rsp0) du thread user cible avant qu'il puisse retourner en Ring 3 ; modifier TSS.RSP0 ne modifie pas le RSP courant
Ne pas faire cpu_sti() avant le retour complet en user mode - laisser iretq restaurer RFLAGS
La couche réseau (ethernet → ipv4 → tcp) est appelée depuis un IRQ : pas de mutex/condvar, utiliser spinlock_irqsave et wait_queue_wake_*
Lors d'un switch coopératif Ring 0 -> Ring 0, le frame IRETQ contient RIP/CS/RFLAGS uniquement ; RSP/SS ne sont présents que lors d'un changement de privilège
