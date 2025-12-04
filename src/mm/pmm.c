/* src/pmm.c - Physical Memory Manager for x86-64 with Limine */
#include "pmm.h"
#include "../kernel/klog.h"

/* Symboles définis dans le linker script */
extern char _kernel_start[];
extern char _kernel_end[];
extern char _kernel_phys_end[];

/* 
 * Bitmap pour tracker les blocs physiques.
 * Chaque bit représente un bloc de 4 KiB :
 *   - bit = 1 : bloc utilisé/réservé
 *   - bit = 0 : bloc libre
 * 
 * Pour 4 GiB de RAM : 4 GiB / 4 KiB = 1M blocs = 128 KiB de bitmap
 * Pour 64 GiB de RAM : 64 GiB / 4 KiB = 16M blocs = 2 MiB de bitmap
 * On supporte jusqu'à 4 GiB pour commencer (bitmap de 128 KiB)
 */
#define PMM_MAX_MEMORY      (4ULL * 1024 * 1024 * 1024)  /* 4 GiB max */
#define PMM_MAX_BLOCKS      (PMM_MAX_MEMORY / PMM_BLOCK_SIZE)
#define PMM_BITMAP_SIZE     (PMM_MAX_BLOCKS / PMM_BLOCKS_PER_BYTE)

/* Le bitmap statique */
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

/* Statistiques */
static uint64_t pmm_total_blocks = 0;
static uint64_t pmm_used_blocks = 0;
static uint64_t pmm_memory_size = 0;

/* HHDM offset pour conversion phys<->virt */
static uint64_t pmm_hhdm_offset = 0;

/* ============================================ */
/*          Fonctions Bitmap internes           */
/* ============================================ */

/* Marque un bloc comme utilisé */
static inline void bitmap_set(uint64_t block)
{
    if (block < PMM_MAX_BLOCKS) {
        pmm_bitmap[block / 8] |= (1 << (block % 8));
    }
}

/* Marque un bloc comme libre */
static inline void bitmap_clear(uint64_t block)
{
    if (block < PMM_MAX_BLOCKS) {
        pmm_bitmap[block / 8] &= ~(1 << (block % 8));
    }
}

/* Teste si un bloc est utilisé */
static inline int bitmap_test(uint64_t block)
{
    if (block >= PMM_MAX_BLOCKS) {
        return 1; /* Hors limites = considéré comme utilisé */
    }
    return (pmm_bitmap[block / 8] >> (block % 8)) & 1;
}

/* Marque une région entière comme utilisée */
static void pmm_mark_region_used(uint64_t base_addr, uint64_t length)
{
    uint64_t start_block = PMM_ADDR_TO_BLOCK(base_addr);
    uint64_t num_blocks = PMM_ALIGN_UP(length) / PMM_BLOCK_SIZE;
    
    for (uint64_t i = 0; i < num_blocks; i++) {
        if (start_block + i < PMM_MAX_BLOCKS && !bitmap_test(start_block + i)) {
            bitmap_set(start_block + i);
            pmm_used_blocks++;
        }
    }
}

/* Marque une région entière comme libre */
static void pmm_mark_region_free(uint64_t base_addr, uint64_t length)
{
    uint64_t start_block = PMM_ADDR_TO_BLOCK(base_addr);
    uint64_t num_blocks = length / PMM_BLOCK_SIZE;
    
    for (uint64_t i = 0; i < num_blocks; i++) {
        if (start_block + i < PMM_MAX_BLOCKS && bitmap_test(start_block + i)) {
            bitmap_clear(start_block + i);
            pmm_used_blocks--;
        }
    }
}

/* Trouve le premier bloc libre dans le bitmap */
static int64_t pmm_find_first_free(void)
{
    uint64_t bitmap_bytes = pmm_total_blocks / 8;
    if (bitmap_bytes > PMM_BITMAP_SIZE) bitmap_bytes = PMM_BITMAP_SIZE;
    
    for (uint64_t i = 0; i < bitmap_bytes; i++) {
        if (pmm_bitmap[i] != 0xFF) {
            /* Au moins un bit est à 0 dans cet octet */
            for (int j = 0; j < 8; j++) {
                if (!(pmm_bitmap[i] & (1 << j))) {
                    uint64_t block = i * 8 + j;
                    if (block < pmm_total_blocks) {
                        return (int64_t)block;
                    }
                }
            }
        }
    }
    return -1; /* Aucun bloc libre trouvé */
}

