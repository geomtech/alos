/* src/fs/ext2.c - Ext2 Filesystem Driver Implementation */
#include "ext2.h"
#include "vfs.h"
#include "../drivers/ata.h"
#include "../mm/kheap.h"
#include "../kernel/console.h"

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
        return -1;
    }
    
    /* Allouer un buffer pour le bitmap */
    uint8_t* bitmap = (uint8_t*)kmalloc(fs->block_size);
    if (bitmap == NULL) {
        return -1;
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
            return -1;
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
            return -1;
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
            ext2_write_block(fs, bitmap_block, bitmap);
            fs->group_descs[group].bg_free_blocks_count++;
            fs->superblock.s_free_blocks_count++;
            kfree(bitmap);
            return -1;
        }
        
        /* Écrire le superblock mis à jour */
        if (ext2_write_superblock(fs) != 0) {
            /* Pas de rollback complet ici, mais le FS sera juste inconsistant */
            /* Un fsck pourrait le réparer */
        }
        
        kfree(bitmap);
        return (int32_t)block_num;
    }
    
    kfree(bitmap);
    return -1;  /* Pas de bloc libre trouvé */
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
    if (offset + size > inode->i_size) {
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
            
            if (ext2_read_block(fs, inode->i_block[12], indirect_block) == 0) {
                block_num = indirect_block[indirect_index];
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
            
            if (ext2_read_block(fs, inode->i_block[13], indirect_block) == 0) {
                uint32_t second_block = indirect_block[di_first];
                if (ext2_read_block(fs, second_block, indirect_block) == 0) {
                    block_num = indirect_block[di_second];
                }
            }
            kfree(indirect_block);
        }
        /* Les blocs indirects triples ne sont pas implémentés (fichiers > 4GB) */
        
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
static uint32_t ext2_get_block(ext2_fs_t* fs, ext2_inode_t* inode, 
                                uint32_t block_idx, int allocate)
{
    uint32_t ptrs_per_block = fs->block_size / sizeof(uint32_t);
    uint32_t block_num = 0;
    
    if (block_idx < 12) {
        /* Bloc direct */
        block_num = inode->i_block[block_idx];
        if (block_num == 0 && allocate) {
            int32_t new_block = ext2_alloc_block(fs);
            if (new_block > 0) {
                inode->i_block[block_idx] = (uint32_t)new_block;
                block_num = (uint32_t)new_block;
                /* Mettre le bloc à zéro */
                uint8_t* zero_buf = (uint8_t*)kmalloc(fs->block_size);
                if (zero_buf) {
                    memset(zero_buf, 0, fs->block_size);
                    ext2_write_block(fs, block_num, zero_buf);
                    kfree(zero_buf);
                }
            }
        }
    } else if (block_idx < 12 + ptrs_per_block) {
        /* Bloc indirect simple */
        uint32_t indirect_index = block_idx - 12;
        uint32_t* indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (indirect_block == NULL) return 0;
        
        /* Vérifier/allouer le bloc indirect */
        if (inode->i_block[12] == 0) {
            if (allocate) {
                int32_t new_indirect = ext2_alloc_block(fs);
                if (new_indirect > 0) {
                    inode->i_block[12] = (uint32_t)new_indirect;
                    memset(indirect_block, 0, fs->block_size);
                    ext2_write_block(fs, (uint32_t)new_indirect, indirect_block);
                } else {
                    kfree(indirect_block);
                    return 0;
                }
            } else {
                kfree(indirect_block);
                return 0;
            }
        }
        
        /* Lire le bloc indirect */
        if (ext2_read_block(fs, inode->i_block[12], indirect_block) != 0) {
            kfree(indirect_block);
            return 0;
        }
        
        block_num = indirect_block[indirect_index];
        if (block_num == 0 && allocate) {
            int32_t new_block = ext2_alloc_block(fs);
            if (new_block > 0) {
                indirect_block[indirect_index] = (uint32_t)new_block;
                block_num = (uint32_t)new_block;
                /* Réécrire le bloc indirect */
                ext2_write_block(fs, inode->i_block[12], indirect_block);
                /* Mettre le nouveau bloc à zéro */
                memset(indirect_block, 0, fs->block_size);
                ext2_write_block(fs, block_num, indirect_block);
            }
        }
        kfree(indirect_block);
    } else if (block_idx < 12 + ptrs_per_block + ptrs_per_block * ptrs_per_block) {
        /* Bloc indirect double */
        uint32_t di_index = block_idx - 12 - ptrs_per_block;
        uint32_t di_first = di_index / ptrs_per_block;
        uint32_t di_second = di_index % ptrs_per_block;
        
        uint32_t* indirect_block = (uint32_t*)kmalloc(fs->block_size);
        if (indirect_block == NULL) return 0;
        
        /* Vérifier/allouer le premier niveau d'indirection */
        if (inode->i_block[13] == 0) {
            if (allocate) {
                int32_t new_di = ext2_alloc_block(fs);
                if (new_di > 0) {
                    inode->i_block[13] = (uint32_t)new_di;
                    memset(indirect_block, 0, fs->block_size);
                    ext2_write_block(fs, (uint32_t)new_di, indirect_block);
                } else {
                    kfree(indirect_block);
                    return 0;
                }
            } else {
                kfree(indirect_block);
                return 0;
            }
        }
        
        /* Lire le premier niveau */
        if (ext2_read_block(fs, inode->i_block[13], indirect_block) != 0) {
            kfree(indirect_block);
            return 0;
        }
        
        uint32_t second_level_block = indirect_block[di_first];
        
        /* Vérifier/allouer le second niveau */
        if (second_level_block == 0) {
            if (allocate) {
                int32_t new_second = ext2_alloc_block(fs);
                if (new_second > 0) {
                    indirect_block[di_first] = (uint32_t)new_second;
                    ext2_write_block(fs, inode->i_block[13], indirect_block);
                    second_level_block = (uint32_t)new_second;
                    memset(indirect_block, 0, fs->block_size);
                    ext2_write_block(fs, second_level_block, indirect_block);
                } else {
                    kfree(indirect_block);
                    return 0;
                }
            } else {
                kfree(indirect_block);
                return 0;
            }
        }
        
        /* Lire le second niveau */
        if (ext2_read_block(fs, second_level_block, indirect_block) != 0) {
            kfree(indirect_block);
            return 0;
        }
        
        block_num = indirect_block[di_second];
        if (block_num == 0 && allocate) {
            int32_t new_block = ext2_alloc_block(fs);
            if (new_block > 0) {
                indirect_block[di_second] = (uint32_t)new_block;
                block_num = (uint32_t)new_block;
                ext2_write_block(fs, second_level_block, indirect_block);
                /* Mettre le nouveau bloc à zéro */
                memset(indirect_block, 0, fs->block_size);
                ext2_write_block(fs, block_num, indirect_block);
            }
        }
        kfree(indirect_block);
    }
    /* Les blocs indirects triples ne sont pas supportés */
    
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
    
    uint8_t* block_buffer = (uint8_t*)kmalloc(fs->block_size);
    if (block_buffer == NULL) return -1;
    
    uint32_t bytes_written = 0;
    uint32_t block_index = offset / fs->block_size;
    uint32_t block_offset = offset % fs->block_size;
    
    while (bytes_written < size) {
        /* Obtenir ou allouer le bloc */
        uint32_t block_num = ext2_get_block(fs, inode, block_index, 1);
        if (block_num == 0) {
            /* Échec d'allocation */
            kfree(block_buffer);
            if (bytes_written > 0) {
                /* Mettre à jour la taille si on a écrit quelque chose */
                if (offset + bytes_written > inode->i_size) {
                    inode->i_size = offset + bytes_written;
                }
                ext2_write_inode(fs, inode_num, inode);
            }
            return bytes_written > 0 ? (int)bytes_written : -1;
        }
        
        /* Calculer combien écrire dans ce bloc */
        uint32_t to_write = fs->block_size - block_offset;
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }
        
        /* Si on n'écrit pas le bloc entier, lire d'abord le contenu existant */
        if (block_offset > 0 || to_write < fs->block_size) {
            if (ext2_read_block(fs, block_num, block_buffer) != 0) {
                /* Bloc vide ou erreur - initialiser à zéro */
                memset(block_buffer, 0, fs->block_size);
            }
        }
        
        /* Copier les données dans le buffer */
        memcpy(block_buffer + block_offset, buffer + bytes_written, to_write);
        
        /* Écrire le bloc sur le disque */
        if (ext2_write_block(fs, block_num, block_buffer) != 0) {
            kfree(block_buffer);
            return bytes_written > 0 ? (int)bytes_written : -1;
        }
        
        bytes_written += to_write;
        block_index++;
        block_offset = 0;
    }
    
    /* Mettre à jour la taille de l'inode si nécessaire */
    if (offset + bytes_written > inode->i_size) {
        inode->i_size = offset + bytes_written;
    }
    
    /* Mettre à jour i_blocks (nombre de secteurs de 512 octets utilisés) */
    uint32_t blocks_used = (inode->i_size + fs->block_size - 1) / fs->block_size;
    inode->i_blocks = blocks_used * (fs->block_size / 512);
    
    /* Écrire l'inode mis à jour sur le disque */
    if (ext2_write_inode(fs, inode_num, inode) != 0) {
        kfree(block_buffer);
        return -1;
    }
    
    kfree(block_buffer);
    return (int)bytes_written;
}

