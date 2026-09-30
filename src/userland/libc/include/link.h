/* ElfW designe l'ABI statique ALOS, sans API de chargeur dynamique. */
#ifndef _ALOS_LINK_H
#define _ALOS_LINK_H

#include <elf.h>

#if !defined(__x86_64__) || __SIZEOF_POINTER__ != 8
#error "ALOS link.h requires the x86-64 ELF ABI"
#endif

#define ElfW(type) Elf64_##type

#endif