/* Trouve une séquence de blocs libres contigus */
static int64_t pmm_find_first_free_sequence(uint64_t count)
{
    if (count == 0) return -1;
    if (count == 1) return pmm_find_first_free();
    
    uint64_t consecutive = 0;
    uint64_t start_block = 0;
    
    for (uint64_t block = 0; block < pmm_total_blocks; block++) {
        if (!bitmap_test(block)) {
            /* Bloc libre trouvé */
            if (consecutive == 0) {
                start_block = block;
            }
            consecutive++;
            
            if (consecutive >= count) {
                return (int64_t)start_block;
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

void init_pmm_limine(struct limine_memmap_response *memmap, uint64_t hhdm_offset)
{
    /* Sauvegarder l'offset HHDM */
    pmm_hhdm_offset = hhdm_offset;
    
    /* Vérifier que la memory map est disponible */
    if (memmap == NULL || memmap->entry_count == 0) {
        KLOG_ERROR("PMM", "No memory map available!");
        return;
    }

    /* Calculer la taille totale de la mémoire utilisable */
    pmm_memory_size = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t entry_end = entry->base + entry->length;
        if (entry_end > pmm_memory_size) {
            pmm_memory_size = entry_end;
        }
    }
    
    /* Limiter à notre maximum */
    if (pmm_memory_size > PMM_MAX_MEMORY) {
        pmm_memory_size = PMM_MAX_MEMORY;
    }
    
    pmm_total_blocks = pmm_memory_size / PMM_BLOCK_SIZE;
    pmm_used_blocks = pmm_total_blocks; /* Par défaut, tout est marqué utilisé */
    
    /* Initialiser le bitmap : tous les blocs sont marqués comme utilisés (1) */
    for (uint64_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFF;
    }

    /* Parser la memory map Limine et libérer les régions utilisables */
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        /* Ne traiter que les régions dans nos limites */
        if (entry->base >= PMM_MAX_MEMORY) {
            continue;
        }
        
        /* Seules les régions USABLE sont vraiment libres */
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t base = entry->base;
            uint64_t length = entry->length;
            
            /* Limiter à notre maximum */
            if (base + length > PMM_MAX_MEMORY) {
                length = PMM_MAX_MEMORY - base;
            }
            
            /* Aligner la base vers le haut et la longueur vers le bas */
            uint64_t aligned_base = PMM_ALIGN_UP(base);
            if (aligned_base > base && length > (aligned_base - base)) {
                length -= (aligned_base - base);
            } else if (aligned_base > base) {
                continue; /* Région trop petite */
            }
            length = PMM_ALIGN_DOWN(length);
            
            if (length >= PMM_BLOCK_SIZE) {
                pmm_mark_region_free(aligned_base, length);
            }
        }
    }

    /*
     * Marquer les premiers 1 MiB comme réservés.
     * Cette zone contient la mémoire conventionnelle, la BIOS, la VGA, etc.
     * C'est une bonne pratique de ne jamais allouer dans cette région.
     */
    pmm_mark_region_used(0, 0x100000);
    
    KLOG_INFO_DEC("PMM", "Total blocks: ", (uint32_t)pmm_total_blocks);
    KLOG_INFO_DEC("PMM", "Used blocks: ", (uint32_t)pmm_used_blocks);
}

void* pmm_alloc_block(void)
{
    if (pmm_used_blocks >= pmm_total_blocks) {
        return NULL; /* Plus de mémoire disponible */
    }

    int64_t block = pmm_find_first_free();
    if (block < 0) {
        return NULL; /* Aucun bloc libre trouvé */
    }

    bitmap_set((uint64_t)block);
    pmm_used_blocks++;

    /* Retourne l'adresse virtuelle via HHDM */
    return (void*)(PMM_BLOCK_TO_ADDR(block) + pmm_hhdm_offset);
}

void* pmm_alloc_blocks(uint64_t count)
{
    if (count == 0) {
        return NULL;
    }
    
    if (count == 1) {
        return pmm_alloc_block();
    }
    
    int64_t start_block = pmm_find_first_free_sequence(count);
    if (start_block < 0) {
        return NULL; /* Pas assez de blocs contigus */
    }
    
    /* Marquer tous les blocs comme utilisés */
    for (uint64_t i = 0; i < count; i++) {
        bitmap_set((uint64_t)start_block + i);
        pmm_used_blocks++;
    }
    
    /* Retourne l'adresse virtuelle via HHDM */
    return (void*)(PMM_BLOCK_TO_ADDR(start_block) + pmm_hhdm_offset);
}

void pmm_free_block(void* p)
{
    /* Convertir l'adresse virtuelle en adresse physique */
    uint64_t addr = (uint64_t)p - pmm_hhdm_offset;
    uint64_t block = PMM_ADDR_TO_BLOCK(addr);

    if (block >= pmm_total_blocks) {
        return; /* Adresse invalide */
    }

    if (bitmap_test(block)) {
        bitmap_clear(block);
        pmm_used_blocks--;
    }
}

void pmm_free_blocks(void* p, uint64_t count)
{
    if (count == 0 || p == NULL) {
        return;
    }
    
    /* Convertir l'adresse virtuelle en adresse physique */
    uint64_t addr = (uint64_t)p - pmm_hhdm_offset;
    uint64_t start_block = PMM_ADDR_TO_BLOCK(addr);
    
    for (uint64_t i = 0; i < count; i++) {
        uint64_t block = start_block + i;
        if (block < pmm_total_blocks && bitmap_test(block)) {
            bitmap_clear(block);
            pmm_used_blocks--;
        }
    }
}

uint64_t pmm_get_total_blocks(void)
{
    return pmm_total_blocks;
}

uint64_t pmm_get_used_blocks(void)
{
    return pmm_used_blocks;
}

uint64_t pmm_get_free_blocks(void)
{
    return pmm_total_blocks - pmm_used_blocks;
}

uint64_t pmm_get_free_memory(void)
{
    return pmm_get_free_blocks() * PMM_BLOCK_SIZE;
}

void* pmm_phys_to_virt(uint64_t phys)
{
    return (void*)(phys + pmm_hhdm_offset);
}

uint64_t pmm_virt_to_phys(void* virt)
{
    return (uint64_t)virt - pmm_hhdm_offset;
}