/* ===========================================
 * Callbacks VFS pour Ext2
 * =========================================== */

/* Structure privée pour un noeud Ext2 */
typedef struct ext2_node_data {
    ext2_fs_t* fs;
    uint32_t inode_num;
    ext2_inode_t inode;
} ext2_node_data_t;

static int ext2_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer)
{
    ext2_node_data_t* data = (ext2_node_data_t*)node->fs_data;
    if (data == NULL) return -1;
    
    return ext2_read_inode_data(data->fs, &data->inode, offset, size, buffer);
}

static int ext2_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer)
{
    ext2_node_data_t* data = (ext2_node_data_t*)node->fs_data;
    if (data == NULL) return -1;
    
    /* Ne pas permettre l'écriture sur un répertoire */
    if (node->type == VFS_DIRECTORY) return -1;
    
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
    (void)node;
    (void)flags;
    return 0;  /* Pas de traitement spécial à l'ouverture */
}

static int ext2_vfs_close(vfs_node_t* node)
{
    (void)node;
    return 0;
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

/* Crée un noeud VFS à partir d'un inode Ext2 */
static vfs_node_t* ext2_create_node(ext2_fs_t* fs, uint32_t inode_num, const char* name)
{
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (node == NULL) return NULL;
    
    memset(node, 0, sizeof(vfs_node_t));
    
    ext2_node_data_t* data = (ext2_node_data_t*)kmalloc(sizeof(ext2_node_data_t));
    if (data == NULL) {
        kfree(node);
        return NULL;
    }
    
    data->fs = fs;
    data->inode_num = inode_num;
    
    /* Lire l'inode */
    if (ext2_read_inode(fs, inode_num, &data->inode) != 0) {
        kfree(data);
        kfree(node);
        return NULL;
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
    node->refcount = 0;
    
    /* Callbacks */
    node->read = ext2_vfs_read;
    node->write = ext2_vfs_write;
    node->open = ext2_vfs_open;
    node->close = ext2_vfs_close;
    
    /* Déclarer les fonctions avant de les assigner */
    extern vfs_dirent_t* ext2_vfs_readdir(vfs_node_t* node, uint32_t index);
    extern vfs_node_t* ext2_vfs_finddir(vfs_node_t* node, const char* name);
    
    if (node->type == VFS_DIRECTORY) {
        node->readdir = ext2_vfs_readdir;
        node->finddir = ext2_vfs_finddir;
    }
    
    return node;
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
 * Montage/Démontage
 * =========================================== */

int ext2_mount(vfs_mount_t* mount, void* device)
{
    (void)device;  /* Pas utilisé pour l'instant, on lit depuis ATA primary */
    
    console_puts("[EXT2] Mounting filesystem...\n");
    
    /* Allouer la structure du FS */
    ext2_fs_t* fs = (ext2_fs_t*)kmalloc(sizeof(ext2_fs_t));
    if (fs == NULL) {
        console_puts("[EXT2] Failed to allocate fs structure\n");
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
        console_puts("[EXT2] Failed to read superblock\n");
        kfree(sb_buffer);
        kfree(fs);
        return -1;
    }
    
    memcpy(&fs->superblock, sb_buffer, sizeof(ext2_superblock_t));
    kfree(sb_buffer);
    
    /* Vérifier le magic number */
    if (fs->superblock.s_magic != EXT2_MAGIC) {
        console_puts("[EXT2] Invalid magic number: ");
        console_put_hex(fs->superblock.s_magic);
        console_puts("\n");
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
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[EXT2] Superblock valid!\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    console_puts("[EXT2] Block size: ");
    console_put_dec(fs->block_size);
    console_puts(" bytes\n");
    
    console_puts("[EXT2] Total inodes: ");
    console_put_dec(fs->superblock.s_inodes_count);
    console_puts("\n");
    
    console_puts("[EXT2] Total blocks: ");
    console_put_dec(fs->superblock.s_blocks_count);
    console_puts("\n");
    
    console_puts("[EXT2] Block groups: ");
    console_put_dec(fs->num_groups);
    console_puts("\n");
    
    if (fs->superblock.s_volume_name[0] != '\0') {
        console_puts("[EXT2] Volume name: ");
        console_puts(fs->superblock.s_volume_name);
        console_puts("\n");
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
            console_puts("[EXT2] Failed to read group descriptors\n");
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
