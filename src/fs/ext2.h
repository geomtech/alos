/* src/fs/ext2.h - Ext2 Filesystem Driver */
#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include "vfs.h"

/* ===========================================
 * Constantes Ext2
 * =========================================== */
#define EXT2_MAGIC              0xEF53
#define EXT2_ROOT_INODE         2       /* Inode de la racine */
#define EXT2_SUPERBLOCK_OFFSET  1024    /* Offset du superblock */

/* ===========================================
 * Types de fichiers (dans i_mode)
 * =========================================== */
#define EXT2_S_IFSOCK   0xC000  /* Socket */
#define EXT2_S_IFLNK    0xA000  /* Lien symbolique */
#define EXT2_S_IFREG    0x8000  /* Fichier régulier */
#define EXT2_S_IFBLK    0x6000  /* Block device */
#define EXT2_S_IFDIR    0x4000  /* Répertoire */
#define EXT2_S_IFCHR    0x2000  /* Character device */
#define EXT2_S_IFIFO    0x1000  /* FIFO */

#define EXT2_S_IFMT     0xF000  /* Masque pour le type */

/* ===========================================
 * Types dans les entrées de répertoire
 * =========================================== */
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

/* ===========================================
 * Superblock (situé à l'offset 1024)
 * =========================================== */
typedef struct ext2_superblock {
    uint32_t s_inodes_count;        /* Nombre total d'inodes */
    uint32_t s_blocks_count;        /* Nombre total de blocs */
    uint32_t s_r_blocks_count;      /* Blocs réservés */
    uint32_t s_free_blocks_count;   /* Blocs libres */
    uint32_t s_free_inodes_count;   /* Inodes libres */
    uint32_t s_first_data_block;    /* Premier bloc de données */
    uint32_t s_log_block_size;      /* Taille bloc = 1024 << s_log_block_size */
    uint32_t s_log_frag_size;       /* Taille fragment */
    uint32_t s_blocks_per_group;    /* Blocs par groupe */
    uint32_t s_frags_per_group;     /* Fragments par groupe */
    uint32_t s_inodes_per_group;    /* Inodes par groupe */
    uint32_t s_mtime;               /* Dernier montage */
    uint32_t s_wtime;               /* Dernière écriture */
    uint16_t s_mnt_count;           /* Compteur de montages */
    uint16_t s_max_mnt_count;       /* Max montages avant fsck */
    uint16_t s_magic;               /* Magic number (0xEF53) */
    uint16_t s_state;               /* État du FS */
    uint16_t s_errors;              /* Comportement en cas d'erreur */
    uint16_t s_minor_rev_level;     /* Révision mineure */
    uint32_t s_lastcheck;           /* Dernier fsck */
    uint32_t s_checkinterval;       /* Intervalle max entre fsck */
    uint32_t s_creator_os;          /* OS créateur */
    uint32_t s_rev_level;           /* Révision majeure */
    uint16_t s_def_resuid;          /* UID par défaut pour blocs réservés */
    uint16_t s_def_resgid;          /* GID par défaut pour blocs réservés */
    
    /* Champs EXT2_DYNAMIC_REV uniquement */
    uint32_t s_first_ino;           /* Premier inode non-réservé */
    uint16_t s_inode_size;          /* Taille d'un inode */
    uint16_t s_block_group_nr;      /* Groupe de ce superblock */
    uint32_t s_feature_compat;      /* Features compatibles */
    uint32_t s_feature_incompat;    /* Features incompatibles */
    uint32_t s_feature_ro_compat;   /* Features read-only compatibles */
    uint8_t  s_uuid[16];            /* UUID du volume */
    char     s_volume_name[16];     /* Nom du volume */
    char     s_last_mounted[64];    /* Dernier point de montage */
    uint32_t s_algo_bitmap;         /* Algorithme de compression */
    
    /* Padding jusqu'à 1024 octets */
    uint8_t  padding[820];
} __attribute__((packed)) ext2_superblock_t;

/* ===========================================
 * Block Group Descriptor
 * =========================================== */
typedef struct ext2_group_desc {
    uint32_t bg_block_bitmap;       /* Bloc du bitmap de blocs */
    uint32_t bg_inode_bitmap;       /* Bloc du bitmap d'inodes */
    uint32_t bg_inode_table;        /* Premier bloc de la table d'inodes */
    uint16_t bg_free_blocks_count;  /* Blocs libres dans ce groupe */
    uint16_t bg_free_inodes_count;  /* Inodes libres dans ce groupe */
    uint16_t bg_used_dirs_count;    /* Répertoires dans ce groupe */
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

/* ===========================================
 * Inode
 * =========================================== */
typedef struct ext2_inode {
    uint16_t i_mode;                /* Type et permissions */
    uint16_t i_uid;                 /* User ID */
    uint32_t i_size;                /* Taille en octets */
    uint32_t i_atime;               /* Access time */
    uint32_t i_ctime;               /* Creation time */
    uint32_t i_mtime;               /* Modification time */
    uint32_t i_dtime;               /* Deletion time */
    uint16_t i_gid;                 /* Group ID */
    uint16_t i_links_count;         /* Hard links count */
    uint32_t i_blocks;              /* Blocs de 512 octets */
    uint32_t i_flags;               /* Flags */
    uint32_t i_osd1;                /* OS dependent */
    uint32_t i_block[15];           /* Pointeurs vers blocs de données */
                                    /* [0-11] = direct */
                                    /* [12] = indirect simple */
                                    /* [13] = indirect double */
                                    /* [14] = indirect triple */
    uint32_t i_generation;          /* File version (NFS) */
    uint32_t i_file_acl;            /* ACL fichier */
    uint32_t i_dir_acl;             /* ACL répertoire (ou taille haute) */
    uint32_t i_faddr;               /* Fragment address */
    uint8_t  i_osd2[12];            /* OS dependent */
} __attribute__((packed)) ext2_inode_t;

/* ===========================================
 * Entrée de répertoire
 * =========================================== */
typedef struct ext2_dir_entry {
    uint32_t inode;                 /* Numéro d'inode */
    uint16_t rec_len;               /* Taille totale de cette entrée */
    uint8_t  name_len;              /* Longueur du nom */
    uint8_t  file_type;             /* Type de fichier */
    char     name[];                /* Nom (pas null-terminated!) */
} __attribute__((packed)) ext2_dir_entry_t;

/* ===========================================
 * Structure de contexte Ext2
 * =========================================== */
typedef struct ext2_fs {
    ext2_superblock_t superblock;
    ext2_group_desc_t* group_descs;
    uint32_t block_size;            /* Taille d'un bloc en octets */
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint32_t num_groups;
    uint32_t inode_size;
    void* device;                   /* Référence au périphérique */
} ext2_fs_t;

/* ===========================================
 * API Ext2
 * =========================================== */

/**
 * Enregistre le driver Ext2 dans le VFS.
 */
void ext2_init(void);

/**
 * Monte un système Ext2.
 */
int ext2_mount(vfs_mount_t* mount, void* device);

/**
 * Démonte un système Ext2.
 */
int ext2_unmount(vfs_mount_t* mount);

/**
 * Retourne le noeud racine d'un système Ext2 monté.
 */
vfs_node_t* ext2_get_root(vfs_mount_t* mount);

/**
 * Lit un inode Ext2.
 */
int ext2_read_inode(ext2_fs_t* fs, uint32_t inode_num, ext2_inode_t* inode);

/**
 * Lit un bloc de données.
 */
int ext2_read_block(ext2_fs_t* fs, uint32_t block_num, void* buffer);

#endif /* EXT2_H */
