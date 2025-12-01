/* src/include/elf.h - ELF32 Format Definitions */
#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* ========================================
 * ELF Magic Number
 * ======================================== */

#define ELF_MAGIC_0     0x7F
#define ELF_MAGIC_1     'E'
#define ELF_MAGIC_2     'L'
#define ELF_MAGIC_3     'F'

/* Macro pour vérifier le magic */
#define ELF_CHECK_MAGIC(ehdr) \
    ((ehdr)->e_ident[0] == ELF_MAGIC_0 && \
     (ehdr)->e_ident[1] == ELF_MAGIC_1 && \
     (ehdr)->e_ident[2] == ELF_MAGIC_2 && \
     (ehdr)->e_ident[3] == ELF_MAGIC_3)

/* ========================================
 * ELF Identification (e_ident)
 * ======================================== */

#define EI_MAG0         0       /* File identification */
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4       /* File class */
#define EI_DATA         5       /* Data encoding */
#define EI_VERSION      6       /* File version */
#define EI_OSABI        7       /* OS/ABI identification */
#define EI_ABIVERSION   8       /* ABI version */
#define EI_PAD          9       /* Start of padding bytes */
#define EI_NIDENT       16      /* Size of e_ident[] */

/* ELF Class */
#define ELFCLASS32      1       /* 32-bit objects */
#define ELFCLASS64      2       /* 64-bit objects */

/* ELF Data Encoding */
#define ELFDATA2LSB     1       /* Little endian */
#define ELFDATA2MSB     2       /* Big endian */

/* ========================================
 * ELF Header Types (e_type)
 * ======================================== */

#define ET_NONE         0       /* No file type */
#define ET_REL          1       /* Relocatable file */
#define ET_EXEC         2       /* Executable file */
#define ET_DYN          3       /* Shared object file */
#define ET_CORE         4       /* Core file */

/* ========================================
 * ELF Machine Types (e_machine)
 * ======================================== */

#define EM_386          3       /* Intel 80386 */
#define EM_X86_64       62      /* AMD x86-64 */

/* ========================================
 * Program Header Types (p_type)
 * ======================================== */

#define PT_NULL         0       /* Unused entry */
#define PT_LOAD         1       /* Loadable segment */
#define PT_DYNAMIC      2       /* Dynamic linking info */
#define PT_INTERP       3       /* Interpreter path */
#define PT_NOTE         4       /* Auxiliary information */
#define PT_SHLIB        5       /* Reserved */
#define PT_PHDR         6       /* Program header table */
#define PT_TLS          7       /* Thread-local storage */

/* ========================================
 * Program Header Flags (p_flags)
 * ======================================== */

#define PF_X            0x1     /* Execute */
#define PF_W            0x2     /* Write */
#define PF_R            0x4     /* Read */

/* ========================================
 * Section Header Types (sh_type)
 * ======================================== */

#define SHT_NULL        0       /* Inactive */
#define SHT_PROGBITS    1       /* Program-defined information */
#define SHT_SYMTAB      2       /* Symbol table */
#define SHT_STRTAB      3       /* String table */
#define SHT_NOBITS      8       /* No space in file (bss) */

/* ========================================
 * ELF32 Header Structure
 * ======================================== */

typedef struct {
    uint8_t     e_ident[EI_NIDENT]; /* ELF identification */
    uint16_t    e_type;             /* Object file type */
    uint16_t    e_machine;          /* Machine type */
    uint32_t    e_version;          /* Object file version */
    uint32_t    e_entry;            /* Entry point address */
    uint32_t    e_phoff;            /* Program header offset */
    uint32_t    e_shoff;            /* Section header offset */
    uint32_t    e_flags;            /* Processor-specific flags */
    uint16_t    e_ehsize;           /* ELF header size */
    uint16_t    e_phentsize;        /* Size of program header entry */
    uint16_t    e_phnum;            /* Number of program header entries */
    uint16_t    e_shentsize;        /* Size of section header entry */
    uint16_t    e_shnum;            /* Number of section header entries */
    uint16_t    e_shstrndx;         /* Section name string table index */
} __attribute__((packed)) Elf32_Ehdr;

/* ========================================
 * ELF32 Program Header Structure
 * ======================================== */

typedef struct {
    uint32_t    p_type;             /* Segment type */
    uint32_t    p_offset;           /* Offset in file */
    uint32_t    p_vaddr;            /* Virtual address in memory */
    uint32_t    p_paddr;            /* Physical address (ignored) */
    uint32_t    p_filesz;           /* Size in file */
    uint32_t    p_memsz;            /* Size in memory */
    uint32_t    p_flags;            /* Segment flags */
    uint32_t    p_align;            /* Alignment */
} __attribute__((packed)) Elf32_Phdr;

/* ========================================
 * ELF32 Section Header Structure
 * ======================================== */

typedef struct {
    uint32_t    sh_name;            /* Section name (index into string table) */
    uint32_t    sh_type;            /* Section type */
    uint32_t    sh_flags;           /* Section flags */
    uint32_t    sh_addr;            /* Address in memory */
    uint32_t    sh_offset;          /* Offset in file */
    uint32_t    sh_size;            /* Size of section */
    uint32_t    sh_link;            /* Link to another section */
    uint32_t    sh_info;            /* Additional section info */
    uint32_t    sh_addralign;       /* Alignment */
    uint32_t    sh_entsize;         /* Entry size if section holds table */
} __attribute__((packed)) Elf32_Shdr;

/* ========================================
 * Error Codes
 * ======================================== */

#define ELF_OK              0       /* Success */
#define ELF_ERR_FILE        -1      /* File not found or read error */
#define ELF_ERR_MAGIC       -2      /* Invalid magic number */
#define ELF_ERR_CLASS       -3      /* Wrong class (not 32-bit) */
#define ELF_ERR_MACHINE     -4      /* Wrong machine type */
#define ELF_ERR_TYPE        -5      /* Not an executable */
#define ELF_ERR_MEMORY      -6      /* Memory allocation failed */
#define ELF_ERR_SEGMENT     -7      /* Invalid segment */

#endif /* ELF_H */
