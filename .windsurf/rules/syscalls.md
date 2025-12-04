---
trigger: always_on
---

Ne jamais mettre à jour TSS.RSP0 quand on switch vers un thread user déjà dans le kernel
Ne pas faire cpu_sti() avant le retour complet en user mode - laisser iretq restaurer RFLAGS