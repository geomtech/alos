/* src/fs/vfs.h - Virtual File System Layer */
#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "../include/fs_metadata.h"
#include "../include/statvfs.h"

/* ===========================================
 * Types de fichiers
 * =========================================== */
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_MOUNTPOINT  0x08

/* ===========================================
 * Flags pour open()
 * =========================================== */
#define VFS_O_RDONLY    0x0000
#define VFS_O_WRONLY    0x0001
#define VFS_O_RDWR      0x0002
#define VFS_O_APPEND    0x0008
#define VFS_O_CREAT     0x0200
#define VFS_O_TRUNC     0x0400

/* ===========================================
 * Whence pour lseek()
 * =========================================== */
#define VFS_SEEK_SET    0
#define VFS_SEEK_CUR    1
#define VFS_SEEK_END    2

/* ===========================================
 * Limites
 * =========================================== */
#define VFS_MAX_NAME    255
#define VFS_MAX_PATH    4096
#define VFS_MAX_MOUNTS  16

/* ===========================================
 * Structures forward declarations
 * =========================================== */
struct vfs_node;
struct vfs_dirent;
struct vfs_filesystem;
struct vfs_mount;

/* ===========================================
 * Types de callbacks pour les opérations FS
 * =========================================== */
typedef int (*read_fn)(struct vfs_node*, uint32_t offset, uint32_t size, uint8_t* buffer);
typedef int (*write_fn)(struct vfs_node*, uint32_t offset, uint32_t size, const uint8_t* buffer);
typedef int (*open_fn)(struct vfs_node*, uint32_t flags);
typedef int (*close_fn)(struct vfs_node*);
typedef struct vfs_dirent* (*readdir_fn)(struct vfs_node*, uint32_t index);
typedef struct vfs_node* (*finddir_fn)(struct vfs_node*, const char* name);
typedef int (*create_fn)(struct vfs_node* parent, const char* name, uint32_t type);
typedef int (*unlink_fn)(struct vfs_node* parent, const char* name);
typedef int (*mkdir_fn)(struct vfs_node* parent, const char* name);
typedef int (*stat_fn)(struct vfs_node*, struct stat*);
typedef int (*readdir_checked_fn)(struct vfs_node*, uint32_t, struct vfs_dirent*);
/* Un succes lookup_checked transfere un nouveau noeud au demandeur. */
typedef int (*lookup_checked_fn)(struct vfs_node*, const char*, struct vfs_node**);
typedef void (*dispose_fn)(struct vfs_node*);
typedef int (*sync_fn)(struct vfs_node*);
typedef int (*truncate_fn)(struct vfs_node*);
typedef int (*resize_fn)(struct vfs_node*, uint32_t);
typedef int (*statvfs_fn)(struct vfs_node*, struct statvfs*);
typedef int (*set_times_fn)(struct vfs_node*, uint32_t, uint32_t, uint32_t);
typedef int (*readlink_fn)(struct vfs_node*, char*, uint32_t);
typedef int (*symlink_fn)(struct vfs_node*, const char*, const char*);
typedef int (*rename_fn)(struct vfs_node*, const char*, struct vfs_node*, const char*);

/* ===========================================
 * Structure d'un noeud VFS (inode abstrait)
 * =========================================== */
typedef struct vfs_node {
    char name[VFS_MAX_NAME + 1];    /* Nom du fichier/dossier */
    uint32_t inode;                  /* Numéro d'inode (spécifique au FS) */
    uint32_t type;                   /* Type (fichier, dossier, etc.) */
    uint32_t permissions;            /* Permissions (rwx) */
    uint32_t uid;                    /* User ID */
    uint32_t gid;                    /* Group ID */
    uint32_t size;                   /* Taille en octets */
    uint32_t atime;                  /* Access time */
    uint32_t mtime;                  /* Modification time */
    uint32_t ctime;                  /* Creation time */
    
    /* Callbacks spécifiques au FS */
    read_fn read;
    write_fn write;
    open_fn open;
    close_fn close;
    readdir_fn readdir;
    finddir_fn finddir;
    create_fn create;
    unlink_fn unlink;
    mkdir_fn mkdir;
    stat_fn stat;
    readdir_checked_fn readdir_checked;
    lookup_checked_fn lookup_checked;
    dispose_fn dispose;
    sync_fn sync;
    truncate_fn truncate;
    resize_fn resize;
    statvfs_fn statvfs;
    set_times_fn set_times;
    readlink_fn readlink;
    symlink_fn symlink;
    rename_fn rename;
    
    /* Données privées du FS */
    void* fs_data;
    
    /* Référence au mount point */
    struct vfs_mount* mount;
    
    /* Compteur de références */
    uint32_t refcount;
} vfs_node_t;

/* ===========================================
 * Entrée de répertoire
 * =========================================== */
typedef struct vfs_dirent {
    char name[VFS_MAX_NAME + 1];
    uint32_t inode;
    uint32_t type;
} vfs_dirent_t;

