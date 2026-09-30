/* Compile en C et C++ avec les headers libc, puis avec -DALOS_KERNEL_ELF_HEADER. */
#include <stddef.h>
#ifdef ALOS_KERNEL_ELF_HEADER
#include "../../../src/include/elf.h"
#else
#include <elf.h>
#include <link.h>
#include <sys/cdefs.h>
#endif

#ifdef __cplusplus
#define CHECK(condition) static_assert(condition, #condition)
#else
#define CHECK(condition) _Static_assert(condition, #condition)
#endif
#define OFFSET(type, field, expected) CHECK(offsetof(type, field) == expected)

CHECK(sizeof(Elf64_Ehdr) == 64);
CHECK(sizeof(Elf64_Phdr) == 56);
CHECK(sizeof(Elf64_Shdr) == 64);
OFFSET(Elf64_Ehdr, e_ident, 0);
OFFSET(Elf64_Ehdr, e_type, 16);
OFFSET(Elf64_Ehdr, e_machine, 18);
OFFSET(Elf64_Ehdr, e_version, 20);
OFFSET(Elf64_Ehdr, e_entry, 24);
OFFSET(Elf64_Ehdr, e_phoff, 32);
OFFSET(Elf64_Ehdr, e_shoff, 40);
OFFSET(Elf64_Ehdr, e_flags, 48);
OFFSET(Elf64_Ehdr, e_ehsize, 52);
OFFSET(Elf64_Ehdr, e_phentsize, 54);
OFFSET(Elf64_Ehdr, e_phnum, 56);
OFFSET(Elf64_Ehdr, e_shentsize, 58);
OFFSET(Elf64_Ehdr, e_shnum, 60);
OFFSET(Elf64_Ehdr, e_shstrndx, 62);
OFFSET(Elf64_Phdr, p_type, 0);
OFFSET(Elf64_Phdr, p_flags, 4);
OFFSET(Elf64_Phdr, p_offset, 8);
OFFSET(Elf64_Phdr, p_vaddr, 16);
OFFSET(Elf64_Phdr, p_paddr, 24);
OFFSET(Elf64_Phdr, p_filesz, 32);
OFFSET(Elf64_Phdr, p_memsz, 40);
OFFSET(Elf64_Phdr, p_align, 48);
OFFSET(Elf64_Shdr, sh_name, 0);
OFFSET(Elf64_Shdr, sh_type, 4);
OFFSET(Elf64_Shdr, sh_flags, 8);
OFFSET(Elf64_Shdr, sh_addr, 16);
OFFSET(Elf64_Shdr, sh_offset, 24);
OFFSET(Elf64_Shdr, sh_size, 32);
OFFSET(Elf64_Shdr, sh_link, 40);
OFFSET(Elf64_Shdr, sh_info, 44);
OFFSET(Elf64_Shdr, sh_addralign, 48);
OFFSET(Elf64_Shdr, sh_entsize, 56);
CHECK(EI_NIDENT == 16 && ELFCLASS64 == 2 && ELFDATA2LSB == 1);
CHECK(ET_EXEC == 2 && ET_DYN == 3 && EM_X86_64 == 62);
CHECK(PT_LOAD == 1 && PT_TLS == 7 && PF_X == 1 && PF_W == 2 && PF_R == 4);

#ifndef ALOS_KERNEL_ELF_HEADER
CHECK(sizeof(Elf64_Half) == 2 && sizeof(Elf64_Word) == 4);
CHECK(sizeof(Elf64_Sword) == 4 && sizeof(Elf64_Xword) == 8);
CHECK(sizeof(Elf64_Sxword) == 8 && sizeof(Elf64_Addr) == 8);
CHECK(sizeof(Elf64_Off) == 8 && sizeof(ElfW(Ehdr)) == 64);
CHECK((Elf64_Sword)-1 < 0 && (Elf64_Sxword)-1 < 0);
CHECK((Elf64_Word)-1 > 0 && (Elf64_Xword)-1 > 0);
__BEGIN_DECLS
void alos_elf_abi_nothrow(void) __THROW;
__END_DECLS
#ifdef __cplusplus
CHECK(noexcept(alos_elf_abi_nothrow()));
#endif
#ifdef __GLIBC__
#error "ALOS headers must not advertise glibc"
#endif
#endif
