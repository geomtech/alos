/* src/kheap.c - Kernel Heap Allocator avec Liste Chaînée */
#include "kheap.h"
#include "../kernel/thread.h"

/* Pointeur vers le premier bloc du heap */
static KHeapBlock* heap_start = NULL;

/* Taille totale du heap */
static size_t heap_total_size = 0;

/* Spinlock pour protéger l'accès concurrent au heap */
static spinlock_t heap_lock;

/**
 * Aligne une taille sur 4 octets (alignement naturel pour 32-bit).
 * Cela améliore les performances et évite des problèmes d'alignement.
 */
static inline size_t align4(size_t size)
{
    return (size + 3) & ~3;
}

/**
 * Tente de découper un bloc en deux si l'espace restant est suffisant.
 * 
 * @param block Le bloc à découper
 * @param size La taille demandée (alignée)
 */
static void split_block(KHeapBlock* block, size_t size)
{
    /* 
     * On ne split que si l'espace restant peut contenir :
     * - Un nouveau header (sizeof(KHeapBlock))
     * - Au moins KHEAP_MIN_BLOCK_SIZE octets de données
     */
    size_t remaining = block->size - size;
    size_t min_split_size = sizeof(KHeapBlock) + KHEAP_MIN_BLOCK_SIZE;
    
    if (remaining >= min_split_size) {
        /* Calculer l'adresse du nouveau bloc */
        /* IMPORTANT: On cast en uint8_t* pour faire de l'arithmétique en octets */
        uint8_t* block_addr = (uint8_t*)block;
        uint8_t* new_block_addr = block_addr + sizeof(KHeapBlock) + size;
        
        KHeapBlock* new_block = (KHeapBlock*)new_block_addr;
        
        /* Initialiser le nouveau bloc libre */
        new_block->size = remaining - sizeof(KHeapBlock);
        new_block->is_free = true;
        new_block->next = block->next;
        
        /* Mettre à jour le bloc original */
        block->size = size;
        block->next = new_block;
    }
    /* Sinon, on laisse le bloc entier (un peu de gaspillage mais évite la fragmentation) */
}

/**
 * Tente de fusionner un bloc avec le suivant s'il est libre.
 * 
 * @param block Le bloc à fusionner
 */
static void coalesce_block(KHeapBlock* block)
{
    if (block == NULL || block->next == NULL) {
        return;
    }
    
    /* Vérification de sanité : le pointeur next doit être dans le heap */
    uint8_t* heap_end = (uint8_t*)heap_start + heap_total_size;
    if ((uint8_t*)block->next < (uint8_t*)heap_start || 
        (uint8_t*)block->next >= heap_end) {
        /* Corruption détectée ! */
        extern void console_puts(const char*);
        extern void console_put_hex(uint32_t);
        console_puts("\n[KHEAP] CORRUPTION: block->next = 0x");
        console_put_hex((uint32_t)block->next);
        console_puts(" is outside heap!\n");
        block->next = NULL;  /* Couper la chaîne pour éviter le crash */
        return;
    }
    
    /* Si le bloc suivant est libre, on le fusionne */
    if (block->next->is_free) {
        /* 
         * La nouvelle taille inclut :
         * - La taille des données du bloc suivant
         * - Le header du bloc suivant (qui disparaît)
         */
        block->size += sizeof(KHeapBlock) + block->next->size;
        block->next = block->next->next;
        
        /* Récursivement fusionner si le nouveau suivant est aussi libre */
        coalesce_block(block);
    }
}

/* ============================================ */
/*            Fonctions Publiques               */
/* ============================================ */

void kheap_init(void* start_addr, size_t size_bytes)
{
    /* Initialiser le spinlock du heap */
    spinlock_init(&heap_lock);
    
    if (start_addr == NULL || size_bytes < sizeof(KHeapBlock) + KHEAP_MIN_BLOCK_SIZE) {
        return;
    }
    
    heap_start = (KHeapBlock*)start_addr;
    heap_total_size = size_bytes;
    
    /* Créer le premier bloc qui couvre tout le heap */
    heap_start->size = size_bytes - sizeof(KHeapBlock);
    heap_start->is_free = true;
    heap_start->next = NULL;
}

