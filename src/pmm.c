/* src/pmm.c - Physical Memory Manager avec Bitmap */
#include "pmm.h"

/* Symboles définis dans le linker script */
extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

/* 
 * Bitmap pour tracker les blocs physiques.
 * Chaque bit représente un bloc de 4 KiB :
 *   - bit = 1 : bloc utilisé/réservé
 *   - bit = 0 : bloc libre
 * 
 * Pour 4 GiB de RAM : 4 GiB / 4 KiB = 1M blocs = 128 KiB de bitmap
 * On limite ici à 128 MiB pour un kernel simple (32 KiB de bitmap)
 */
#define PMM_MAX_MEMORY      (128 * 1024 * 1024)  /* 128 MiB max */
#define PMM_MAX_BLOCKS      (PMM_MAX_MEMORY / PMM_BLOCK_SIZE)
#define PMM_BITMAP_SIZE     (PMM_MAX_BLOCKS / PMM_BLOCKS_PER_BYTE)

/* Le bitmap statique */
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

/* Statistiques */
static uint32_t pmm_total_blocks = 0;
static uint32_t pmm_used_blocks = 0;
static uint32_t pmm_memory_size = 0;

/* ============================================ */
/*          Fonctions Bitmap internes           */
/* ============================================ */

/* Marque un bloc comme utilisé */
static inline void bitmap_set(uint32_t block)
{
    if (block < PMM_MAX_BLOCKS) {
        pmm_bitmap[block / 8] |= (1 << (block % 8));
    }
}

/* Marque un bloc comme libre */
static inline void bitmap_clear(uint32_t block)
{
    if (block < PMM_MAX_BLOCKS) {
        pmm_bitmap[block / 8] &= ~(1 << (block % 8));
    }
}

/* Teste si un bloc est utilisé */
static inline int bitmap_test(uint32_t block)
{
    if (block >= PMM_MAX_BLOCKS) {
        return 1; /* Hors limites = considéré comme utilisé */
    }
    return (pmm_bitmap[block / 8] >> (block % 8)) & 1;
}

/* Marque une région entière comme utilisée */
static void pmm_mark_region_used(uint32_t base_addr, uint32_t length)
{
    uint32_t start_block = PMM_ADDR_TO_BLOCK(base_addr);
    uint32_t num_blocks = PMM_ALIGN_UP(length) / PMM_BLOCK_SIZE;
    
    for (uint32_t i = 0; i < num_blocks; i++) {
        if (!bitmap_test(start_block + i)) {
            bitmap_set(start_block + i);
            pmm_used_blocks++;
        }
    }
}

/* Marque une région entière comme libre */
static void pmm_mark_region_free(uint32_t base_addr, uint32_t length)
{
    uint32_t start_block = PMM_ADDR_TO_BLOCK(base_addr);
    uint32_t num_blocks = length / PMM_BLOCK_SIZE;
    
    for (uint32_t i = 0; i < num_blocks; i++) {
        if (bitmap_test(start_block + i)) {
            bitmap_clear(start_block + i);
            pmm_used_blocks--;
        }
    }
}

/* Trouve le premier bloc libre dans le bitmap */
static int32_t pmm_find_first_free(void)
{
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        if (pmm_bitmap[i] != 0xFF) {
            /* Au moins un bit est à 0 dans cet octet */
            for (int j = 0; j < 8; j++) {
                if (!(pmm_bitmap[i] & (1 << j))) {
                    uint32_t block = i * 8 + j;
                    if (block < pmm_total_blocks) {
                        return block;
                    }
                }
            }
        }
    }
    return -1; /* Aucun bloc libre trouvé */
}

/* Trouve une séquence de blocs libres contigus */
static int32_t pmm_find_first_free_sequence(uint32_t count)
{
    if (count == 0) return -1;
    if (count == 1) return pmm_find_first_free();
    
    uint32_t consecutive = 0;
    uint32_t start_block = 0;
    
    for (uint32_t block = 0; block < pmm_total_blocks; block++) {
        if (!bitmap_test(block)) {
            /* Bloc libre trouvé */
            if (consecutive == 0) {
                start_block = block;
            }
            consecutive++;
            
            if (consecutive >= count) {
                return start_block;
            }
        } else {
            /* Bloc occupé, reset du compteur */
            consecutive = 0;
        }
    }
    
    return -1; /* Pas assez de blocs contigus */
}

/* ============================================ */
/*            Fonctions Publiques               */
/* ============================================ */

