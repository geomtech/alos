/* src/fs/ext2.c - Ext2 Filesystem Driver Implementation */
#include "ext2.h"
#include "../kernel/timer.h"
#include "vfs.h"
#include "../drivers/ata.h"
#include "../mm/kheap.h"
#include "../kernel/klog.h"
#include "../include/errno.h"

/* ===========================================
 * Variables globales
 * =========================================== */
static vfs_filesystem_t ext2_fs_type;
static vfs_dirent_t ext2_dirent;  /* Dirent statique pour readdir */

/* ===========================================
 * Fonctions utilitaires
 * =========================================== */

static void* memset(void* s, int c, size_t n)
{
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void* memcpy(void* dest, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

static int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static size_t strlen(const char* s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* ===========================================
 * Lecture bas niveau
 * =========================================== */

/**
 * Lit un bloc du disque.
 */
int ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buffer)
{
    /* Calculer le LBA de départ */
    uint32_t sectors_per_block = fs->block_size / 512;
    uint32_t lba = block_num * sectors_per_block;
    
    /* Lire les secteurs */
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        if (ata_read_sectors(lba + i, 1, (uint8_t*)buffer + i * 512) != 0) {
            return -1;
        }
    }
    
    return 0;
}

/**
 * Lit un inode spécifique.
 */
int ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* inode)
{
    if (!fs || !inode || inode_num == 0 ||
        inode_num > fs->superblock.s_inodes_count) return -EINVAL;
    if (!fs->group_descs || !fs->inodes_per_group ||
        fs->inode_size < sizeof(ext2_inode_t) ||
        fs->inode_size > fs->block_size) return -EIO;
    
    /* Les inodes sont numérotés à partir de 1 */
    inode_num--;
    
    /* Trouver le groupe contenant cet inode */
    uint32_t group = inode_num / fs->inodes_per_group;
    uint32_t index = inode_num % fs->inodes_per_group;
    if (group >= fs->num_groups) return -EIO;
    
    /* Obtenir le bloc de la table d'inodes pour ce groupe */
    uint32_t inode_table_block = fs->group_descs[group].bg_inode_table;
    
    /* Calculer l'offset de l'inode dans la table */
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * fs->inode_size;
    
    /* Lire le bloc contenant l'inode */
    uint8_t* block_buffer = (uint8_t*)kmalloc(fs->block_size);
    if (block_buffer == NULL) return -ENOMEM;
    
    if (ext2_read_block(fs, inode_table_block + block_offset, block_buffer) != 0) {
        kfree(block_buffer);
        return -EIO;
    }
    
    /* Copier l'inode */
    memcpy(inode, block_buffer + inode_offset, sizeof(ext2_inode_t));
    
    kfree(block_buffer);
    return 0;
}

/* ===========================================
 * Écriture bas niveau
 * =========================================== */

/**
 * Écrit un bloc sur le disque.
 */
int ext2_write_block(ext2_fs_t* fs, uint32_t block_num, const void* buffer)
{
    /* Calculer le LBA de départ */
    uint32_t sectors_per_block = fs->block_size / 512;
    uint32_t lba = block_num * sectors_per_block;
    
    /* Écrire les secteurs */
    for (uint32_t i = 0; i < sectors_per_block; i++) {
        if (ata_write_sectors(lba + i, 1, (const uint8_t*)buffer + i * 512) != 0) {
            return -1;
        }
    }
    
    return 0;
}

/**
 * Écrit le superblock sur le disque.
 * Le superblock est toujours à l'offset 1024 (LBA 2).
 */