/* ===========================================
 * Structure d'un système de fichiers
 * =========================================== */
typedef struct vfs_filesystem {
    char name[32];                   /* Nom du FS (ext2, fat32, etc.) */
    
    /* Callbacks de montage */
    int (*mount)(struct vfs_mount* mount, void* device);
    int (*unmount)(struct vfs_mount* mount);
    
    /* Obtenir le noeud racine */
    vfs_node_t* (*get_root)(struct vfs_mount* mount);
    
    /* Données privées du driver FS */
    void* driver_data;
    
    /* Liste chaînée des FS enregistrés */
    struct vfs_filesystem* next;
} vfs_filesystem_t;

/* ===========================================
 * Point de montage
 * =========================================== */
typedef struct vfs_mount {
    char path[VFS_MAX_PATH];         /* Chemin du point de montage */
    vfs_filesystem_t* fs;            /* Système de fichiers */
    vfs_node_t* root;                /* Noeud racine du FS monté */
    void* device;                    /* Périphérique (ex: numéro de disque) */
    void* fs_specific;               /* Données spécifiques au FS monté */
    int active;                      /* Mount actif? */
} vfs_mount_t;

/* ===========================================
 * API VFS - Initialisation
 * =========================================== */

/**
 * Initialise le VFS.
 */
void vfs_init(void);
int vfs_statvfs_path(const char* path, struct statvfs* information);

/**
 * Enregistre un système de fichiers.
 */
int vfs_register_fs(vfs_filesystem_t* fs);

/**
 * Monte un système de fichiers.
 * @param path     Chemin du point de montage
 * @param fs_name  Nom du FS à utiliser
 * @param device   Périphérique source
 * @return 0 si succès, -1 si erreur
 */
int vfs_mount(const char* path, const char* fs_name, void* device);

/**
 * Démonte un système de fichiers.
 */
int vfs_unmount(const char* path);

/* ===========================================
 * API VFS - Opérations sur fichiers
 * =========================================== */

/**
 * Ouvre un fichier par son chemin.
 * @return Pointeur vers le noeud ou NULL si erreur
 */
vfs_node_t* vfs_open(const char* path, uint32_t flags);
int vfs_open_checked(const char*, uint32_t, vfs_node_t**);

/**
 * Ferme un fichier.
 */
int vfs_close(vfs_node_t* node);

/**
 * Lit des données depuis un fichier.
 */
int vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);

/**
 * Écrit des données dans un fichier.
 */
int vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);

/* ===========================================
 * API VFS - Opérations sur répertoires
 * =========================================== */

/**
 * Lit une entrée de répertoire.
 * @param node   Noeud du répertoire
 * @param index  Index de l'entrée (0, 1, 2, ...)
 * @return Entrée ou NULL si fin du répertoire
 */
vfs_dirent_t* vfs_readdir(vfs_node_t* node, uint32_t index);
/* 1 entree, 0 fin normale, ou -errno ; aucune confusion EOF/erreur. */
int vfs_readdir_checked(vfs_node_t*, uint32_t, vfs_dirent_t*);
int vfs_stat_node(vfs_node_t*, struct stat*);
int vfs_stat_path(const char*, struct stat*, int flags);
int vfs_open_at_checked(vfs_node_t*, const char*, uint32_t, vfs_node_t**);
int vfs_stat_at(vfs_node_t*, const char*, struct stat*, int);
int vfs_unlink_at(vfs_node_t*, const char*, int);
int vfs_create_at(vfs_node_t*, const char*, uint32_t, uint32_t);
int vfs_readlink_at(vfs_node_t*, const char*, char*, uint32_t);
int vfs_symlink_at(vfs_node_t*, const char*, const char*);
int vfs_rename_at(vfs_node_t*, const char*, const char*);

/**
 * Cherche un fichier dans un répertoire.
 */
vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name);

/**
 * Crée un fichier.
 */
int vfs_create(const char* path);
int vfs_create_checked(const char* path, uint32_t type, uint32_t mode);
int vfs_access(const char* path, int mode);

/**
 * Crée un répertoire.
 */
int vfs_mkdir(const char* path);

/**
 * Supprime un fichier.
 */
int vfs_unlink(const char* path);

/**
 * Supprime un répertoire vide.
 */
int vfs_rmdir(const char* path);

/* ===========================================
 * API VFS - Utilitaires
 * =========================================== */

/**
 * Retourne le noeud racine du VFS.
 */
vfs_node_t* vfs_get_root(void);

/**
 * Retourne le point de montage racine.
 */
vfs_mount_t* vfs_get_root_mount(void);

/**
 * Résout un chemin en noeud VFS.
 */
vfs_node_t* vfs_resolve_path(const char* path);

/**
 * Affiche les informations de debug du VFS.
 */
void vfs_debug(void);

#endif /* VFS_H */
