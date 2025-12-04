Plan de Migration vers x86-64 pour ALOS

La migration de votre OS vers l'architecture x86-64 (Long Mode) avec UEFI représente une refonte majeure qui vous permettra d'accéder à plus de 4 Go de RAM, aux registres 64 bits, et aux instructions modernes. Voici un plan structuré pour cette transition.​

Critique : Ajouter -mno-red-zone dans les flags pour désactiver la red zone (zone de 128 octets sous RSP utilisée par l'ABI System V) incompatible avec les interruptions kernel​

Modifications du Makefile :

CC = x86_64-elf-gcc
Ajouter flags : -m64 -march=x86-64 -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2

Phase 2 : Migration du Bootloader vers Limine
Remplacez votre boot.s Multiboot1 par le protocole Limine, qui gère nativement le Long Mode, le paging 64 bits, et supporte BIOS + UEFI.​

Avantages :

Limine vous place directement en 64-bit avec paging activé​
Plus besoin de boot.s : votre kmain() devient le point d'entrée direct​
Supporte le higher half kernel à 0xFFFFFFFF80000000​

Étapes :

Limine se trouve dans le dossier limine-10.4.0
Créer un fichier limine.conf avec configuration de boot
Supprimer src/arch/x86/boot.s
Modifier le linker script pour placer le kernel à 0xFFFFFFFF80000000​
Ajouter les requêtes Limine dans une section .limine_requests​

Phase 3 : Refonte du Système de Mémoire

Le paging x86-64 utilise 4 niveaux (PML4 → PDPT → PD → PT) au lieu de 2 en 32 bits.​

Modifications dans mm/vmm.c :
Implémenter les structures PML4, PDPT, PD, PT (chacune avec 512 entrées de 8 octets)
Chaque entrée couvre : PML4 = 512 Go, PDPT = 1 Go, PD = 2 Mo, PT = 4 Ko​
Limine fournit une carte mémoire initiale, mais vous devrez gérer le mapping dynamique
Attention : Les adresses ne tiennent plus dans uint32_t - utiliser uintptr_t ou uint64_t partout
Higher Half Direct Mapping (HHDM) :
Mapper toute la mémoire physique à un offset fixe (ex: 0xFFFF800000000000)
Permet un accès direct : phys_addr + HHDM_OFFSET = virt_addr

Phase 4 : Interruptions et Exceptions

L'IDT en 64 bits utilise des descripteurs de 16 octets au lieu de 8.​
Modifications dans arch/x86/interrupts.s et kernel/idt.c :
Restructurer les descripteurs IDT (format différent avec IST, etc.)
Réécrire tous les stubs d'interruption en ASM 64 bits
Les handlers reçoivent maintenant la stack frame en registres selon System V ABI​

Convention d'appel System V AMD64 :

Arguments : rdi, rsi, rdx, rcx, r8, r9 (plus de passage par stack pour les 6 premiers)​
Valeur de retour : rax
Registres préservés : rbx, rbp, r12-r15​

Phase 5 : Syscalls et User Mode

Modifications dans arch/x86/usermode.c et kernel/syscalls.c :
La GDT est simplifiée en Long Mode (segmentation quasi-inexistante) mais reste nécessaire pour les transitions Ring 0/3​
Utiliser l'instruction syscall/sysret au lieu de int 0x80 pour de meilleures performances
Configurer les MSR (Model-Specific Registers) : STAR, LSTAR, SFMASK

Phase 6 : Drivers et Code Assembleur

Tous les fichiers .s doivent être réécrits :
arch/x86/switch.s (context switch) : utiliser registres 64 bits (rax, rbx, etc.)
Les structures de contexte doublent de taille (registres 8 octets)
Dans les drivers réseau et PCI, remplacer les casts d'adresses 32 bits

Phase 7 : Bibliothèque Standard

Modifications dans lib/ :
int reste 32 bits, mais long et pointeurs deviennent 64 bits
Utiliser <stdint.h> systématiquement : uint64_t, uintptr_t, size_t
Attention aux printf : le format %p fonctionne, mais %x tronque les adresses

Ordre de Migration Recommandé
Semaine 1 : Adapter Makefile
Semaine 2 : Intégrer Limine, tester boot basique avec kmain() minimal
Semaine 3-4 : Refondre VMM (paging 4 niveaux), tester allocation mémoire
Semaine 5 : Réécrire IDT/interruptions, tester timer et keyboard
Semaine 6 : Porter les drivers (ATA, PCI, réseau)
Semaine 7 : Adapter syscalls et user mode
Semaine 8 : Tests d'intégration (VFS, réseau, shell, userland)

Points Critiques
Ne jamais mélanger code 32/64 bits dans la même build, donc n'avoir que du code 64 bits
Le paging est obligatoire en Long Mode (pas optionnel)​