int ext2_write_superblock(ext2_fs_t* fs)
{
    /* Le superblock fait 1024 octets et commence à l'offset 1024 */
    /* Donc il occupe les secteurs 2 et 3 (LBA) */
    uint8_t buffer[1024];
    memcpy(buffer, &fs->superblock, sizeof(ext2_superblock_t));
    
    if (ata_write_sectors(2, 2, buffer) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * Écrit un descripteur de groupe sur le disque.
 */
int ext2_write_group_desc(ext2_fs_t* fs, uint32_t group)
{
    if (group >= fs->num_groups) {
        return -1;
    }
    
    /* La table des descripteurs de groupe commence juste après le superblock */
    /* Pour un block_size de 1024, c'est le bloc 2 */
    /* Pour un block_size >= 2048, c'est le bloc 1 */
    uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    
    /* Calculer quel bloc de la GDT contient ce descripteur */
    uint32_t descs_per_block = fs->block_size / sizeof(ext2_group_desc_t);
    uint32_t gdt_block_offset = group / descs_per_block;
    
    /* Lire le bloc entier de la GDT */
    uint8_t* gdt_buffer = (uint8_t*)kmalloc(fs->block_size);
    if (gdt_buffer == NULL) {
        return -1;
    }
    
    if (ext2_read_block(fs, gdt_block + gdt_block_offset, gdt_buffer) != 0) {
        kfree(gdt_buffer);
        return -1;
    }
    
    /* Mettre à jour le descripteur dans le buffer */
    uint32_t offset_in_block = (group % descs_per_block) * sizeof(ext2_group_desc_t);
    memcpy(gdt_buffer + offset_in_block, &fs->group_descs[group], sizeof(ext2_group_desc_t));
    
    /* Réécrire le bloc */
    if (ext2_write_block(fs, gdt_block + gdt_block_offset, gdt_buffer) != 0) {
        kfree(gdt_buffer);
        return -1;
    }
    
    kfree(gdt_buffer);
    return 0;
}

/**
 * Écrit un inode sur le disque.
 */
int ext2_write_inode(ext2_fs_t* fs, uint32_t inode_num, const ext2_inode_t* inode)
{
    if (inode_num == 0) return -1;
    
    /* Les inodes sont numérotés à partir de 1 */
    inode_num--;
    
    /* Trouver le groupe contenant cet inode */
    uint32_t group = inode_num / fs->inodes_per_group;
    uint32_t index = inode_num % fs->inodes_per_group;
    
    /* Obtenir le bloc de la table d'inodes pour ce groupe */
    uint32_t inode_table_block = fs->group_descs[group].bg_inode_table;
    
    /* Calculer l'offset de l'inode dans la table */
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * fs->inode_size;
    
    /* Lire le bloc contenant l'inode */
    uint8_t* block_buffer = (uint8_t*)kmalloc(fs->block_size);
    if (block_buffer == NULL) return -1;
    
    if (ext2_read_block(fs, inode_table_block + block_offset, block_buffer) != 0) {
        kfree(block_buffer);
        return -1;
    }
    
    /* Mettre à jour l'inode dans le buffer */
    memcpy(block_buffer + inode_offset, inode, sizeof(ext2_inode_t));
    
    /* Réécrire le bloc */
    if (ext2_write_block(fs, inode_table_block + block_offset, block_buffer) != 0) {
        kfree(block_buffer);
        return -1;
    }
    
    kfree(block_buffer);
    return 0;
}

/* ===========================================
 * Gestion des Bitmaps
 * =========================================== */

/**
 * Trouve le premier bit à 0 dans un bitmap.
 * @param bitmap   Le bitmap à parcourir
 * @param size     Taille du bitmap en octets
 * @param max_bits Nombre maximum de bits à vérifier
 * @return Index du premier bit libre, ou -1 si aucun
 */
static int32_t find_first_zero_bit(const uint8_t* bitmap, uint32_t size, uint32_t max_bits)
{
    for (uint32_t byte = 0; byte < size; byte++) {
        if (bitmap[byte] != 0xFF) {
            /* Il y a au moins un bit à 0 dans cet octet */
            for (int bit = 0; bit < 8; bit++) {
                uint32_t bit_index = byte * 8 + bit;
                if (bit_index >= max_bits) {
                    return -1;
                }
                if ((bitmap[byte] & (1 << bit)) == 0) {
                    return (int32_t)bit_index;
                }
            }
        }
    }
    return -1;
}

/**
 * Met un bit à 1 dans un bitmap.
 */
static void set_bit(uint8_t* bitmap, uint32_t index)
{
    bitmap[index / 8] |= (1 << (index % 8));
}

/**
 * Met un bit à 0 dans un bitmap.
 */
static void clear_bit(uint8_t* bitmap, uint32_t index)
{
    bitmap[index / 8] &= ~(1 << (index % 8));
}

/* ===========================================
 * Allocation de Blocs
 * =========================================== */

/**
 * Alloue un nouveau bloc sur le disque.
 * 
 * Algorithme:
 * 1. Parcourir les groupes de blocs
 * 2. Pour chaque groupe avec des blocs libres:
 *    a. Lire le bitmap de blocs
 *    b. Trouver le premier bit à 0
 *    c. Le mettre à 1
 *    d. Réécrire le bitmap
 *    e. Mettre à jour les compteurs
 * 3. Retourner le numéro du bloc alloué
 * 
 * @return Numéro du bloc alloué, ou -1 si le disque est plein
 */
int32_t ext2_alloc_block(ext2_fs_t* fs)
{
    /* Vérifier s'il reste des blocs libres */
    if (fs->superblock.s_free_blocks_count == 0) {
        return -ENOSPC;
    }
    
    /* Allouer un buffer pour le bitmap */
    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (bitmap == NULL) {
        return -ENOMEM;
    }
    
    /* Parcourir les groupes de blocs */
    for (uint32_t group = 0; group < fs->num_groups; group++) {
        /* Vérifier si ce groupe a des blocs libres */
        if (fs->group_descs[group].bg_free_blocks_count == 0) {
            continue;
        }
        
        /* Lire le bitmap de blocs de ce groupe */
        uint32_t bitmap_block = fs->group_descs[group].bg_block_bitmap;
        if (ext2_read_block(fs, bitmap_block, bitmap) != 0) {
            kfree(bitmap);
            return -EIO;
        }
        
        /* Déterminer le nombre de blocs dans ce groupe */
        uint32_t blocks_in_group = fs->blocks_per_group;
        /* Le dernier groupe peut avoir moins de blocs */
        if (group == fs->num_groups - 1) {
            uint32_t remaining = fs->superblock.s_blocks_count % fs->blocks_per_group;
            if (remaining != 0) {
                blocks_in_group = remaining;
            }
        }
        
        /* Trouver le premier bit libre */
        int32_t bit_index = find_first_zero_bit(bitmap, fs->block_size, blocks_in_group);
        if (bit_index < 0) {
            /* Pas de bloc libre dans ce groupe (compteur désynchronisé?) */
            continue;
        }
        
        /* Marquer le bloc comme utilisé */
        set_bit(bitmap, (uint32_t)bit_index);
        
        /* Réécrire le bitmap sur le disque */
        if (ext2_write_block(fs, bitmap_block, bitmap) != 0) {
            kfree(bitmap);
            return -EIO;
        }
        
        /* Calculer le numéro de bloc physique */
        uint32_t block_num = group * fs->blocks_per_group + 
                            fs->superblock.s_first_data_block + (uint32_t)bit_index;
        
        /* Mettre à jour les compteurs */
        fs->group_descs[group].bg_free_blocks_count--;
        fs->superblock.s_free_blocks_count--;
        
        /* Écrire le descripteur de groupe mis à jour */
        if (ext2_write_group_desc(fs, group) != 0) {
            /* Rollback: remettre le bit à 0 */
            clear_bit(bitmap, (uint32_t)bit_index);
            if (ext2_write_block(fs, bitmap_block, bitmap) != 0)
                KLOG_ERROR("EXT2", "Failed to roll back block allocation");
            fs->group_descs[group].bg_free_blocks_count++;
            fs->superblock.s_free_blocks_count++;
            kfree(bitmap);
            return -EIO;
        }
        
        /* Écrire le superblock mis à jour */
        if (ext2_write_superblock(fs) != 0) {
            KLOG_ERROR("EXT2", "Failed to persist block allocation counters");
            kfree(bitmap);
            return -EIO;
        }
        
        kfree(bitmap);
        return (int32_t)block_num;
    }
    
    kfree(bitmap);
    return -ENOSPC;
}

/**
 * Libère un bloc.
 * 
 * @param block_num Numéro du bloc à libérer
 * @return 0 si succès, -1 si erreur
 */
int ext2_free_block(ext2_fs_t* fs, uint32_t block_num)
{
    /* Calculer le groupe et l'index dans le groupe */
    uint32_t adjusted = block_num - fs->superblock.s_first_data_block;
    uint32_t group = adjusted / fs->blocks_per_group;
    uint32_t bit_index = adjusted % fs->blocks_per_group;
    
    if (group >= fs->num_groups) {
        return -1;
    }
    
    /* Lire le bitmap de blocs */
    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (bitmap == NULL) {
        return -1;
    }
    
    uint32_t bitmap_block = fs->group_descs[group].bg_block_bitmap;
    if (ext2_read_block(fs, bitmap_block, bitmap) != 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Vérifier que le bloc était bien alloué */
    if ((bitmap[bit_index / 8] & (1 << (bit_index % 8))) == 0) {
        /* Le bloc n'était pas alloué - double free? */
        kfree(bitmap);
        return -1;
    }
    
    /* Marquer le bloc comme libre */
    clear_bit(bitmap, bit_index);
    
    /* Réécrire le bitmap */
    if (ext2_write_block(fs, bitmap_block, bitmap) != 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Mettre à jour les compteurs */
    fs->group_descs[group].bg_free_blocks_count++;
    fs->superblock.s_free_blocks_count++;
    
    /* Écrire les métadonnées mises à jour */
    ext2_write_group_desc(fs, group);
    ext2_write_superblock(fs);
    
    kfree(bitmap);
    return 0;
}

/* ===========================================
 * Allocation d'Inodes
 * =========================================== */

/**
 * Alloue un nouvel inode.
 * 
 * @return Numéro de l'inode alloué (>= 1), ou -1 si plus d'inodes disponibles
 */
int32_t ext2_alloc_inode(ext2_fs_t* fs)
{
    /* Vérifier s'il reste des inodes libres */
    if (fs->superblock.s_free_inodes_count == 0) {
        return -1;
    }
    
    /* Allouer un buffer pour le bitmap */
    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (bitmap == NULL) {
        return -1;
    }
    
    /* Parcourir les groupes */
    for (uint32_t group = 0; group < fs->num_groups; group++) {
        /* Vérifier si ce groupe a des inodes libres */
        if (fs->group_descs[group].bg_free_inodes_count == 0) {
            continue;
        }
        
        /* Lire le bitmap d'inodes de ce groupe */
        uint32_t bitmap_block = fs->group_descs[group].bg_inode_bitmap;
        if (ext2_read_block(fs, bitmap_block, bitmap) != 0) {
            kfree(bitmap);
            return -1;
        }
        
        /* Trouver le premier bit libre */
        int32_t bit_index = find_first_zero_bit(bitmap, fs->block_size, fs->inodes_per_group);
        if (bit_index < 0) {
            continue;
        }
        
        /* Marquer l'inode comme utilisé */
        set_bit(bitmap, (uint32_t)bit_index);
        
        /* Réécrire le bitmap sur le disque */
        if (ext2_write_block(fs, bitmap_block, bitmap) != 0) {
            kfree(bitmap);
            return -1;
        }
        
        /* Calculer le numéro d'inode (1-indexed) */
        uint32_t inode_num = group * fs->inodes_per_group + (uint32_t)bit_index + 1;
        
        /* Mettre à jour les compteurs */
        fs->group_descs[group].bg_free_inodes_count--;
        fs->superblock.s_free_inodes_count--;
        
        /* Écrire le descripteur de groupe mis à jour */
        if (ext2_write_group_desc(fs, group) != 0) {
            /* Rollback */
            clear_bit(bitmap, (uint32_t)bit_index);
            ext2_write_block(fs, bitmap_block, bitmap);
            fs->group_descs[group].bg_free_inodes_count++;
            fs->superblock.s_free_inodes_count++;
            kfree(bitmap);
            return -1;
        }
        
        /* Écrire le superblock mis à jour */
        ext2_write_superblock(fs);
        
        kfree(bitmap);
        return (int32_t)inode_num;
    }
    
    kfree(bitmap);
    return -1;
}

/**
 * Libère un inode.
 * 
 * @param inode_num Numéro de l'inode à libérer (1-indexed)
 * @return 0 si succès, -1 si erreur
 */
int ext2_free_inode(ext2_fs_t* fs, uint32_t inode_num)
{
    if (inode_num == 0) {
        return -1;
    }
    
    /* Convertir en index 0-based */
    inode_num--;
    
    /* Calculer le groupe et l'index dans le groupe */
    uint32_t group = inode_num / fs->inodes_per_group;
    uint32_t bit_index = inode_num % fs->inodes_per_group;
    
    if (group >= fs->num_groups) {
        return -1;
    }
    
    /* Lire le bitmap d'inodes */
    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (bitmap == NULL) {
        return -1;
    }
    
    uint32_t bitmap_block = fs->group_descs[group].bg_inode_bitmap;
    if (ext2_read_block(fs, bitmap_block, bitmap) != 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Vérifier que l'inode était bien alloué */
    if ((bitmap[bit_index / 8] & (1 << (bit_index % 8))) == 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Marquer l'inode comme libre */
    clear_bit(bitmap, bit_index);
    
    /* Réécrire le bitmap */
    if (ext2_write_block(fs, bitmap_block, bitmap) != 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Mettre à jour les compteurs */
    fs->group_descs[group].bg_free_inodes_count++;
    fs->superblock.s_free_inodes_count++;
    
    /* Écrire les métadonnées mises à jour */
    ext2_write_group_desc(fs, group);
    ext2_write_superblock(fs);
    
    kfree(bitmap);
    return 0;
}

/**
 * Lit les données d'un fichier/inode.
 * Gère les blocs directs, indirects simples, doubles et triples.
 */
static int ext2_read_inode_data(ext2_fs_t* fs, ext2_inode_t* inode, 
                                 uint32_t offset, uint32_t size, uint8_t* buffer)
{
    if (offset >= inode->i_size) return 0;
    if (size > inode->i_size - offset) {
        size = inode->i_size - offset;
    }
    
    uint8_t* block_buffer = (uint8_t*)kmalloc(fs->block_size);
    if (block_buffer == NULL) return -1;
    
    uint32_t bytes_read = 0;
    uint32_t block_index = offset / fs->block_size;
    uint32_t block_offset = offset % fs->block_size;
    
    uint32_t ptrs_per_block = fs->block_size / sizeof(uint32_t);
    
    while (bytes_read < size) {
        uint32_t block_num = 0;
        
        if (block_index < 12) {
            /* Bloc direct */
            block_num = inode->i_block[block_index];
        } else if (block_index < 12 + ptrs_per_block) {
            /* Bloc indirect simple */
            uint32_t indirect_index = block_index - 12;
            uint32_t* indirect_block = (uint32_t*)kmalloc(fs->block_size);
            if (indirect_block == NULL) {
                kfree(block_buffer);
                return -1;
            }
            
            if (inode->i_block[12] == 0) {
                block_num = 0;
            } else if (ext2_read_block(fs, inode->i_block[12], indirect_block) == 0) {
                block_num = indirect_block[indirect_index];
            } else {
                kfree(indirect_block);
                kfree(block_buffer);
                return bytes_read ? (int)bytes_read : -EIO;
            }
            kfree(indirect_block);
        } else if (block_index < 12 + ptrs_per_block + ptrs_per_block * ptrs_per_block) {
            /* Bloc indirect double */
            uint32_t di_index = block_index - 12 - ptrs_per_block;
            uint32_t di_first = di_index / ptrs_per_block;
            uint32_t di_second = di_index % ptrs_per_block;
            
            uint32_t* indirect_block = (uint32_t*)kmalloc(fs->block_size);
            if (indirect_block == NULL) {
                kfree(block_buffer);
                return -1;
            }
            
            if (inode->i_block[13] == 0) {
                block_num = 0;
            } else if (ext2_read_block(fs, inode->i_block[13], indirect_block) == 0) {
                uint32_t second_block = indirect_block[di_first];
                if (second_block == 0) {
                    block_num = 0;
                } else if (ext2_read_block(fs, second_block, indirect_block) == 0) {
                    block_num = indirect_block[di_second];
                } else {
                    kfree(indirect_block);
                    kfree(block_buffer);
                    return bytes_read ? (int)bytes_read : -EIO;
                }
            } else {
                kfree(indirect_block);
                kfree(block_buffer);
                return bytes_read ? (int)bytes_read : -EIO;
            }
            kfree(indirect_block);
        }
        else {
            kfree(block_buffer);
            return bytes_read ? (int)bytes_read : -EFBIG;
        }
        
        if (block_num == 0) {
            /* Bloc sparse (trou) - remplir de zéros */
            uint32_t to_copy = fs->block_size - block_offset;
            if (to_copy > size - bytes_read) {
                to_copy = size - bytes_read;
            }
            memset(buffer + bytes_read, 0, to_copy);
            bytes_read += to_copy;
        } else {
            /* Lire le bloc */
            if (ext2_read_block(fs, block_num, block_buffer) != 0) {
                kfree(block_buffer);
                return -1;
            }
            
            uint32_t to_copy = fs->block_size - block_offset;
            if (to_copy > size - bytes_read) {
                to_copy = size - bytes_read;
            }
            
            memcpy(buffer + bytes_read, block_buffer + block_offset, to_copy);
            bytes_read += to_copy;
        }
        
        block_index++;
        block_offset = 0;
    }
    
    kfree(block_buffer);
    return bytes_read;
}

/* ===========================================
 * Écriture de données dans un inode
 * =========================================== */

/**
 * Obtient le numéro de bloc pour un index donné dans un inode.
 * Si le bloc n'existe pas et allocate=1, alloue un nouveau bloc.
 * 
 * @param fs        Contexte du filesystem
 * @param inode     L'inode (sera modifié si allocation)
 * @param block_idx Index du bloc logique (0, 1, 2, ...)
 * @param allocate  1 pour allouer si inexistant, 0 pour retourner 0
 * @return Numéro de bloc physique, ou 0 si inexistant/erreur
 */
static int64_t ext2_allocate_zeroed_block(ext2_fs_t* fs, ext2_inode_t* inode)
{
    uint8_t* zero = (uint8_t*)kmalloc(fs->block_size);
    if (zero == NULL) return -ENOMEM;
    int32_t block = ext2_alloc_block(fs);
    if (block <= 0) {
        kfree(zero);
        return block < 0 ? block : -EIO;
    }
    memset(zero, 0, fs->block_size);
    int result = ext2_write_block(fs, (uint32_t)block, zero);
    kfree(zero);
    if (result != 0) {
        if (ext2_free_block(fs, (uint32_t)block) != 0)
            KLOG_ERROR("EXT2", "Failed to release uninitialized block");
        return -EIO;
    }
    inode->i_blocks += fs->block_size / 512;
    return block;
}

static int64_t ext2_get_block(ext2_fs_t* fs, ext2_inode_t* inode,
                                uint32_t block_idx, int allocate)
{
    uint32_t ptrs_per_block = fs->block_size / sizeof(uint32_t);
    uint32_t block_num = 0;
    
    if (block_idx < 12) {
        /* Bloc direct */
        block_num = inode->i_block[block_idx];
        if (block_num == 0 && allocate) {
            int64_t new_block = ext2_allocate_zeroed_block(fs, inode);
            if (new_block > 0) {
                inode->i_block[block_idx] = (uint32_t)new_block;
                block_num = (uint32_t)new_block;
            } else return new_block;
        }
    } else if (block_idx < 12 + ptrs_per_block) {
        /* Bloc indirect simple */
        uint32_t indirect_index = block_idx - 12;
        uint32_t* indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (indirect_block == NULL) return -ENOMEM;
        
        /* Vérifier/allouer le bloc indirect */
        if (inode->i_block[12] == 0) {
            if (allocate) {
                int64_t new_indirect = ext2_allocate_zeroed_block(fs, inode);
                if (new_indirect > 0) {
                    inode->i_block[12] = (uint32_t)new_indirect;
                } else {
                    kfree(indirect_block);
                    return new_indirect;
                }
            } else {
                kfree(indirect_block);
                return 0;
            }
        }
        
        /* Lire le bloc indirect */
        if (ext2_read_block(fs, inode->i_block[12], indirect_block) != 0) {
            kfree(indirect_block);
            return -EIO;
        }
        
        block_num = indirect_block[indirect_index];
        if (block_num == 0 && allocate) {
            int64_t new_block = ext2_allocate_zeroed_block(fs, inode);
            if (new_block > 0) {
                indirect_block[indirect_index] = (uint32_t)new_block;
                block_num = (uint32_t)new_block;
                /* Réécrire le bloc indirect */
                if (ext2_write_block(fs, inode->i_block[12], indirect_block) != 0) {
                    kfree(indirect_block);
                    return -EIO;
                }
            } else {
                kfree(indirect_block);
                return new_block;
            }
        }
        kfree(indirect_block);
    } else if (block_idx < 12 + ptrs_per_block + ptrs_per_block * ptrs_per_block) {
        /* Bloc indirect double */
        uint32_t di_index = block_idx - 12 - ptrs_per_block;
        uint32_t di_first = di_index / ptrs_per_block;
        uint32_t di_second = di_index % ptrs_per_block;
        
        uint32_t* indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (indirect_block == NULL) return -ENOMEM;
        
        /* Vérifier/allouer le premier niveau d'indirection */
        if (inode->i_block[13] == 0) {
            if (allocate) {
                int64_t new_di = ext2_allocate_zeroed_block(fs, inode);
                if (new_di > 0) {
                    inode->i_block[13] = (uint32_t)new_di;
                } else {
                    kfree(indirect_block);
                    return new_di;
                }
            } else {
                kfree(indirect_block);
                return 0;
            }
        }
        
        /* Lire le premier niveau */
        if (ext2_read_block(fs, inode->i_block[13], indirect_block) != 0) {
            kfree(indirect_block);
            return -EIO;
        }
        
        uint32_t second_level_block = indirect_block[di_first];
        
        /* Vérifier/allouer le second niveau */
        if (second_level_block == 0) {
            if (allocate) {
                int64_t new_second = ext2_allocate_zeroed_block(fs, inode);
                if (new_second > 0) {
                    indirect_block[di_first] = (uint32_t)new_second;
                    if (ext2_write_block(fs, inode->i_block[13], indirect_block) != 0) {
                        kfree(indirect_block);
                        return -EIO;
                    }
                    second_level_block = (uint32_t)new_second;
                } else {
                    kfree(indirect_block);
                    return new_second;
                }
            } else {
                kfree(indirect_block);
                return 0;
            }
        }
        
        /* Lire le second niveau */
        if (ext2_read_block(fs, second_level_block, indirect_block) != 0) {
            kfree(indirect_block);
            return -EIO;
        }
        
        block_num = indirect_block[di_second];
        if (block_num == 0 && allocate) {
            int64_t new_block = ext2_allocate_zeroed_block(fs, inode);
            if (new_block > 0) {
                indirect_block[di_second] = (uint32_t)new_block;
                block_num = (uint32_t)new_block;
                if (ext2_write_block(fs, second_level_block, indirect_block) != 0) {
                    kfree(indirect_block);
                    return -EIO;
                }
            } else {
                kfree(indirect_block);
                return new_block;
            }
        }
        kfree(indirect_block);
    }
    else return -EFBIG;
    
    return block_num;
}

/**
 * Écrit des données dans un inode.
 * Gère l'allocation de nouveaux blocs si nécessaire.
 * 
 * @param fs      Contexte du filesystem
 * @param inode   L'inode (sera modifié)
 * @param inode_num Numéro de l'inode (pour mise à jour sur disque)
 * @param offset  Offset où commencer l'écriture
 * @param size    Nombre d'octets à écrire
 * @param buffer  Données à écrire
 * @return Nombre d'octets écrits, ou -1 si erreur
 */
static int ext2_write_inode_data(ext2_fs_t* fs, ext2_inode_t* inode,
                                  uint32_t inode_num, uint32_t offset, 
                                  uint32_t size, const uint8_t* buffer)
{
    if (size == 0) return 0;
    if (size > INT32_MAX || size > UINT32_MAX - offset) return -EFBIG;
    
    uint8_t* block_buffer = (uint8_t*)kmalloc(fs->block_size);
    if (block_buffer == NULL) return -ENOMEM;
    
    uint32_t bytes_written = 0;
    uint32_t block_index = offset / fs->block_size;
    uint32_t block_offset = offset % fs->block_size;
    int error = 0;
    /* Effacer les octets apres EOF deja presents dans le dernier bloc. */
    if (offset > inode->i_size && inode->i_size % fs->block_size != 0) {
        int64_t last = ext2_get_block(fs, inode,
                                      inode->i_size / fs->block_size, 0);
        if (last < 0) error = (int)last;
        else if (last > 0) {
            if (ext2_read_block(fs, (uint32_t)last, block_buffer) != 0)
                error = -EIO;
            else {
                uint32_t tail = inode->i_size % fs->block_size;
                memset(block_buffer + tail, 0, fs->block_size - tail);
                if (ext2_write_block(fs, (uint32_t)last, block_buffer) != 0)
                    error = -EIO;
            }
        }
        if (error) {
            kfree(block_buffer);
            return error;
        }
    }
    
    while (bytes_written < size) {
        /* Obtenir ou allouer le bloc */
        int64_t block_result = ext2_get_block(fs, inode, block_index, 1);
        if (block_result <= 0) {
            error = block_result < 0 ? (int)block_result : -EIO;
            break;
        }
        uint32_t block_num = (uint32_t)block_result;
        
        /* Calculer combien écrire dans ce bloc */
        uint32_t to_write = fs->block_size - block_offset;
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }
        
        /* Si on n'écrit pas le bloc entier, lire d'abord le contenu existant */
        if (block_offset > 0 || to_write < fs->block_size) {
            if (ext2_read_block(fs, block_num, block_buffer) != 0) {
                error = -EIO;
                break;
            }
        }
        
        /* Copier les données dans le buffer */
        memcpy(block_buffer + block_offset, buffer + bytes_written, to_write);
        
        /* Écrire le bloc sur le disque */
        if (ext2_write_block(fs, block_num, block_buffer) != 0) {
            error = -EIO;
            break;
        }
        
        bytes_written += to_write;
        block_index++;
        block_offset = 0;
    }
    
    /* Mettre à jour la taille de l'inode si nécessaire */
    if (offset + bytes_written > inode->i_size) {
        inode->i_size = offset + bytes_written;
    }
    
    /* Écrire l'inode mis à jour sur le disque */
    if (ext2_write_inode(fs, inode_num, inode) != 0) {
        kfree(block_buffer);
        return -EIO;
    }
    
    kfree(block_buffer);
    return bytes_written ? (int)bytes_written : error;
}

/* ===========================================
 * Callbacks VFS pour Ext2
 * =========================================== */

/* Structure privée pour un noeud Ext2 */
typedef struct ext2_node_data {
    ext2_fs_t* fs;
    uint32_t inode_num;
    ext2_inode_t inode;
    struct ext2_node_data* opened_next;
    unsigned open_refs;
} ext2_node_data_t;
static ext2_node_data_t* opened_nodes;
static int ext2_free_inode_blocks(ext2_fs_t*, ext2_inode_t*);

static int inode_is_open(ext2_fs_t* fs, uint32_t inode_num) {
    for (ext2_node_data_t* d = opened_nodes; d; d = d->opened_next)
        if (d->fs == fs && d->inode_num == inode_num) return 1;
    return 0;
}

static int ext2_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer)
{
    ext2_node_data_t* data = (ext2_node_data_t*)node->fs_data;
    if (data == NULL) return -1;
    int error = ext2_read_inode(data->fs, data->inode_num, &data->inode);
    if (error) return error;
    node->size = data->inode.i_size;
    
    return ext2_read_inode_data(data->fs, &data->inode, offset, size, buffer);
}

static int ext2_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer)
{
    ext2_node_data_t* data = (ext2_node_data_t*)node->fs_data;
    if (data == NULL) return -EIO;
    
    /* Ne pas permettre l'écriture sur un répertoire */
    if (node->type == VFS_DIRECTORY) return -EISDIR;
    int error = ext2_read_inode(data->fs, data->inode_num, &data->inode);
    if (error) return error;
    
    int result = ext2_write_inode_data(data->fs, &data->inode, 
                                        data->inode_num, offset, size, buffer);
    
    /* Mettre à jour la taille dans le noeud VFS */
    if (result > 0) {
        node->size = data->inode.i_size;
    }
    
    return result;
}

static int ext2_vfs_open(vfs_node_t* node, uint32_t flags)
{
    (void)flags;
    ext2_node_data_t* data = node->fs_data;
    if (!data->open_refs++) {
        data->opened_next = opened_nodes;
        opened_nodes = data;
    }
    return 0;
}

static int ext2_vfs_close(vfs_node_t* node)
{
    ext2_node_data_t* data = node->fs_data;
    if (!data->open_refs || --data->open_refs) return 0;
    ext2_node_data_t** link = &opened_nodes;
    while (*link && *link != data) link = &(*link)->opened_next;
    if (*link) *link = data->opened_next;
    if (!inode_is_open(data->fs, data->inode_num)) {
        ext2_inode_t inode;
        if (ext2_read_inode(data->fs, data->inode_num, &inode)) return -EIO;
        if (!inode.i_links_count) {
            if (ext2_free_inode_blocks(data->fs, &inode)) return -EIO;
            inode.i_size = 0;
            inode.i_dtime = 1;
            if (ext2_write_inode(data->fs, data->inode_num, &inode)) return -EIO;
            if (ext2_free_inode(data->fs, data->inode_num)) return -EIO;
            if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
                uint32_t group = (data->inode_num - 1) / data->fs->inodes_per_group;
                if (data->fs->group_descs[group].bg_used_dirs_count)
                    data->fs->group_descs[group].bg_used_dirs_count--;
                if (ext2_write_group_desc(data->fs, group)) return -EIO;
            }
            return 0;
        }
    }
    return 0;
}

static int ext2_vfs_sync(vfs_node_t* node)
{
    if (!node || !node->fs_data) return -EIO;
    /* Les donnees et metadonnees Ext2 sont ecrites immediatement par ATA. */
    return ata_flush() == 0 ? 0 : -EIO;
}

static int ext2_trim_inode(ext2_fs_t*, ext2_inode_t*, uint64_t, uint32_t*);

static int ext2_vfs_resize(vfs_node_t* node, uint32_t length)
{
    ext2_node_data_t* data = node->fs_data;
    if (!data || node->type != VFS_FILE) return -EINVAL;
    if (ext2_read_inode(data->fs, data->inode_num, &data->inode)) return -EIO;
    ext2_fs_t* fs = data->fs;
    uint64_t pointers = fs->block_size / sizeof(uint32_t);
    uint64_t capacity = (12 + pointers + pointers * pointers) * fs->block_size;
    if (length > capacity) return -EFBIG;
    uint32_t old_length = data->inode.i_size;
    if (length == old_length) return 0;
    uint32_t* scratch = kmalloc(4 * fs->block_size);
    if (!scratch) return -ENOMEM;
    uint32_t boundary = length < old_length ? length : old_length;
    if (boundary % fs->block_size) {
        int64_t block = ext2_get_block(fs, &data->inode, boundary / fs->block_size, 0);
        if (block < 0) { kfree(scratch); return (int)block; }
        if (block) {
            uint8_t* bytes = (uint8_t*)scratch + 3 * fs->block_size;
            int error = ext2_read_block(fs, (uint32_t)block, bytes);
            if (!error) {
                uint32_t offset = boundary % fs->block_size;
                memset(bytes + offset, 0, fs->block_size - offset);
                error = ext2_write_block(fs, (uint32_t)block, bytes);
            }
            if (error) { kfree(scratch); return -EIO; }
        }
    }
    if (length < old_length) {
        uint64_t keep = ((uint64_t)length + fs->block_size - 1) / fs->block_size;
        int error = ext2_trim_inode(fs, &data->inode, keep, scratch);
        if (error) {
            /* Les blocs deja retires ne doivent pas rester references. */
            ext2_write_inode(fs, data->inode_num, &data->inode);
            kfree(scratch);
            return error;
        }
    }
    data->inode.i_size = length;
    data->inode.i_dir_acl = 0;
    kfree(scratch);
    if (ext2_write_inode(fs, data->inode_num, &data->inode)) return -EIO;
    node->size = length;
    return 0;
}

static int ext2_vfs_truncate(vfs_node_t* node)
{
    return ext2_vfs_resize(node, 0);
}

/* Convertir un type Ext2 en type VFS */
static uint32_t ext2_type_to_vfs(uint16_t mode)
{
    switch (mode & EXT2_S_IFMT) {
        case EXT2_S_IFREG: return VFS_FILE;
        case EXT2_S_IFDIR: return VFS_DIRECTORY;
        case EXT2_S_IFCHR: return VFS_CHARDEVICE;
        case EXT2_S_IFBLK: return VFS_BLOCKDEVICE;
        case EXT2_S_IFLNK: return VFS_SYMLINK;
        case EXT2_S_IFIFO: return VFS_PIPE;
        default: return VFS_FILE;
    }
}

static uint32_t ext2_ftype_to_vfs(uint8_t file_type)
{
    switch (file_type) {
        case EXT2_FT_REG_FILE: return VFS_FILE;
        case EXT2_FT_DIR: return VFS_DIRECTORY;
        case EXT2_FT_CHRDEV: return VFS_CHARDEVICE;
        case EXT2_FT_BLKDEV: return VFS_BLOCKDEVICE;
        case EXT2_FT_SYMLINK: return VFS_SYMLINK;
        case EXT2_FT_FIFO: return VFS_PIPE;
        default: return VFS_FILE;
    }
}

/* Forward declarations pour les callbacks VFS */
static int ext2_vfs_mkdir(vfs_node_t* parent, const char* name);
static int ext2_vfs_stat(vfs_node_t*, struct stat*);
static int ext2_vfs_statvfs(vfs_node_t*, struct statvfs*);
static int ext2_vfs_set_times(vfs_node_t*, uint32_t, uint32_t, uint32_t);
static int ext2_readdir_checked(vfs_node_t*, uint32_t, vfs_dirent_t*);
static int ext2_lookup_checked(vfs_node_t*, const char*, vfs_node_t**);
static void ext2_dispose_node(vfs_node_t*);
static int ext2_vfs_readlink(vfs_node_t*, char*, uint32_t);
static int ext2_vfs_symlink(vfs_node_t*, const char*, const char*);
static int ext2_vfs_rename(vfs_node_t*, const char*, vfs_node_t*, const char*);

/* Crée un noeud VFS à partir d'un inode Ext2 */
static int ext2_create_node_checked(ext2_fs_t* fs, uint32_t inode_num,
                                   const char* name, vfs_node_t** output)
{
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (node == NULL) return -ENOMEM;
    
    memset(node, 0, sizeof(vfs_node_t));
    
    ext2_node_data_t* data = (ext2_node_data_t*)kmalloc(sizeof(ext2_node_data_t));
    if (data == NULL) {
        kfree(node);
        return -ENOMEM;
    }
    
    data->fs = fs;
    data->inode_num = inode_num;
    data->opened_next = NULL;
    data->open_refs = 0;
    
    /* Lire l'inode */
    int error = ext2_read_inode(fs, inode_num, &data->inode);
    if (error) {
        kfree(data);
        kfree(node);
        return error;
    }
    
    /* Remplir le noeud VFS */
    int i = 0;
    while (name[i] && i < VFS_MAX_NAME) {
        node->name[i] = name[i];
        i++;
    }
    node->name[i] = '\0';
    
    node->inode = inode_num;
    node->type = ext2_type_to_vfs(data->inode.i_mode);
    node->permissions = data->inode.i_mode & 0x0FFF;
    node->uid = data->inode.i_uid;
    node->gid = data->inode.i_gid;
    node->size = data->inode.i_size;
    node->atime = data->inode.i_atime;
    node->mtime = data->inode.i_mtime;
    node->ctime = data->inode.i_ctime;
    node->fs_data = data;
    node->mount = fs->mount;
    node->refcount = 0;
    
    /* Callbacks */
    node->read = ext2_vfs_read;
    node->write = ext2_vfs_write;
    node->open = ext2_vfs_open;
    node->close = ext2_vfs_close;
    node->stat = ext2_vfs_stat;
    node->statvfs = ext2_vfs_statvfs;
    node->set_times = ext2_vfs_set_times;
    node->dispose = ext2_dispose_node;
    node->sync = ext2_vfs_sync;
    node->truncate = ext2_vfs_truncate;
    node->resize = ext2_vfs_resize;
    node->readlink = ext2_vfs_readlink;
    
    /* Déclarer les fonctions avant de les assigner */
    extern vfs_dirent_t* ext2_vfs_readdir(vfs_node_t* node, uint32_t index);
    extern vfs_node_t* ext2_vfs_finddir(vfs_node_t* node, const char* name);
    
    /* Déclarer ext2_vfs_unlink */
    extern int ext2_vfs_unlink(vfs_node_t* parent, const char* name);
    
    if (node->type == VFS_DIRECTORY) {
        node->readdir = ext2_vfs_readdir;
        node->readdir_checked = ext2_readdir_checked;
        node->lookup_checked = ext2_lookup_checked;
        node->finddir = ext2_vfs_finddir;
        node->mkdir = ext2_vfs_mkdir;
        node->create = ext2_vfs_create;
        node->unlink = ext2_vfs_unlink;
        node->symlink = ext2_vfs_symlink;
        node->rename = ext2_vfs_rename;
    }
    
    *output = node;
    return 0;
}

static vfs_node_t* ext2_create_node(ext2_fs_t* fs, uint32_t inode_num,
                                  const char* name)
{
    vfs_node_t* node = NULL;
    if (ext2_create_node_checked(fs, inode_num, name, &node)) return NULL;
    return node;
}

static void ext2_dispose_node(vfs_node_t* node)
{
    kfree(node->fs_data);
    kfree(node);
}

static int ext2_vfs_statvfs(vfs_node_t* node, struct statvfs* information)
{
    ext2_node_data_t* data = node->fs_data;
    if (!data || !data->fs) return -EIO;
    ext2_fs_t* fs = data->fs;
    if (fs->superblock.s_free_blocks_count > fs->superblock.s_blocks_count ||
        fs->superblock.s_free_inodes_count > fs->superblock.s_inodes_count)
        return -EIO;
    memset(information, 0, sizeof(*information));
    information->f_bsize = information->f_frsize = fs->block_size;
    information->f_blocks = fs->superblock.s_blocks_count;
    information->f_bfree = fs->superblock.s_free_blocks_count;
    /* Le profil ALOS n'enforce pas de reserve de blocs par uid. */
    information->f_bavail = information->f_bfree;
    information->f_files = fs->superblock.s_inodes_count;
    information->f_ffree = fs->superblock.s_free_inodes_count;
    information->f_favail = information->f_ffree;
    information->f_flag = ST_NOSUID;
    information->f_namemax = VFS_MAX_NAME;
    return 0;
}

static int ext2_vfs_set_times(vfs_node_t* node, uint32_t atime,
                             uint32_t mtime, uint32_t ctime)
{
    ext2_node_data_t* data = node->fs_data;
    if (!data || !data->fs) return -EIO;
    ext2_inode_t inode;
    int error = ext2_read_inode(data->fs, data->inode_num, &inode);
    if (error) return error;
    inode.i_atime = atime;
    inode.i_mtime = mtime;
    inode.i_ctime = ctime;
    error = ext2_write_inode(data->fs, data->inode_num, &inode);
    if (error) return error;
    data->inode = inode;
    node->atime = atime;
    node->mtime = mtime;
    node->ctime = ctime;
    return 0;
}

static int ext2_vfs_stat(vfs_node_t* node, struct stat* metadata)
{
    ext2_node_data_t* data = node->fs_data;
    if (!data) return -EIO;
    ext2_inode_t inode;
    int error = ext2_read_inode(data->fs, data->inode_num, &inode);
    if (error) return error;
    uint16_t type = inode.i_mode & EXT2_S_IFMT;
    if (type != EXT2_S_IFREG && type != EXT2_S_IFDIR &&
        type != EXT2_S_IFLNK) return -ENOTSUP;
    uint64_t size = inode.i_size;
    if (type == EXT2_S_IFREG &&
        (data->fs->superblock.s_feature_ro_compat & 2))
        size |= (uint64_t)inode.i_dir_acl << 32;
    if (size > INT64_MAX) return -EOVERFLOW;
    memset(metadata, 0, sizeof(*metadata));
    metadata->st_ino = data->inode_num;
    metadata->st_nlink = inode.i_links_count;
    metadata->st_mode = inode.i_mode;
    metadata->st_uid = inode.i_uid;
    metadata->st_gid = inode.i_gid;
    if (data->fs->superblock.s_creator_os == 0) {
        metadata->st_uid |= ((uint32_t)inode.i_osd2[4] |
                              (uint32_t)inode.i_osd2[5] << 8) << 16;
        metadata->st_gid |= ((uint32_t)inode.i_osd2[6] |
                              (uint32_t)inode.i_osd2[7] << 8) << 16;
    }
    metadata->st_size = (int64_t)size;
    metadata->st_blksize = data->fs->block_size;
    metadata->st_blocks = inode.i_blocks;
    metadata->st_atim.tv_sec = inode.i_atime;
    metadata->st_mtim.tv_sec = inode.i_mtime;
    metadata->st_ctim.tv_sec = inode.i_ctime;
    data->inode = inode;
    node->size = inode.i_size;
    return 0;
}

static int ext2_directory_data(vfs_node_t* node, uint8_t** buffer,
                               uint32_t* size)
{
    ext2_node_data_t* data = node->fs_data;
    if (!data) return -EIO;
    int error = ext2_read_inode(data->fs, data->inode_num, &data->inode);
    if (error) return error;
    if ((data->inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -ENOTDIR;
    *size = data->inode.i_size;
    if (*size % data->fs->block_size) return -EIO;
    *buffer = NULL;
    if (*size > INT32_MAX) return -EOVERFLOW;
    if (!*size) return 0;
    *buffer = kmalloc(*size);
    if (!*buffer) return -ENOMEM;
    int received = ext2_read_inode_data(data->fs, &data->inode, 0, *size, *buffer);
    if (received < 0 || (uint32_t)received != *size) {
        kfree(*buffer);
        *buffer = NULL;
        return received < 0 ? received : -EIO;
    }
    return 0;
}

static int ext2_directory_entry(ext2_fs_t* fs, uint8_t* buffer, uint32_t size,
                                uint32_t offset, ext2_dir_entry_t** output)
{
    if (size - offset < sizeof(ext2_dir_entry_t)) return -EIO;
    ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(buffer + offset);
    if (entry->rec_len < sizeof(*entry) || (entry->rec_len & 3) ||
        entry->rec_len > size - offset ||
        entry->rec_len > fs->block_size - offset % fs->block_size ||
        entry->name_len > entry->rec_len - sizeof(*entry) ||
        entry->inode > fs->superblock.s_inodes_count) return -EIO;
    if (entry->inode) {
        if (!entry->name_len || entry->file_type > EXT2_FT_SYMLINK) return -EIO;
        for (unsigned i = 0; i < entry->name_len; i++)
            if (!entry->name[i] || entry->name[i] == '/') return -EIO;
    }
    *output = entry;
    return 0;
}

static int ext2_readdir_checked(vfs_node_t* node, uint32_t index,
                                vfs_dirent_t* output)
{
    uint8_t* buffer;
    uint32_t size;
    int result = ext2_directory_data(node, &buffer, &size);
    if (result) return result;
    ext2_fs_t* fs = ((ext2_node_data_t*)node->fs_data)->fs;
    uint32_t offset = 0, current = 0;
    while (offset < size) {
        ext2_dir_entry_t* entry;
        result = ext2_directory_entry(fs, buffer, size, offset, &entry);
        if (result) break;
        if (entry->inode && current++ == index) {
            memset(output, 0, sizeof(*output));
            output->inode = entry->inode;
            output->type = entry->file_type == EXT2_FT_UNKNOWN ||
                             entry->file_type == EXT2_FT_SOCK
                               ? 0 : ext2_ftype_to_vfs(entry->file_type);
            memcpy(output->name, entry->name, entry->name_len);
            result = 1;
            break;
        }
        offset += entry->rec_len;
    }
    kfree(buffer);
    return result;
}

static int ext2_lookup_checked(vfs_node_t* node, const char* name,
                               vfs_node_t** output)
{
    uint8_t* buffer;
    uint32_t size;
    int result = ext2_directory_data(node, &buffer, &size);
    if (result) return result;
    ext2_fs_t* fs = ((ext2_node_data_t*)node->fs_data)->fs;
    uint32_t offset = 0, inode = 0;
    size_t length = strlen(name);
    while (offset < size) {
        ext2_dir_entry_t* entry;
        result = ext2_directory_entry(fs, buffer, size, offset, &entry);
        if (result) break;
        if (entry->inode && entry->name_len == length &&
            !strncmp(entry->name, name, length)) {
            inode = entry->inode;
            break;
        }
        offset += entry->rec_len;
    }
    kfree(buffer);
    if (result) return result;
    if (!inode) return -ENOENT;
    return ext2_create_node_checked(fs, inode, name, output);
}

/* Lecture des entrées de répertoire */
vfs_dirent_t* ext2_vfs_readdir(vfs_node_t* node, uint32_t index)
{
    if (node == NULL || node->fs_data == NULL) return NULL;
    if ((node->type & VFS_DIRECTORY) == 0) return NULL;
    
    ext2_node_data_t* data = (ext2_node_data_t*)node->fs_data;
    ext2_fs_t* fs = data->fs;
    
    /* Allouer un buffer pour les données du répertoire */
    uint8_t* dir_data = (uint8_t*)kmalloc(data->inode.i_size);
    if (dir_data == NULL) return NULL;
    
    /* Lire toutes les données du répertoire */
    if (ext2_read_inode_data(fs, &data->inode, 0, data->inode.i_size, dir_data) < 0) {
        kfree(dir_data);
        return NULL;
    }
    
    /* Parcourir les entrées jusqu'à l'index voulu */
    uint32_t offset = 0;
    uint32_t current_index = 0;
    
    while (offset < data->inode.i_size) {
        ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(dir_data + offset);
        
        if (entry->inode != 0) {
            if (current_index == index) {
                /* Trouver l'entrée désirée */
                ext2_dirent.inode = entry->inode;
                ext2_dirent.type = ext2_ftype_to_vfs(entry->file_type);
                
                /* Copier le nom */
                int i;
                for (i = 0; i < entry->name_len && i < VFS_MAX_NAME; i++) {
                    ext2_dirent.name[i] = entry->name[i];
                }
                ext2_dirent.name[i] = '\0';
                
                kfree(dir_data);
                return &ext2_dirent;
            }
            current_index++;
        }
        
        offset += entry->rec_len;
        if (entry->rec_len == 0) break;  /* Protection contre boucle infinie */
    }
    
    kfree(dir_data);
    return NULL;
}

/* Recherche dans un répertoire */
vfs_node_t* ext2_vfs_finddir(vfs_node_t* node, const char* name)
{
    if (node == NULL || node->fs_data == NULL || name == NULL) return NULL;
    if ((node->type & VFS_DIRECTORY) == 0) return NULL;
    
    ext2_node_data_t* data = (ext2_node_data_t*)node->fs_data;
    ext2_fs_t* fs = data->fs;
    
    /* Allouer un buffer pour les données du répertoire */
    uint8_t* dir_data = (uint8_t*)kmalloc(data->inode.i_size);
    if (dir_data == NULL) return NULL;
    
    /* Lire toutes les données du répertoire */
    if (ext2_read_inode_data(fs, &data->inode, 0, data->inode.i_size, dir_data) < 0) {
        kfree(dir_data);
        return NULL;
    }
    
    /* Parcourir les entrées */
    uint32_t offset = 0;
    
    while (offset < data->inode.i_size) {
        ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(dir_data + offset);
        
        if (entry->inode != 0) {
            /* Comparer le nom */
            int name_len = 0;
            while (name[name_len]) name_len++;
            
            if (entry->name_len == name_len && 
                strncmp(entry->name, name, name_len) == 0) {
                /* Trouvé! Créer un noeud VFS */
                uint32_t found_inode = entry->inode;
                kfree(dir_data);
                return ext2_create_node(fs, found_inode, name);
            }
        }
        
        offset += entry->rec_len;
        if (entry->rec_len == 0) break;
    }
    
    kfree(dir_data);
    return NULL;
}

/* ===========================================
 * Ajout d'entrée dans un répertoire
 * =========================================== */

/**
 * Calcule la taille réelle nécessaire pour une entrée de répertoire.
 * La taille est alignée sur 4 octets.
 */
static uint32_t ext2_dir_entry_size(uint8_t name_len)
{
    /* Taille = 8 octets (header) + name_len, aligné sur 4 */
    return ((8 + name_len + 3) / 4) * 4;
}

/**
 * Supprime une entrée d'un répertoire.
 * 
 * @param fs         Contexte du filesystem
 * @param dir_inode  Inode du répertoire parent
 * @param dir_inode_num Numéro de l'inode du répertoire
 * @param name       Nom de l'entrée à supprimer
 * @return Numéro de l'inode supprimé si succès, -1 si erreur
 */
static int32_t ext2_remove_dir_entry(ext2_fs_t* fs, ext2_inode_t* dir_inode,
                                      uint32_t dir_inode_num, const char* name)
{
    uint32_t name_len = strlen(name);
    if (name_len > 255 || name_len == 0) return -1;
    
    /* Lire les données du répertoire */
    if (dir_inode->i_size == 0) return -1;
    
    uint8_t* dir_data = (uint8_t*)kmalloc(dir_inode->i_size);
    if (dir_data == NULL) return -1;
    
    if (ext2_read_inode_data(fs, dir_inode, 0, dir_inode->i_size, dir_data) < 0) {
        kfree(dir_data);
        return -1;
    }
    
    /* Chercher l'entrée à supprimer */
    uint32_t offset = 0;
    ext2_dir_entry_t* prev_entry = NULL;
    uint32_t prev_offset = 0;
    int32_t found_inode = -1;
    
    while (offset < dir_inode->i_size) {
        ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(dir_data + offset);
        
        if (entry->rec_len == 0) break;
        
        /* Comparer les noms */
        if (entry->inode != 0 && entry->name_len == name_len) {
            int match = 1;
            for (uint32_t i = 0; i < name_len; i++) {
                if (entry->name[i] != name[i]) {
                    match = 0;
                    break;
                }
            }
            
            if (match) {
                found_inode = (int32_t)entry->inode;
                
                /* Marquer l'entrée comme supprimée (inode = 0) */
                /* Et fusionner avec l'entrée précédente si possible */
                if (prev_entry != NULL) {
                    /* Fusionner avec l'entrée précédente */
                    prev_entry->rec_len += entry->rec_len;
                } else {
                    /* Première entrée - juste marquer l'inode comme 0 */
                    entry->inode = 0;
                }
                break;
            }
        }
        
        prev_entry = entry;
        prev_offset = offset;
        offset += entry->rec_len;
    }
    
    if (found_inode < 0) {
        kfree(dir_data);
        return -1;  /* Entrée non trouvée */
    }
    
    /* Écrire les données mises à jour */
    if (ext2_write_inode_data(fs, dir_inode, dir_inode_num, 0, dir_inode->i_size, dir_data) < 0) {
        kfree(dir_data);
        return -1;
    }
    
    kfree(dir_data);
    return found_inode;
}

/**
 * Ajoute une entrée dans un répertoire.
 * 
 * @param fs         Contexte du filesystem
 * @param dir_inode  Inode du répertoire parent (sera modifié)
 * @param dir_inode_num Numéro de l'inode du répertoire
 * @param new_inode  Numéro de l'inode de la nouvelle entrée
 * @param name       Nom de la nouvelle entrée
 * @param file_type  Type de fichier (EXT2_FT_*)
 * @return 0 si succès, -1 si erreur
 */
static int ext2_add_dir_entry(ext2_fs_t* fs, ext2_inode_t* dir_inode,
                               uint32_t dir_inode_num, uint32_t new_inode,
                               const char* name, uint8_t file_type)
{
    uint32_t name_len = strlen(name);
    if (name_len > 255) name_len = 255;
    
    uint32_t needed_size = ext2_dir_entry_size((uint8_t)name_len);
    
    /* Lire les données du répertoire */
    uint8_t* dir_data = (uint8_t*)kmalloc(dir_inode->i_size > 0 ? dir_inode->i_size : fs->block_size);
    if (dir_data == NULL) return -1;
    
    if (dir_inode->i_size > 0) {
        if (ext2_read_inode_data(fs, dir_inode, 0, dir_inode->i_size, dir_data) < 0) {
            kfree(dir_data);
            return -1;
        }
    }
    
    /* Chercher de l'espace dans les entrées existantes */
    uint32_t offset = 0;
    int found_space = 0;
    
    while (offset < dir_inode->i_size) {
        ext2_dir_entry_t* entry = (ext2_dir_entry_t*)(dir_data + offset);
        
        if (entry->rec_len == 0) break;
        
        /* Calculer la taille réelle utilisée par cette entrée */
        uint32_t actual_size = ext2_dir_entry_size(entry->name_len);
        uint32_t free_space = entry->rec_len - actual_size;
        
        /* Vérifier si on peut insérer notre entrée dans l'espace libre */
        if (free_space >= needed_size) {
            /* Réduire la taille de l'entrée existante */
            entry->rec_len = (uint16_t)actual_size;
            
            /* Créer la nouvelle entrée juste après */
            ext2_dir_entry_t* new_entry = (ext2_dir_entry_t*)(dir_data + offset + actual_size);
            new_entry->inode = new_inode;
            new_entry->rec_len = (uint16_t)free_space;
            new_entry->name_len = (uint8_t)name_len;
            new_entry->file_type = file_type;
            memcpy(new_entry->name, name, name_len);
            
            found_space = 1;
            break;
        }
        
        offset += entry->rec_len;
    }
    
    if (!found_space) {
        /* Pas d'espace - allouer un nouveau bloc */
        int32_t new_block = ext2_alloc_block(fs);
        if (new_block < 0) {
            kfree(dir_data);
            return -1;
        }
        
        /* Réallouer le buffer pour inclure le nouveau bloc */
        uint8_t* new_dir_data = (uint8_t*)kmalloc(dir_inode->i_size + fs->block_size);
        if (new_dir_data == NULL) {
            ext2_free_block(fs, (uint32_t)new_block);
            kfree(dir_data);
            return -1;
        }
        
        if (dir_inode->i_size > 0) {
            memcpy(new_dir_data, dir_data, dir_inode->i_size);
        }
        memset(new_dir_data + dir_inode->i_size, 0, fs->block_size);
        kfree(dir_data);
        dir_data = new_dir_data;
        
        /* Créer l'entrée dans le nouveau bloc */
        ext2_dir_entry_t* new_entry = (ext2_dir_entry_t*)(dir_data + dir_inode->i_size);
        new_entry->inode = new_inode;
        new_entry->rec_len = (uint16_t)fs->block_size;  /* Prend tout le bloc */
        new_entry->name_len = (uint8_t)name_len;
        new_entry->file_type = file_type;
        memcpy(new_entry->name, name, name_len);
        
        /* Mettre à jour la taille du répertoire */
        dir_inode->i_size += fs->block_size;
    }
    
    /* Écrire les données mises à jour */
    if (ext2_write_inode_data(fs, dir_inode, dir_inode_num, 0, dir_inode->i_size, dir_data) < 0) {
        kfree(dir_data);
        return -1;
    }
    
    kfree(dir_data);
    return 0;
}

/* ===========================================
 * Création de fichier (create)
 * =========================================== */

/**
 * Crée un nouveau fichier vide.
 * 
 * @param parent     Noeud VFS du répertoire parent
 * @param name       Nom du nouveau fichier
 * @param type       Type VFS (ignoré, crée toujours un fichier régulier)
 * @return 0 si succès, -1 si erreur
 */
static int ext2_mkdir_mode(vfs_node_t*, const char*, uint32_t);

int ext2_vfs_create(vfs_node_t* parent, const char* name, uint32_t type)
{
    if ((type & 0xffff) == VFS_DIRECTORY)
        return ext2_mkdir_mode(parent, name, type >> 16);
    
    if (parent == NULL || parent->fs_data == NULL || name == NULL) return -1;
    if ((parent->type & VFS_DIRECTORY) == 0) return -1;
    
    ext2_node_data_t* parent_data = (ext2_node_data_t*)parent->fs_data;
    ext2_fs_t* fs = parent_data->fs;
    
    /* Vérifier que le nom n'existe pas déjà */
    vfs_node_t* existing = ext2_vfs_finddir(parent, name);
    if (existing != NULL) {
        /* Le nom existe déjà */
        existing->dispose(existing);
        return -EEXIST;
    }
    
    /* 1. Allouer un nouvel inode */
    int32_t new_inode_num = ext2_alloc_inode(fs);
    if (new_inode_num < 0) {
        KLOG_ERROR("EXT2", "create: failed to allocate inode");
        return -1;
    }
    
    /* 2. Initialiser l'inode du nouveau fichier (vide, pas de bloc alloué) */
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    
    new_inode.i_mode = EXT2_S_IFREG | (type >> 16);
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_size = 0;  /* Fichier vide */
    new_inode.i_atime = 0;  /* TODO: utiliser le temps réel */
    new_inode.i_ctime = 0;
    new_inode.i_mtime = 0;
    new_inode.i_dtime = 0;
    new_inode.i_links_count = 1;  /* Un seul lien (depuis le parent) */
    new_inode.i_blocks = 0;  /* Pas de blocs alloués */
    /* Tous les i_block[] sont déjà à 0 grâce au memset */
    
    /* 3. Écrire l'inode sur le disque */
    if (ext2_write_inode(fs, (uint32_t)new_inode_num, &new_inode) != 0) {
        ext2_free_inode(fs, (uint32_t)new_inode_num);
        return -1;
    }
    
    /* 4. Ajouter l'entrée dans le répertoire parent */
    if (ext2_add_dir_entry(fs, &parent_data->inode, parent_data->inode_num,
                           (uint32_t)new_inode_num, name, EXT2_FT_REG_FILE) != 0) {
        ext2_free_inode(fs, (uint32_t)new_inode_num);
        return -1;
    }
    
    klog(LOG_INFO, "EXT2", "Created file: ");
    klog(LOG_INFO, "EXT2", name);
    
    return 0;
}

/* ===========================================
 * Création de répertoire (mkdir)
 * =========================================== */

/**
 * Crée un nouveau répertoire.
 * 
 * @param parent     Noeud VFS du répertoire parent
 * @param name       Nom du nouveau répertoire
 * @return 0 si succès, -1 si erreur
 */
static int ext2_vfs_mkdir(vfs_node_t* parent, const char* name)
{
    return ext2_mkdir_mode(parent, name, 0755);
}

static int ext2_mkdir_mode(vfs_node_t* parent, const char* name, uint32_t mode)
{
    if (parent == NULL || parent->fs_data == NULL || name == NULL) return -1;
    if ((parent->type & VFS_DIRECTORY) == 0) return -1;
    
    ext2_node_data_t* parent_data = (ext2_node_data_t*)parent->fs_data;
    ext2_fs_t* fs = parent_data->fs;
    
    /* Vérifier que le nom n'existe pas déjà */
    vfs_node_t* existing = ext2_vfs_finddir(parent, name);
    if (existing != NULL) {
        /* Le nom existe déjà */
        existing->dispose(existing);
        return -EEXIST;
    }
    
    /* 1. Allouer un nouvel inode */
    int32_t new_inode_num = ext2_alloc_inode(fs);
    if (new_inode_num < 0) {
        KLOG_ERROR("EXT2", "mkdir: failed to allocate inode");
        return -1;
    }
    
    /* 2. Allouer un bloc pour les entrées du répertoire */
    int32_t new_block = ext2_alloc_block(fs);
    if (new_block < 0) {
        ext2_free_inode(fs, (uint32_t)new_inode_num);
        KLOG_ERROR("EXT2", "mkdir: failed to allocate block");
        return -1;
    }
    
    /* 3. Initialiser l'inode du nouveau répertoire */
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    
    new_inode.i_mode = EXT2_S_IFDIR | (mode & 0777);
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_size = fs->block_size;  /* Un bloc pour . et .. */
    new_inode.i_atime = 0;  /* TODO: utiliser le temps réel */
    new_inode.i_ctime = 0;
    new_inode.i_mtime = 0;
    new_inode.i_dtime = 0;
    new_inode.i_links_count = 2;  /* . et le lien depuis le parent */
    new_inode.i_blocks = fs->block_size / 512;
    new_inode.i_block[0] = (uint32_t)new_block;
    
    /* 4. Créer les entrées . et .. dans le nouveau répertoire */
    uint8_t* dir_block = (uint8_t*)kmalloc(fs->block_size);
    if (dir_block == NULL) {
        ext2_free_block(fs, (uint32_t)new_block);
        ext2_free_inode(fs, (uint32_t)new_inode_num);
        return -1;
    }
    memset(dir_block, 0, fs->block_size);
    
    /* Entrée "." (pointe vers soi-même) */
    ext2_dir_entry_t* dot = (ext2_dir_entry_t*)dir_block;
    dot->inode = (uint32_t)new_inode_num;
    dot->rec_len = 12;  /* Taille minimale pour "." */
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';
    
    /* Entrée ".." (pointe vers le parent) */
    ext2_dir_entry_t* dotdot = (ext2_dir_entry_t*)(dir_block + 12);
    dotdot->inode = parent_data->inode_num;
    dotdot->rec_len = (uint16_t)(fs->block_size - 12);  /* Reste du bloc */
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    
    /* 5. Écrire le bloc du nouveau répertoire */
    if (ext2_write_block(fs, (uint32_t)new_block, dir_block) != 0) {
        kfree(dir_block);
        ext2_free_block(fs, (uint32_t)new_block);
        ext2_free_inode(fs, (uint32_t)new_inode_num);
        return -1;
    }
    kfree(dir_block);
    
    /* 6. Écrire l'inode du nouveau répertoire */
    if (ext2_write_inode(fs, (uint32_t)new_inode_num, &new_inode) != 0) {
        ext2_free_block(fs, (uint32_t)new_block);
        ext2_free_inode(fs, (uint32_t)new_inode_num);
        return -1;
    }
    
    /* 7. Ajouter l'entrée dans le répertoire parent */
    if (ext2_add_dir_entry(fs, &parent_data->inode, parent_data->inode_num,
                           (uint32_t)new_inode_num, name, EXT2_FT_DIR) != 0) {
        ext2_free_block(fs, (uint32_t)new_block);
        ext2_free_inode(fs, (uint32_t)new_inode_num);
        return -1;
    }
    
    /* 8. Mettre à jour le compteur de liens du parent (+1 pour ..) */
    parent_data->inode.i_links_count++;
    if (ext2_write_inode(fs, parent_data->inode_num, &parent_data->inode) != 0) {
        /* L'entrée est déjà ajoutée, on continue quand même */
        KLOG_WARN("EXT2", "mkdir: failed to update parent links");
    }
    
    /* 9. Mettre à jour le compteur de répertoires du groupe */
    uint32_t group = ((uint32_t)new_inode_num - 1) / fs->inodes_per_group;
    if (group < fs->num_groups) {
        fs->group_descs[group].bg_used_dirs_count++;
        ext2_write_group_desc(fs, group);
    }
    
    klog(LOG_INFO, "EXT2", "Created directory: ");
    klog(LOG_INFO, "EXT2", name);
    
    return 0;
}

/* ===========================================
 * Suppression de fichiers et répertoires
 * =========================================== */

/**
 * Vérifie si un répertoire est vide (ne contient que . et ..)
 */
static int ext2_is_dir_empty(ext2_fs_t* fs, ext2_inode_t* dir_inode)
{
    if (dir_inode->i_size == 0) return 1;
    if (dir_inode->i_size % fs->block_size || dir_inode->i_size > INT32_MAX) return -EIO;
    
    uint8_t* dir_data = (uint8_t*)kmalloc(dir_inode->i_size);
    if (dir_data == NULL) return -ENOMEM;
    
    if (ext2_read_inode_data(fs, dir_inode, 0, dir_inode->i_size, dir_data) !=
        (int)dir_inode->i_size) {
        kfree(dir_data);
        return -EIO;
    }
    
    uint32_t offset = 0;
    int entry_count = 0;
    
    while (offset < dir_inode->i_size) {
        ext2_dir_entry_t* entry;
        int error = ext2_directory_entry(fs, dir_data, dir_inode->i_size, offset, &entry);
        if (error) { kfree(dir_data); return error; }
        
        if (entry->inode != 0) {
            /* Ignorer . et .. */
            if (!(entry->name_len == 1 && entry->name[0] == '.') &&
                !(entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.')) {
                entry_count++;
            }
        }
        
        offset += entry->rec_len;
    }
    
    kfree(dir_data);
    return (entry_count == 0) ? 1 : 0;
}

/**
 * Libère tous les blocs de données d'un inode.
 */
static int ext2_free_block_tree(ext2_fs_t* fs, uint32_t block, unsigned depth)
{
    if (!block) return 0;
    if (!depth) return ext2_free_block(fs, block);
    uint32_t* pointers = kmalloc(fs->block_size);
    if (!pointers) return -ENOMEM;
    if (ext2_read_block(fs, block, pointers)) { kfree(pointers); return -EIO; }
    int error = 0;
    for (unsigned i = 0; i < fs->block_size / sizeof(*pointers); i++) {
        if (!pointers[i]) continue;
        error = ext2_free_block_tree(fs, pointers[i], depth - 1);
        if (error) break;
        pointers[i] = 0;
    }
    /* Conserver les pointeurs restants si une erreur impose une reprise. */
    if (error) {
        ext2_write_block(fs, block, pointers);
        kfree(pointers);
        return error;
    }
    kfree(pointers);
    return ext2_free_block(fs, block);
}

static int ext2_free_inode_blocks(ext2_fs_t* fs, ext2_inode_t* inode)
{
    if ((inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFLNK && !inode->i_blocks) {
        memset(inode->i_block, 0, sizeof(inode->i_block));
        return 0;
    }
    /* Libérer les blocs directs */
    for (int i = 0; i < 12; i++) {
        if (inode->i_block[i] != 0) {
            if (ext2_free_block(fs, inode->i_block[i])) return -EIO;
            inode->i_block[i] = 0;
        }
    }
    
    for (unsigned i = 12; i < 15; i++) {
        if (ext2_free_block_tree(fs, inode->i_block[i], i - 11)) return -EIO;
        inode->i_block[i] = 0;
    }
    
    return 0;
}

static int ext2_trim_tree(ext2_fs_t* fs, uint32_t* slot, unsigned depth,
                           uint64_t first, uint64_t keep, uint32_t* released,
                           uint32_t* scratch)
{
    if (!*slot) return 0;
    if (!depth) {
        if (first < keep) return 0;
        if (ext2_free_block(fs, *slot)) return -EIO;
        *slot = 0;
        (*released)++;
        return 0;
    }
    uint32_t count = fs->block_size / sizeof(uint32_t);
    uint32_t* entries = scratch + (depth - 1) * count;
    if (ext2_read_block(fs, *slot, entries)) return -EIO;
    uint64_t span = 1;
    for (unsigned i = 1; i < depth; i++) span *= count;
    int error = 0, changed = 0, occupied = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!entries[i]) continue;
        if (first + ((uint64_t)i + 1) * span <= keep) {
            occupied = 1;
            continue;
        }
        uint32_t before = entries[i];
        error = ext2_trim_tree(fs, &entries[i], depth - 1,
                                first + (uint64_t)i * span, keep, released, scratch);
        if (before != entries[i]) changed = 1;
        if (entries[i]) occupied = 1;
        if (error) break;
    }
    if (error || occupied) {
        if (changed && ext2_write_block(fs, *slot, entries)) error = -EIO;
    } else {
        if (ext2_free_block(fs, *slot)) error = -EIO;
        else { *slot = 0; (*released)++; }
    }
    return error;
}

static int ext2_trim_inode(ext2_fs_t* fs, ext2_inode_t* inode, uint64_t keep,
                            uint32_t* scratch)
{
    uint32_t released = 0;
    int error = 0;
    for (unsigned i = 0; i < 12; i++) {
        uint32_t block = inode->i_block[i];
        error = ext2_trim_tree(fs, &block, 0, i, keep, &released, scratch);
        inode->i_block[i] = block;
        if (error) break;
    }
    uint64_t first = 12, span = fs->block_size / sizeof(uint32_t);
    for (unsigned depth = 1; !error && depth <= 3; depth++) {
        uint32_t block = inode->i_block[11 + depth];
        error = ext2_trim_tree(fs, &block, depth,
                                first, keep, &released, scratch);
        inode->i_block[11 + depth] = block;
        first += span;
        span *= fs->block_size / sizeof(uint32_t);
    }
    uint64_t sectors = (uint64_t)released * (fs->block_size / 512);
    if (sectors > inode->i_blocks) { inode->i_blocks = 0; return -EIO; }
    inode->i_blocks -= (uint32_t)sectors;
    return error;
}

/**
 * Supprime un fichier ou répertoire.
 * Pour les répertoires, ils doivent être vides.
 */
int ext2_vfs_unlink(vfs_node_t* parent, const char* name)
{
    if (parent == NULL || name == NULL) return -EINVAL;
    if (parent->fs_data == NULL) return -EIO;
    
    /* Ne pas permettre la suppression de . et .. */
    if ((name[0] == '.' && name[1] == '\0') ||
        (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
        KLOG_ERROR("EXT2", "unlink: cannot remove . or ..");
        return -EINVAL;
    }
    
    ext2_node_data_t* parent_data = (ext2_node_data_t*)parent->fs_data;
    ext2_fs_t* fs = parent_data->fs;
    
    /* Trouver l'entrée à supprimer pour obtenir l'inode */
    int error = ext2_read_inode(fs, parent_data->inode_num, &parent_data->inode);
    if (error) return error;
    vfs_node_t* target = NULL;
    error = ext2_lookup_checked(parent, name, &target);
    if (error) return error;
    
    ext2_node_data_t* target_data = (ext2_node_data_t*)target->fs_data;
    uint32_t target_inode_num = target_data->inode_num;
    int is_directory = (target->type == VFS_DIRECTORY);
    
    /* Si c'est un répertoire, vérifier qu'il est vide */
    if (is_directory) {
        int empty = ext2_is_dir_empty(fs, &target_data->inode);
        if (empty != 1) {
            KLOG_ERROR("EXT2", "unlink: directory not empty");
            /* Libérer le noeud cible */
            if (target->fs_data) kfree(target->fs_data);
            kfree(target);
            return empty < 0 ? empty : -ENOTEMPTY;
        }
    }
    
    /* Supprimer l'entrée du répertoire parent */
    int32_t removed_inode = ext2_remove_dir_entry(fs, &parent_data->inode,
                                                   parent_data->inode_num, name);
    if (removed_inode < 0) {
        KLOG_ERROR("EXT2", "unlink: failed to remove dir entry");
        if (target->fs_data) kfree(target->fs_data);
        kfree(target);
        return -EIO;
    }
    
    /* Décrémenter le compteur de liens */
    if (is_directory) target_data->inode.i_links_count = 0;
    else if (target_data->inode.i_links_count) target_data->inode.i_links_count--;
    if (is_directory && parent_data->inode.i_links_count) {
        parent_data->inode.i_links_count--;
        if (ext2_write_inode(fs, parent_data->inode_num, &parent_data->inode)) {
            target->dispose(target);
            return -EIO;
        }
    }
    if (!target_data->inode.i_links_count &&
        inode_is_open(fs, target_inode_num)) {
        int error = ext2_write_inode(fs, target_inode_num, &target_data->inode);
        target->dispose(target);
        return error ? -EIO : 0;
    }
    
    /* Si plus aucun lien, libérer l'inode et ses données */
    if (target_data->inode.i_links_count == 0) {
        /* Libérer les blocs de données */
        if (ext2_free_inode_blocks(fs, &target_data->inode)) {
            target->dispose(target);
            return -EIO;
        }
        
        /* Marquer l'inode comme supprimé (dtime non nul) */
        target_data->inode.i_dtime = 1;  /* TODO: utiliser le temps réel */
        target_data->inode.i_size = 0;
        
        /* Écrire l'inode mis à jour */
        if (ext2_write_inode(fs, target_inode_num, &target_data->inode)) {
            target->dispose(target);
            return -EIO;
        }
        
        /* Libérer l'inode */
        if (ext2_free_inode(fs, target_inode_num)) {
            target->dispose(target);
            return -EIO;
        }
        
        /* Si c'était un répertoire, mettre à jour le compteur de répertoires */
        if (is_directory) {
            uint32_t group = (target_inode_num - 1) / fs->inodes_per_group;
            if (group < fs->num_groups && fs->group_descs[group].bg_used_dirs_count > 0) {
                fs->group_descs[group].bg_used_dirs_count--;
                if (ext2_write_group_desc(fs, group)) {
                    target->dispose(target);
                    return -EIO;
                }
            }
            
        }
    } else {
        /* Écrire l'inode avec le compteur de liens décrémenté */
        if (ext2_write_inode(fs, target_inode_num, &target_data->inode)) {
            target->dispose(target);
            return -EIO;
        }
    }
    
    /* Libérer le noeud VFS cible */
    if (target->fs_data) kfree(target->fs_data);
    kfree(target);
    
    klog(LOG_INFO, "EXT2", "Removed: ");
    klog(LOG_INFO, "EXT2", name);
    
    return 0;
}

/* ===========================================
 * Montage/Démontage
 * =========================================== */

static int ext2_vfs_readlink(vfs_node_t* node, char* buffer, uint32_t size)
{
    ext2_node_data_t* data = node->fs_data;
    ext2_inode_t inode;
    int error = ext2_read_inode(data->fs, data->inode_num, &inode);
    if (error) return error;
    if ((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFLNK) return -EINVAL;
    if (inode.i_size >= VFS_MAX_PATH) return -ENAMETOOLONG;
    uint32_t count = size < inode.i_size ? size : inode.i_size;
    if (!inode.i_blocks) {
        if (inode.i_size > sizeof(inode.i_block)) return -EIO;
        memcpy(buffer, inode.i_block, count);
        return (int)count;
    }
    return ext2_read_inode_data(data->fs, &inode, 0, count, (uint8_t*)buffer);
}

static int ext2_vfs_symlink(vfs_node_t* parent, const char* name, const char* target)
{
    ext2_node_data_t* data = parent->fs_data;
    size_t length = strlen(target);
    if (length >= sizeof(((ext2_inode_t*)0)->i_block)) return -ENOTSUP;
    int error = ext2_read_inode(data->fs, data->inode_num, &data->inode);
    if (error) return error;
    if (!data->inode.i_links_count) return -ENOENT;
    int32_t number = ext2_alloc_inode(data->fs);
    if (number < 0) return data->fs->superblock.s_free_inodes_count ? -EIO : -ENOSPC;
    ext2_inode_t inode = {0};
    inode.i_mode = EXT2_S_IFLNK | 0777;
    inode.i_links_count = 1;
    inode.i_size = (uint32_t)length;
    uint64_t now = timer_get_realtime_ms() / 1000;
    if (now > UINT32_MAX) { ext2_free_inode(data->fs, number); return -EOVERFLOW; }
    inode.i_atime = inode.i_mtime = inode.i_ctime = (uint32_t)now;
    memcpy(inode.i_block, target, length);
    error = ext2_write_inode(data->fs, number, &inode);
    if (!error) error = ext2_add_dir_entry(data->fs, &data->inode, data->inode_num,
                                           number, name, EXT2_FT_SYMLINK);
    if (error) {
        vfs_node_t* installed = NULL;
        int lookup = ext2_lookup_checked(parent, name, &installed);
        if (!lookup) {
            installed->dispose(installed);
            /* Une erreur ATA peut avoir publie un prefixe : ne pas recycler
             * l'inode tant qu'une entree peut encore le referencer. */
            KLOG_ERROR("EXT2", "Symlink publication failed; inode retained");
        } else if (lookup == -ENOENT) ext2_free_inode(data->fs, number);
        else KLOG_ERROR("EXT2", "Symlink rollback lookup failed; inode retained");
        return -EIO;
    }
    return 0;
}

static int ext2_vfs_rename(vfs_node_t* parent, const char* from,
                            vfs_node_t* destination, const char* to)
{
    ext2_node_data_t* data = parent->fs_data;
    ext2_node_data_t* other = destination->fs_data;
    if (data->fs != other->fs) return -EXDEV;
    /* Les transactions multi-repertoires et le remplacement ne sont pas
     * simules : le profil initial ne reecrit qu'une entree dans son bloc. */
    if (data->inode_num != other->inode_num) return -ENOTSUP;
    vfs_node_t* source = NULL;
    int error = ext2_lookup_checked(parent, from, &source);
    if (error) return error;
    if (!strncmp(from, to, VFS_MAX_NAME + 1)) { source->dispose(source); return 0; }
    /* Les cwd restent des chemins : renommer un repertoire sans mettre a jour
     * tous ses cwd serait mensonger, meme si ses directory FD restaient valides. */
    if (source->type == VFS_DIRECTORY) { source->dispose(source); return -ENOTSUP; }
    vfs_node_t* existing = NULL;
    error = ext2_lookup_checked(destination, to, &existing);
    if (!error) {
        existing->dispose(existing);
        source->dispose(source);
        return -ENOTSUP;
    }
    if (error != -ENOENT) { source->dispose(source); return error; }
    uint8_t* buffer = NULL;
    uint32_t size;
    error = ext2_directory_data(parent, &buffer, &size);
    if (error) { source->dispose(source); return error; }
    error = -ENOENT;
    size_t length = strlen(to);
    for (uint32_t offset = 0; offset < size;) {
        ext2_dir_entry_t* entry;
        int checked = ext2_directory_entry(data->fs, buffer, size, offset, &entry);
        if (checked) { error = checked; break; }
        if (entry->inode && entry->name_len == strlen(from) &&
            !strncmp(entry->name, from, entry->name_len)) {
            if (length > entry->rec_len - sizeof(*entry)) { error = -ENOTSUP; break; }
            uint32_t base = offset - offset % data->fs->block_size;
            int64_t physical = ext2_get_block(data->fs, &data->inode,
                                               offset / data->fs->block_size, 0);
            if (physical <= 0) { error = -EIO; break; }
            uint8_t* backup = kmalloc(data->fs->block_size);
            if (!backup) { error = -ENOMEM; break; }
            memcpy(backup, buffer + base, data->fs->block_size);
            ext2_inode_t old_parent = data->inode;
            ext2_node_data_t* src = source->fs_data;
            ext2_inode_t old_source = src->inode;
            uint64_t now = timer_get_realtime_ms() / 1000;
            if (now > UINT32_MAX) { kfree(backup); error = -EOVERFLOW; break; }
            data->inode.i_mtime = data->inode.i_ctime = (uint32_t)now;
            src->inode.i_ctime = (uint32_t)now;
            memset(entry->name, 0, entry->rec_len - sizeof(*entry));
            memcpy(entry->name, to, length);
            entry->name_len = (uint8_t)length;
            error = ext2_write_inode(data->fs, data->inode_num, &data->inode);
            if (!error) error = ext2_write_inode(data->fs, src->inode_num, &src->inode);
            if (!error) error = ext2_write_block(data->fs, (uint32_t)physical, buffer + base);
            if (error) {
                int restore_block = ext2_write_block(data->fs, (uint32_t)physical, backup);
                int restore_parent = ext2_write_inode(data->fs, data->inode_num, &old_parent);
                int restore_source = ext2_write_inode(data->fs, src->inode_num, &old_source);
                if (restore_block || restore_parent || restore_source)
                    KLOG_ERROR("EXT2", "Rename rollback failed after ATA error");
                data->inode = old_parent;
                error = -EIO;
            }
            kfree(backup);
            break;
        }
        offset += entry->rec_len;
    }
    kfree(buffer);
    source->dispose(source);
    return error;
}

int ext2_mount(vfs_mount_t* mount, void* device)
{
    (void)device;  /* Pas utilisé pour l'instant, on lit depuis ATA primary */
    
    KLOG_INFO("EXT2", "Mounting filesystem...");
    
    /* Allouer la structure du FS */
    ext2_fs_t* fs = (ext2_fs_t*)kmalloc(sizeof(ext2_fs_t));
    if (fs == NULL) {
        KLOG_ERROR("EXT2", "Failed to allocate fs structure");
        return -1;
    }
    
    memset(fs, 0, sizeof(ext2_fs_t));
    fs->device = device;
    
    /* Lire le superblock (secteurs 2 et 3, offset 1024) */
    uint8_t* sb_buffer = (uint8_t*)kmalloc(1024);
    if (sb_buffer == NULL) {
        kfree(fs);
        return -1;
    }
    
    /* Le superblock est à l'offset 1024, donc LBA 2 */
    if (ata_read_sectors(2, 2, sb_buffer) != 0) {
        KLOG_ERROR("EXT2", "Failed to read superblock");
        kfree(sb_buffer);
        kfree(fs);
        return -1;
    }
    
    memcpy(&fs->superblock, sb_buffer, sizeof(ext2_superblock_t));
    kfree(sb_buffer);
    
    /* Vérifier le magic number */
    if (fs->superblock.s_magic != EXT2_MAGIC) {
        KLOG_ERROR_HEX("EXT2", "Invalid magic number: ", fs->superblock.s_magic);
        kfree(fs);
        return -1;
    }
    
    /* Calculer les paramètres */
    fs->block_size = 1024 << fs->superblock.s_log_block_size;
    fs->inodes_per_group = fs->superblock.s_inodes_per_group;
    fs->blocks_per_group = fs->superblock.s_blocks_per_group;
    fs->num_groups = (fs->superblock.s_blocks_count + fs->blocks_per_group - 1) / fs->blocks_per_group;
    
    /* Taille d'inode (128 pour révision 0, configurable sinon) */
    if (fs->superblock.s_rev_level == 0) {
        fs->inode_size = 128;
    } else {
        fs->inode_size = fs->superblock.s_inode_size;
    }
    
    KLOG_INFO("EXT2", "Superblock valid!");
    KLOG_INFO_DEC("EXT2", "Block size: ", fs->block_size);
    KLOG_INFO_DEC("EXT2", "Total inodes: ", fs->superblock.s_inodes_count);
    KLOG_INFO_DEC("EXT2", "Total blocks: ", fs->superblock.s_blocks_count);
    KLOG_INFO_DEC("EXT2", "Block groups: ", fs->num_groups);
    
    if (fs->superblock.s_volume_name[0] != '\0') {
        klog(LOG_INFO, "EXT2", "Volume name: ");
        klog(LOG_INFO, "EXT2", fs->superblock.s_volume_name);
    }
    
    /* Lire la table des descripteurs de groupes */
    /* Elle commence au bloc suivant le superblock */
    uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    uint32_t gdt_size = fs->num_groups * sizeof(ext2_group_desc_t);
    uint32_t gdt_blocks = (gdt_size + fs->block_size - 1) / fs->block_size;
    
    fs->group_descs = (ext2_group_desc_t*)kmalloc(gdt_blocks * fs->block_size);
    if (fs->group_descs == NULL) {
        kfree(fs);
        return -1;
    }
    
    for (uint32_t i = 0; i < gdt_blocks; i++) {
        if (ext2_read_block(fs, gdt_block + i, 
                           (uint8_t*)fs->group_descs + i * fs->block_size) != 0) {
            KLOG_ERROR("EXT2", "Failed to read group descriptors");
            kfree(fs->group_descs);
            kfree(fs);
            return -1;
        }
    }
    
    /* Stocker le contexte dans le mount */
    mount->fs_specific = fs;
    
    /* Marquer le FS comme "dirty" (non proprement démonté) */
    /* Cela permet de détecter si le système a crashé avant un démontage propre */
    fs->superblock.s_state = EXT2_ERROR_FS;
    ext2_write_superblock(fs);
    
    return 0;
}

int ext2_unmount(vfs_mount_t* mount)
{
    if (mount->fs_specific != NULL) {
        ext2_fs_t* fs = (ext2_fs_t*)mount->fs_specific;
        
        /* Marquer le FS comme "clean" (proprement démonté) */
        fs->superblock.s_state = EXT2_VALID_FS;
        ext2_write_superblock(fs);
        
        if (fs->group_descs != NULL) {
            kfree(fs->group_descs);
        }
        kfree(fs);
        mount->fs_specific = NULL;
    }
    return 0;
}

vfs_node_t* ext2_get_root(vfs_mount_t* mount)
{
    if (mount->fs_specific == NULL) return NULL;
    
    ext2_fs_t* fs = (ext2_fs_t*)mount->fs_specific;
    fs->mount = mount;
    return ext2_create_node(fs, EXT2_ROOT_INODE, "/");
}

/* ===========================================
 * Initialisation du driver
 * =========================================== */

void ext2_init(void)
{
    /* Configurer le type de FS */
    memset(&ext2_fs_type, 0, sizeof(vfs_filesystem_t));
    
    ext2_fs_type.name[0] = 'e';
    ext2_fs_type.name[1] = 'x';
    ext2_fs_type.name[2] = 't';
    ext2_fs_type.name[3] = '2';
    ext2_fs_type.name[4] = '\0';
    
    ext2_fs_type.mount = ext2_mount;
    ext2_fs_type.unmount = ext2_unmount;
    ext2_fs_type.get_root = ext2_get_root;
    
    /* Enregistrer dans le VFS */
    vfs_register_fs(&ext2_fs_type);
}
