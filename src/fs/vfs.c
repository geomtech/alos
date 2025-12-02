/* src/fs/vfs.c - Virtual File System Implementation */
#include "vfs.h"
#include "../mm/kheap.h"
#include "../kernel/klog.h"

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
    
    KLOG_INFO("VFS", "Virtual File System initialized");
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
    
    klog(LOG_INFO, "VFS", "Registered filesystem: ");
    klog(LOG_INFO, "VFS", fs->name);
    
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
        klog(LOG_ERROR, "VFS", "Unknown filesystem: ");
        klog(LOG_ERROR, "VFS", fs_name);
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
        KLOG_ERROR("VFS", "No free mount slots");
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
            klog(LOG_ERROR, "VFS", "Failed to mount ");
            klog(LOG_ERROR, "VFS", fs_name);
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
    
    klog(LOG_INFO, "VFS", "Mounted ");
    klog(LOG_INFO, "VFS", fs_name);
    klog(LOG_INFO, "VFS", " at ");
    klog(LOG_INFO, "VFS", path);
    
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
    if (path == NULL || path[0] == '\0') return -1;
    if (path[0] != '/') return -1;  /* Chemin absolu requis */
    
    /* Trouver le dernier '/' pour séparer le chemin parent du nom */
    int last_slash = -1;
    int i = 0;
    while (path[i] != '\0') {
        if (path[i] == '/') last_slash = i;
        i++;
    }
    
    if (last_slash < 0) return -1;
    
    /* Extraire le nom du nouveau fichier */
    const char* name = path + last_slash + 1;
    if (name[0] == '\0') return -1;  /* Pas de nom après le dernier / */
    
    /* Construire le chemin du parent */
    char parent_path[VFS_MAX_PATH];
    if (last_slash == 0) {
        /* Le parent est la racine */
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (i = 0; i < last_slash && i < VFS_MAX_PATH - 1; i++) {
            parent_path[i] = path[i];
        }
        parent_path[i] = '\0';
    }
    
    /* Résoudre le répertoire parent */
    vfs_node_t* parent = vfs_resolve_path(parent_path);
    if (parent == NULL) {
        KLOG_ERROR("VFS", "create: parent directory not found");
        return -1;
    }
    
    /* Vérifier que c'est un répertoire */
    if ((parent->type & VFS_DIRECTORY) == 0) {
        KLOG_ERROR("VFS", "create: parent is not a directory");
        return -1;
    }
    
    /* Vérifier que le callback create existe */
    if (parent->create == NULL) {
        KLOG_ERROR("VFS", "create: operation not supported");
        return -1;
    }
    
    /* Appeler le callback create du filesystem */
    return parent->create(parent, name, VFS_FILE);
}

int vfs_mkdir(const char* path)
{
    if (path == NULL || path[0] == '\0') return -1;
    if (path[0] != '/') return -1;  /* Chemin absolu requis */
    
    /* Trouver le dernier '/' pour séparer le chemin parent du nom */
    int last_slash = -1;
    int i = 0;
    while (path[i] != '\0') {
        if (path[i] == '/') last_slash = i;
        i++;
    }
    
    if (last_slash < 0) return -1;
    
    /* Extraire le nom du nouveau répertoire */
    const char* name = path + last_slash + 1;
    if (name[0] == '\0') return -1;  /* Pas de nom après le dernier / */
    
    /* Construire le chemin du parent */
    char parent_path[VFS_MAX_PATH];
    if (last_slash == 0) {
        /* Le parent est la racine */
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (i = 0; i < last_slash && i < VFS_MAX_PATH - 1; i++) {
            parent_path[i] = path[i];
        }
        parent_path[i] = '\0';
    }
    
    /* Résoudre le répertoire parent */
    vfs_node_t* parent = vfs_resolve_path(parent_path);
    if (parent == NULL) {
        KLOG_ERROR("VFS", "mkdir: parent directory not found");
        return -1;
    }
    
    /* Vérifier que c'est un répertoire */
    if ((parent->type & VFS_DIRECTORY) == 0) {
        KLOG_ERROR("VFS", "mkdir: parent is not a directory");
        return -1;
    }
    
    /* Vérifier que le callback mkdir existe */
    if (parent->mkdir == NULL) {
        KLOG_ERROR("VFS", "mkdir: operation not supported");
        return -1;
    }
    
    /* Appeler le callback mkdir du filesystem */
    return parent->mkdir(parent, name);
}

int vfs_unlink(const char* path)
{
    if (path == NULL || path[0] == '\0') return -1;
    
    /* Ne pas permettre la suppression de la racine */
    if (path[0] == '/' && path[1] == '\0') {
        KLOG_ERROR("VFS", "unlink: cannot remove root");
        return -1;
    }
    
    /* Trouver le dernier / pour séparer le chemin du parent et le nom */
    int last_slash = -1;
    int i = 0;
    while (path[i] != '\0') {
        if (path[i] == '/') last_slash = i;
        i++;
    }
    
    if (last_slash < 0) {
        KLOG_ERROR("VFS", "unlink: invalid path");
        return -1;
    }
    
    /* Extraire le nom du fichier/répertoire à supprimer */
    const char* name = path + last_slash + 1;
    if (name[0] == '\0') {
        KLOG_ERROR("VFS", "unlink: no name after last /");
        return -1;
    }
    
    /* Construire le chemin du parent */
    char parent_path[VFS_MAX_PATH];
    if (last_slash == 0) {
        /* Le parent est la racine */
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (i = 0; i < last_slash && i < VFS_MAX_PATH - 1; i++) {
            parent_path[i] = path[i];
        }
        parent_path[i] = '\0';
    }
    
    /* Résoudre le répertoire parent */
    vfs_node_t* parent = vfs_resolve_path(parent_path);
    if (parent == NULL) {
        KLOG_ERROR("VFS", "unlink: parent directory not found");
        return -1;
    }
    
    /* Vérifier que c'est un répertoire */
    if ((parent->type & VFS_DIRECTORY) == 0) {
        KLOG_ERROR("VFS", "unlink: parent is not a directory");
        return -1;
    }
    
    /* Vérifier que le callback unlink existe */
    if (parent->unlink == NULL) {
        KLOG_ERROR("VFS", "unlink: operation not supported");
        return -1;
    }
    
    /* Appeler le callback unlink du filesystem */
    return parent->unlink(parent, name);
}

int vfs_rmdir(const char* path)
{
    /* rmdir utilise le même mécanisme que unlink */
    /* La vérification que c'est un répertoire vide est faite dans le FS */
    return vfs_unlink(path);
}

/* ===========================================
 * Debug
 * =========================================== */

void vfs_debug(void)
{
    KLOG_DEBUG("VFS", "--- VFS Debug ---");
    
    klog(LOG_DEBUG, "VFS", "Registered filesystems:");
    vfs_filesystem_t* fs = registered_filesystems;
    while (fs != NULL) {
        klog(LOG_DEBUG, "VFS", fs->name);
        fs = fs->next;
    }
    
    klog(LOG_DEBUG, "VFS", "Mount points:");
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active) {
            klog(LOG_DEBUG, "VFS", mounts[i].path);
            klog(LOG_DEBUG, "VFS", mounts[i].fs->name);
        }
    }
}
