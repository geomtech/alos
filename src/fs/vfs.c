/* src/fs/vfs.c - Virtual File System Implementation */
#include "vfs.h"
#include "../mm/kheap.h"
#include "../kernel/console.h"

/* ===========================================
 * Variables globales
 * =========================================== */
static vfs_filesystem_t* registered_filesystems = NULL;
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static vfs_node_t* vfs_root = NULL;

/* Dirent statique pour readdir (simplifié) */
static vfs_dirent_t current_dirent;

/* ===========================================
 * Fonctions utilitaires
 * =========================================== */

static int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
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

static char* strcpy(char* dest, const char* src)
{
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

static size_t strlen(const char* s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* ===========================================
 * Initialisation
 * =========================================== */

void vfs_init(void)
{
    /* Initialiser les points de montage */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = 0;
    }
    
    registered_filesystems = NULL;
    vfs_root = NULL;
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("\n=== Virtual File System ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    console_puts("[VFS] Initialized\n");
}

/* ===========================================
 * Enregistrement des FS
 * =========================================== */

int vfs_register_fs(vfs_filesystem_t* fs)
{
    if (fs == NULL) return -1;
    
    /* Ajouter en tête de liste */
    fs->next = registered_filesystems;
    registered_filesystems = fs;
    
    console_puts("[VFS] Registered filesystem: ");
    console_puts(fs->name);
    console_puts("\n");
    
    return 0;
}

static vfs_filesystem_t* vfs_find_fs(const char* name)
{
    vfs_filesystem_t* fs = registered_filesystems;
    while (fs != NULL) {
        if (strcmp(fs->name, name) == 0) {
            return fs;
        }
        fs = fs->next;
    }
    return NULL;
}

/* ===========================================
 * Montage
 * =========================================== */

int vfs_mount(const char* path, const char* fs_name, void* device)
{
    /* Trouver le système de fichiers */
    vfs_filesystem_t* fs = vfs_find_fs(fs_name);
    if (fs == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[VFS] Unknown filesystem: ");
        console_puts(fs_name);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return -1;
    }
    
    /* Trouver un slot libre */
    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[VFS] No free mount slots\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return -1;
    }
    
    /* Configurer le point de montage */
    vfs_mount_t* mount = &mounts[slot];
    strcpy(mount->path, path);
    mount->fs = fs;
    mount->device = device;
    mount->active = 1;
    
    /* Appeler le callback de montage du FS */
    if (fs->mount != NULL) {
        if (fs->mount(mount, device) != 0) {
            mount->active = 0;
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            console_puts("[VFS] Failed to mount ");
            console_puts(fs_name);
            console_puts(" at ");
            console_puts(path);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            return -1;
        }
    }
    
    /* Obtenir le noeud racine */
    if (fs->get_root != NULL) {
        mount->root = fs->get_root(mount);
    }
    
    /* Si c'est le montage racine, mettre à jour vfs_root */
    if (strcmp(path, "/") == 0) {
        vfs_root = mount->root;
    }
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[VFS] Mounted ");
    console_puts(fs_name);
    console_puts(" at ");
    console_puts(path);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    return 0;
}

int vfs_unmount(const char* path)
{
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && strcmp(mounts[i].path, path) == 0) {
            /* Appeler le callback de démontage */
            if (mounts[i].fs->unmount != NULL) {
                mounts[i].fs->unmount(&mounts[i]);
            }
            mounts[i].active = 0;
            
            if (strcmp(path, "/") == 0) {
                vfs_root = NULL;
            }
            
            return 0;
        }
    }
    return -1;
}

/* ===========================================
 * Résolution de chemin
 * =========================================== */

vfs_node_t* vfs_get_root(void)
{
    return vfs_root;
}

vfs_mount_t* vfs_get_root_mount(void)
{
    /* Chercher le montage racine "/" */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && mounts[i].path[0] == '/' && mounts[i].path[1] == '\0') {
            return &mounts[i];
        }
    }
    return NULL;
}

