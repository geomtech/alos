---
trigger: always_on
---

Ne jamais accéder à du MMIO via HHDM → ça plantera toujours.
Ne jamais croire que “tout ce qui est physique est dans le HHDM”
Ne jamais lire la BAR phys addr directement comme un pointeur → c’est une simple adresse physique, rien d’autre.