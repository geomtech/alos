#include "base/debug/elf_reader.h"
#include "base/profiler/module_cache.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main() {
  static_assert(sizeof(Elf64_Ehdr) == 64);
  static_assert(sizeof(Elf64_Phdr) == 56);
  static_assert(sizeof(Elf64_Nhdr) == 12);
  static_assert(sizeof(Elf64_Dyn) == 16);
  struct Image {
    Elf64_Ehdr header;
    Elf64_Phdr segments[2];
    Elf64_Nhdr note;
    char name[4];
    unsigned char id[4];
  } image{};
  memcpy(image.header.e_ident, ELFMAG, SELFMAG);
  image.header.e_ident[EI_CLASS] = ELFCLASS64;
  image.header.e_ident[EI_DATA] = ELFDATA2LSB;
  image.header.e_ident[EI_VERSION] = EV_CURRENT;
  image.header.e_type = ET_DYN;
  image.header.e_machine = EM_X86_64;
  image.header.e_version = EV_CURRENT;
  image.header.e_ehsize = sizeof(Elf64_Ehdr);
  image.header.e_phoff = offsetof(Image, segments);
  image.header.e_phnum = 2;
  image.header.e_phentsize = sizeof(Elf64_Phdr);
  image.segments[0].p_type = PT_LOAD;
  image.segments[0].p_flags = PF_R;
  image.segments[0].p_filesz = sizeof(image);
  image.segments[0].p_memsz = sizeof(image);
  image.segments[1].p_type = PT_NOTE;
  image.segments[1].p_vaddr = offsetof(Image, note);
  image.segments[1].p_offset = offsetof(Image, note);
  image.segments[1].p_filesz = 20;
  image.segments[1].p_memsz = 20;
  image.note = {4, 4, NT_GNU_BUILD_ID};
  memcpy(image.name, "GNU", 4);
  const unsigned char id[] = {0x12, 0x34, 0xab, 0xcd};
  memcpy(image.id, id, sizeof(id));
  base::debug::ElfBuildIdBuffer output;
  if (base::debug::GetElfProgramHeaders(&image).size() != 2 ||
      base::debug::GetRelocationOffset(&image) != reinterpret_cast<uintptr_t>(&image) ||
      base::debug::ReadElfBuildId(&image, true, output) != 8 ||
      strcmp(output, "1234ABCD") ||
      base::debug::ReadElfBuildId(&image, false, output) != 8 ||
      strcmp(output, "1234abcd") ||
      base::debug::ReadElfLibraryName(&image)) {
    puts("chromium-elf-reader-smoke: FAIL mapped ELF metadata");
    return 1;
  }
  image.header.e_ident[0] = 0;
  if (!base::debug::GetElfProgramHeaders(&image).empty() ||
      base::debug::ReadElfBuildId(&image, false, output) != 0) {
    puts("chromium-elf-reader-smoke: FAIL invalid ELF magic");
    return 1;
  }
  base::ModuleCache cache;
  errno = 0;
  if (cache.GetModuleForAddress(reinterpret_cast<uintptr_t>(&main)) ||
      errno != ENOTSUP) {
    puts("chromium-elf-reader-smoke: FAIL unavailable dladdr capability");
    return 1;
  }
  puts("chromium-elf-reader-smoke: PASS (mapped bytes, no loader discovery)");
  return 0;
}