vfs_node_t* vfs_resolve_path(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    
    /* Chemin absolu requis */
    if (path[0] != '/') {
        return NULL;
    }
    
    /* Cas spécial: racine */
    if (path[0] == '/' && path[1] == '\0') {
        return vfs_root;
    }
    
    if (vfs_root == NULL) {
        return NULL;
    }
    
    vfs_node_t* current = vfs_root;
    const char* p = path + 1;  /* Sauter le '/' initial */
    char component[VFS_MAX_NAME + 1];
    
    while (*p != '\0') {
        /* Extraire le prochain composant du chemin */
        int i = 0;
        while (*p != '\0' && *p != '/' && i < VFS_MAX_NAME) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        /* Sauter les '/' consécutifs */
        while (*p == '/') p++;
        
        /* Si composant vide, continuer */
        if (component[0] == '\0') {
            continue;
        }
        
        /* Chercher ce composant dans le répertoire courant */
        if (current->finddir == NULL) {
            return NULL;
        }
        
        vfs_node_t* next = current->finddir(current, component);
        if (next == NULL) {
            return NULL;  /* Composant non trouvé */
        }
        
        current = next;
    }
    
    return current;
}

/* ===========================================
 * Opérations sur fichiers
 * =========================================== */

vfs_node_t* vfs_open(const char* path, uint32_t flags)
{
    vfs_node_t* node = vfs_resolve_path(path);
    if (node == NULL) {
        return NULL;
    }
    
    /* Appeler le callback open si disponible */
    if (node->open != NULL) {
        if (node->open(node, flags) != 0) {
            return NULL;
        }
    }
    
    node->refcount++;
    return node;
}

int vfs_close(vfs_node_t* node)
{
    if (node == NULL) return -1;
    
    if (node->close != NULL) {
        node->close(node);
    }
    
    if (node->refcount > 0) {
        node->refcount--;
    }
    
    return 0;
}

int vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer)
{
    if (node == NULL || buffer == NULL) return -1;
    if (node->read == NULL) return -1;
    
    return node->read(node, offset, size, buffer);
}

int vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer)
{
    if (node == NULL || buffer == NULL) return -1;
    if (node->write == NULL) return -1;
    
    return node->write(node, offset, size, buffer);
}

/* ===========================================
 * Opérations sur répertoires
 * =========================================== */

vfs_dirent_t* vfs_readdir(vfs_node_t* node, uint32_t index)
{
    if (node == NULL) return NULL;
    if ((node->type & VFS_DIRECTORY) == 0) return NULL;
    if (node->readdir == NULL) return NULL;
    
    return node->readdir(node, index);
}

vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name)
{
    if (node == NULL || name == NULL) return NULL;
    if ((node->type & VFS_DIRECTORY) == 0) return NULL;
    if (node->finddir == NULL) return NULL;
    
    return node->finddir(node, name);
}

int vfs_create(const char* path)
{
    /* TODO: Implémenter création de fichier */
    (void)path;
    return -1;
}

int vfs_mkdir(const char* path)
{
    /* TODO: Implémenter création de dossier */
    (void)path;
    return -1;
}

int vfs_unlink(const char* path)
{
    /* TODO: Implémenter suppression */
    (void)path;
    return -1;
}

/* ===========================================
 * Debug
 * =========================================== */

void vfs_debug(void)
{
    console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
    console_puts("\n--- VFS Debug ---\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    console_puts("Registered filesystems: ");
    vfs_filesystem_t* fs = registered_filesystems;
    while (fs != NULL) {
        console_puts(fs->name);
        if (fs->next) console_puts(", ");
        fs = fs->next;
    }
    console_puts("\n");
    
    console_puts("Mount points:\n");
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active) {
            console_puts("  ");
            console_puts(mounts[i].path);
            console_puts(" -> ");
            console_puts(mounts[i].fs->name);
            console_puts("\n");
        }
    }
}
