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
    
    return 0;
}

int ext2_unmount(vfs_mount_t* mount)
{
    if (mount->fs_specific != NULL) {
        ext2_fs_t* fs = (ext2_fs_t*)mount->fs_specific;
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