void* kmalloc(size_t size)
{
    /* Vérification rapide avant de prendre le lock */
    if (size == 0 || heap_start == NULL) {
        return NULL;
    }
    
    /* Aligner la taille demandée sur 4 octets */
    size = align4(size);
    
    /* Garantir une taille minimale */
    if (size < KHEAP_MIN_BLOCK_SIZE) {
        size = KHEAP_MIN_BLOCK_SIZE;
    }
    
    /* Prendre le lock avant d'accéder aux structures du heap */
    spinlock_lock(&heap_lock);
    
    /* Parcourir la liste pour trouver un bloc libre assez grand (First Fit) */
    KHeapBlock* current = heap_start;
    
    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            /* Bloc trouvé ! */
            
            /* Essayer de le découper si trop grand */
            split_block(current, size);
            
            /* Marquer comme utilisé */
            current->is_free = false;
            
            /* Libérer le lock avant de retourner */
            spinlock_unlock(&heap_lock);
            
            /* Retourner l'adresse des données (après le header) */
            return KHEAP_BLOCK_DATA(current);
        }
        
        current = current->next;
    }
    
    /* Aucun bloc libre assez grand trouvé - libérer le lock */
    spinlock_unlock(&heap_lock);
    return NULL;
}

void kfree(void* ptr)
{
    /* Vérification rapide avant de prendre le lock */
    if (ptr == NULL || heap_start == NULL) {
        return;
    }
    
    /* Retrouver le header du bloc */
    KHeapBlock* block = KHEAP_DATA_BLOCK(ptr);
    
    /* Prendre le lock avant d'accéder aux structures du heap */
    spinlock_lock(&heap_lock);
    
    /* Vérification basique : le bloc doit être dans le heap */
    uint8_t* heap_end = (uint8_t*)heap_start + heap_total_size;
    if ((uint8_t*)block < (uint8_t*)heap_start || (uint8_t*)block >= heap_end) {
        spinlock_unlock(&heap_lock);
        return; /* Pointeur invalide, hors du heap */
    }
    
    /* Marquer comme libre */
    block->is_free = true;
    
    /* Tenter de fusionner avec le bloc suivant */
    coalesce_block(block);
    
    /* 
     * Note: Pour fusionner avec le bloc précédent, il faudrait parcourir
     * toute la liste depuis le début (car liste simplement chaînée).
     * On le fait ici pour une meilleure défragmentation.
     */
    KHeapBlock* current = heap_start;
    int max_iterations = 10000;  /* Protection contre boucle infinie */
    while (current != NULL && max_iterations-- > 0) {
        /* Vérification de sanité */
        if ((uint8_t*)current < (uint8_t*)heap_start || 
            (uint8_t*)current >= heap_end) {
            break;  /* Corruption, arrêter */
        }
        if (current->is_free) {
            coalesce_block(current);
        }
        current = current->next;
    }
    
    spinlock_unlock(&heap_lock);
}

size_t kheap_get_total_size(void)
{
    return heap_total_size;
}

size_t kheap_get_free_size(void)
{
    spinlock_lock(&heap_lock);
    
    size_t free_size = 0;
    KHeapBlock* current = heap_start;
    
    while (current != NULL) {
        if (current->is_free) {
            free_size += current->size;
        }
        current = current->next;
    }
    
    spinlock_unlock(&heap_lock);
    return free_size;
}

size_t kheap_get_block_count(void)
{
    spinlock_lock(&heap_lock);
    
    size_t count = 0;
    KHeapBlock* current = heap_start;
    
    while (current != NULL) {
        count++;
        current = current->next;
    }
    
    spinlock_unlock(&heap_lock);
    return count;
}

size_t kheap_get_free_block_count(void)
{
    spinlock_lock(&heap_lock);
    
    size_t count = 0;
    KHeapBlock* current = heap_start;
    
    while (current != NULL) {
        if (current->is_free) {
            count++;
        }
        current = current->next;
    }
    
    spinlock_unlock(&heap_lock);
    return count;
}