void init_pmm(multiboot_info_t* mbd)
{
    /* Vérifier que la memory map est disponible */
    if (!(mbd->flags & MULTIBOOT_INFO_MEM_MAP)) {
        /* Pas de memory map, on ne peut pas initialiser le PMM */
        return;
    }

    /* Calculer la taille totale de la mémoire */
    pmm_memory_size = (mbd->mem_lower + mbd->mem_upper + 1024) * 1024;
    if (pmm_memory_size > PMM_MAX_MEMORY) {
        pmm_memory_size = PMM_MAX_MEMORY;
    }
    
    pmm_total_blocks = pmm_memory_size / PMM_BLOCK_SIZE;
    pmm_used_blocks = pmm_total_blocks; /* Par défaut, tout est marqué utilisé */
    
    /* Initialiser le bitmap : tous les blocs sont marqués comme utilisés (1) */
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFF;
    }

    /* Parser la memory map et libérer les régions disponibles */
    multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)(uintptr_t)mbd->mmap_addr;
    multiboot_mmap_entry_t* mmap_end = (multiboot_mmap_entry_t*)((uintptr_t)mbd->mmap_addr + mbd->mmap_length);
    
    while (mmap < mmap_end) {
        /* Ne traiter que les régions dans nos limites (32-bit) */
        if (mmap->addr < PMM_MAX_MEMORY) {
            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                /* Cette région est disponible, la marquer comme libre */
                uint32_t base = (uint32_t)mmap->addr;
                uint32_t length = (uint32_t)mmap->len;
                
                /* Limiter à notre maximum */
                if (base + length > PMM_MAX_MEMORY) {
                    length = PMM_MAX_MEMORY - base;
                }
                
                /* Aligner la base vers le haut et la longueur vers le bas */
                uint32_t aligned_base = PMM_ALIGN_UP(base);
                if (aligned_base > base) {
                    length -= (aligned_base - base);
                }
                length = PMM_ALIGN_DOWN(length);
                
                if (length >= PMM_BLOCK_SIZE) {
                    pmm_mark_region_free(aligned_base, length);
                }
            }
        }
        
        /* Avancer vers l'entrée suivante */
        mmap = (multiboot_mmap_entry_t*)((uintptr_t)mmap + mmap->size + sizeof(mmap->size));
    }

    /* 
     * Marquer le kernel comme utilisé.
     * Les symboles _kernel_start et _kernel_end sont définis dans le linker script.
     */
    uint32_t kernel_start = (uint32_t)(uintptr_t)&_kernel_start;
    uint32_t kernel_end = (uint32_t)(uintptr_t)&_kernel_end;
    uint32_t kernel_size = kernel_end - kernel_start;
    
    pmm_mark_region_used(kernel_start, kernel_size);

    /*
     * Marquer les premiers 1 MiB comme réservés.
     * Cette zone contient la mémoire conventionnelle, la BIOS, la VGA, etc.
     * C'est une bonne pratique de ne jamais allouer dans cette région.
     */
    pmm_mark_region_used(0, 0x100000);
    
    /*
     * Marquer le bitmap lui-même comme utilisé.
     * Note: Dans notre cas le bitmap est dans la section .bss du kernel,
     * donc il est déjà protégé par la protection du kernel ci-dessus.
     */
}

void* pmm_alloc_block(void)
{
    if (pmm_used_blocks >= pmm_total_blocks) {
        return NULL; /* Plus de mémoire disponible */
    }

    int32_t block = pmm_find_first_free();
    if (block < 0) {
        return NULL; /* Aucun bloc libre trouvé */
    }

    bitmap_set(block);
    pmm_used_blocks++;

    return (void*)(uintptr_t)PMM_BLOCK_TO_ADDR(block);
}

void* pmm_alloc_blocks(uint32_t count)
{
    if (count == 0) {
        return NULL;
    }
    
    if (count == 1) {
        return pmm_alloc_block();
    }
    
    int32_t start_block = pmm_find_first_free_sequence(count);
    if (start_block < 0) {
        return NULL; /* Pas assez de blocs contigus */
    }
    
    /* Marquer tous les blocs comme utilisés */
    for (uint32_t i = 0; i < count; i++) {
        bitmap_set(start_block + i);
        pmm_used_blocks++;
    }
    
    return (void*)(uintptr_t)PMM_BLOCK_TO_ADDR(start_block);
}

void pmm_free_block(void* p)
{
    uint32_t addr = (uint32_t)(uintptr_t)p;
    uint32_t block = PMM_ADDR_TO_BLOCK(addr);

    if (block >= pmm_total_blocks) {
        return; /* Adresse invalide */
    }

    if (bitmap_test(block)) {
        bitmap_clear(block);
        pmm_used_blocks--;
    }
}

void pmm_free_blocks(void* p, uint32_t count)
{
    if (count == 0 || p == NULL) {
        return;
    }
    
    uint32_t addr = (uint32_t)(uintptr_t)p;
    uint32_t start_block = PMM_ADDR_TO_BLOCK(addr);
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t block = start_block + i;
        if (block < pmm_total_blocks && bitmap_test(block)) {
            bitmap_clear(block);
            pmm_used_blocks--;
        }
    }
}

uint32_t pmm_get_total_blocks(void)
{
    return pmm_total_blocks;
}

uint32_t pmm_get_used_blocks(void)
{
    return pmm_used_blocks;
}

uint32_t pmm_get_free_blocks(void)
{
    return pmm_total_blocks - pmm_used_blocks;
}

uint32_t pmm_get_free_memory(void)
{
    return pmm_get_free_blocks() * PMM_BLOCK_SIZE;
}
