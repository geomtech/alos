/* src/fs/vfs.c - Virtual File System Implementation */
#include "vfs.h"
#include "../mm/kheap.h"
#include "../kernel/klog.h"
#include "../include/errno.h"
#include "../kernel/sync.h"
#include "../include/fcntl.h"

/* ===========================================
 * Variables globales
 * =========================================== */
static vfs_filesystem_t* registered_filesystems = NULL;
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static vfs_node_t* vfs_root = NULL;
static mutex_t creation_lock = MUTEX_INIT;

/* Dirent statique pour readdir (simplifié) */
static vfs_dirent_t current_dirent;
static int vfs_lookup_path_checked(const char*, int, vfs_node_t**);

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
    vfs_node_t* node = NULL;
    if (vfs_open_checked(path, flags, &node)) return NULL;
    return node;
}

int vfs_open_checked(const char* path, uint32_t flags, vfs_node_t** output)
{
    if (!output) return -EINVAL;
    *output = NULL;
    vfs_node_t* node = NULL;
    int error = vfs_lookup_path_checked(path, 0, &node);
    if (error) return error;
    if (node->open) {
        error = node->open(node, flags);
        if (error) {
            if (node != vfs_root) node->dispose(node);
            return error;
        }
    }
    node->refcount++;
    *output = node;
    return 0;
}

int vfs_close(vfs_node_t* node)
{
    if (node == NULL) return -1;
    
    if (node->close != NULL) {
        node->close(node);
    }
    
    if (node->refcount > 0) {
        node->refcount--;
        if (!node->refcount && node != vfs_root && node->dispose)
            node->dispose(node);
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

int vfs_readdir_checked(vfs_node_t* node, uint32_t index, vfs_dirent_t* entry)
{
    if (!node || !entry) return -EINVAL;
    if (node->type != VFS_DIRECTORY) return -ENOTDIR;
    if (!node->readdir_checked) return -ENOTSUP;
    return node->readdir_checked(node, index, entry);
}

int vfs_stat_node(vfs_node_t* node, struct stat* metadata)
{
    if (!node || !metadata) return -EINVAL;
    if (!node->stat) return -ENOTSUP;
    int result = node->stat(node, metadata);
    if (result) return result;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && node->mount == &mounts[i]) {
            metadata->st_dev = (uint64_t)i + 1;
            return 0;
        }
    }
    return -ENODEV;
}

static int vfs_lookup_at_checked(vfs_node_t* start, const char* path, int flags,
                                   vfs_node_t** output)
{
    if (!path || !output || (flags & ~ALOS_STAT_NOFOLLOW)) return -EINVAL;
    if (!path[0]) return -ENOENT;
    if (!vfs_root) return -ENODEV;
    if (path[0] != '/' && !start) return -EINVAL;
    vfs_node_t* current = path[0] == '/' ? vfs_root : start;
    int owned = 0, result = 0;
    const char* cursor = path + (path[0] == '/');
    while (*cursor) {
        if (*cursor == '/') { cursor++; continue; }
        if (current->type == VFS_SYMLINK) { result = -ENOTSUP; break; }
        if (current->type != VFS_DIRECTORY) { result = -ENOTDIR; break; }
        char name[VFS_MAX_NAME + 1];
        size_t length = 0;
        while (*cursor && *cursor != '/') {
            if (length == VFS_MAX_NAME) { result = -ENAMETOOLONG; break; }
            name[length++] = *cursor++;
        }
        if (result) break;
        name[length] = 0;
        if (!current->lookup_checked) { result = -ENOTSUP; break; }
        vfs_node_t* next = NULL;
        result = current->lookup_checked(current, name, &next);
        if (result) break;
        if (owned) current->dispose(current);
        current = next;
        owned = 1;
        if (*cursor == '/' && current->type != VFS_DIRECTORY) {
            result = current->type == VFS_SYMLINK ? -ENOTSUP : -ENOTDIR;
            break;
        }
    }
    if (!result && current->type == VFS_SYMLINK &&
        !(flags & ALOS_STAT_NOFOLLOW)) result = -ENOTSUP;
    if (result) {
        if (owned) current->dispose(current);
    } else *output = current;
    return result;
}

static int vfs_lookup_path_checked(const char* path, int flags, vfs_node_t** out)
{
    return vfs_lookup_at_checked(NULL, path, flags, out);
}

static void dispose_at_result(vfs_node_t* node, vfs_node_t* anchor)
{
    if (node && node != vfs_root && node != anchor) node->dispose(node);
}

int vfs_open_at_checked(vfs_node_t* anchor, const char* path, uint32_t flags,
                        vfs_node_t** output)
{
    if (!output) return -EINVAL;
    *output = NULL;
    vfs_node_t* node;
    int error = vfs_lookup_at_checked(anchor, path,
        (flags & O_NOFOLLOW) ? ALOS_STAT_NOFOLLOW : 0, &node);
    if (error) return error;
    if ((flags & O_NOFOLLOW) && node->type == VFS_SYMLINK) error = -ELOOP;
    else if (node->open) error = node->open(node, flags);
    if (error) { dispose_at_result(node, anchor); return error; }
    node->refcount++;
    *output = node;
    return 0;
}

int vfs_stat_at(vfs_node_t* anchor, const char* path, struct stat* output, int flags)
{
    vfs_node_t* node;
    int error = vfs_lookup_at_checked(anchor, path, flags, &node);
    if (error) return error;
    error = vfs_stat_node(node, output);
    dispose_at_result(node, anchor);
    return error;
}

typedef struct {
    vfs_node_t* node;
    char* storage;
    const char* name;
    int trailing_slash;
} path_parent_t;

static int parent_at(vfs_node_t* anchor, const char* path, path_parent_t* result)
{
    size_t length = strlen(path);
    if (!length) return -ENOENT;
    if (length >= VFS_MAX_PATH) return -ENAMETOOLONG;
    size_t end = length;
    while (end > 1 && path[end - 1] == '/') --end;
    if (end == 1 && path[0] == '/') return -EBUSY;
    result->trailing_slash = end != length;
    result->storage = kmalloc(length + 1);
    if (!result->storage) return -ENOMEM;
    for (size_t i = 0; i <= length; ++i) result->storage[i] = path[i];
    result->storage[end] = 0;
    char* slash = NULL;
    for (char* p = result->storage; *p; ++p) if (*p == '/') slash = p;
    int error = 0;
    if (!slash) {
        result->node = anchor;
        result->name = result->storage;
        if (!anchor) error = -EINVAL;
    } else {
        result->name = slash + 1;
        if (slash == result->storage)
            error = vfs_lookup_at_checked(NULL, "/", 0, &result->node);
        else {
            *slash = 0;
            error = vfs_lookup_at_checked(anchor, result->storage, 0, &result->node);
        }
    }
    if (!error && result->node->type != VFS_DIRECTORY) error = -ENOTDIR;
    if (!error && strlen(result->name) > VFS_MAX_NAME) error = -ENAMETOOLONG;
    if (!error && (!strcmp(result->name, ".") || !strcmp(result->name, "..")))
        error = -EINVAL;
    if (error) {
        if (result->node) dispose_at_result(result->node, anchor);
        kfree(result->storage);
        result->storage = NULL;
    }
    return error;
}

static void parent_done(vfs_node_t* anchor, path_parent_t* parent)
{
    dispose_at_result(parent->node, anchor);
    kfree(parent->storage);
}

int vfs_create_at(vfs_node_t* anchor, const char* path, uint32_t type, uint32_t mode)
{
    if (type != VFS_FILE && type != VFS_DIRECTORY) return -EINVAL;
    if (mutex_lock(&creation_lock)) return -EIO;
    path_parent_t parent = {0};
    int error = parent_at(anchor, path, &parent);
    if (!error) {
        if (parent.trailing_slash && type != VFS_DIRECTORY) {
            parent_done(anchor, &parent);
            mutex_unlock(&creation_lock);
            return -ENOTDIR;
        }
        struct stat metadata = {0};
        error = vfs_stat_node(parent.node, &metadata);
        if (!error && !metadata.st_nlink) error = -ENOENT;
        vfs_node_t* existing = NULL;
        if (!error) error = parent.node->lookup_checked
            ? parent.node->lookup_checked(parent.node, parent.name, &existing) : -ENOTSUP;
        if (!error) { existing->dispose(existing); error = -EEXIST; }
        else if (error == -ENOENT && metadata.st_nlink) {
            error = parent.node->create
                ? parent.node->create(parent.node, parent.name,
                                      type | ((mode & 0777) << 16)) : -ENOTSUP;
            if (error == -1) error = -EIO;
        }
        parent_done(anchor, &parent);
    }
    mutex_unlock(&creation_lock);
    return error;
}

int vfs_unlink_at(vfs_node_t* anchor, const char* path, int flags)
{
    if (flags & ~AT_REMOVEDIR) return -EINVAL;
    if (mutex_lock(&creation_lock)) return -EIO;
    path_parent_t parent = {0};
    int error = parent_at(anchor, path, &parent);
    if (!error) {
        vfs_node_t* target = NULL;
        error = parent.node->lookup_checked
            ? parent.node->lookup_checked(parent.node, parent.name, &target) : -ENOTSUP;
        if (!error) {
            if (((flags & AT_REMOVEDIR) || parent.trailing_slash) &&
                target->type != VFS_DIRECTORY) error = -ENOTDIR;
            else if (!(flags & AT_REMOVEDIR) && target->type == VFS_DIRECTORY) error = -EISDIR;
            target->dispose(target);
            if (!error) {
                error = parent.node->unlink
                    ? parent.node->unlink(parent.node, parent.name) : -ENOTSUP;
                if (error == -1) error = -EIO;
            }
        }
        parent_done(anchor, &parent);
    }
    mutex_unlock(&creation_lock);
    return error;
}

int vfs_readlink_at(vfs_node_t* anchor, const char* path, char* buffer, uint32_t size)
{
    vfs_node_t* node;
    int result = vfs_lookup_at_checked(anchor, path, ALOS_STAT_NOFOLLOW, &node);
    if (result) return result;
    result = node->type != VFS_SYMLINK ? -EINVAL :
        node->readlink ? node->readlink(node, buffer, size) : -ENOTSUP;
    dispose_at_result(node, anchor);
    return result;
}

int vfs_symlink_at(vfs_node_t* anchor, const char* path, const char* target)
{
    if (!target[0]) return -ENOENT;
    if (mutex_lock(&creation_lock)) return -EIO;
    path_parent_t parent = {0};
    int error = parent_at(anchor, path, &parent);
    if (!error) {
        if (parent.trailing_slash) {
            parent_done(anchor, &parent);
            mutex_unlock(&creation_lock);
            return -ENOTDIR;
        }
        vfs_node_t* existing = NULL;
        error = parent.node->lookup_checked
            ? parent.node->lookup_checked(parent.node, parent.name, &existing) : -ENOTSUP;
        if (!error) { existing->dispose(existing); error = -EEXIST; }
        else if (error == -ENOENT) error = parent.node->symlink
            ? parent.node->symlink(parent.node, parent.name, target) : -ENOTSUP;
        parent_done(anchor, &parent);
    }
    mutex_unlock(&creation_lock);
    return error;
}

int vfs_rename_at(vfs_node_t* anchor, const char* from, const char* to)
{
    if (mutex_lock(&creation_lock)) return -EIO;
    path_parent_t old = {0}, destination = {0};
    int error = parent_at(anchor, from, &old);
    if (!error) {
        error = parent_at(anchor, to, &destination);
        if (!error) {
            error = old.trailing_slash || destination.trailing_slash ? -ENOTSUP :
                old.node->mount != destination.node->mount ? -EXDEV :
                old.node->rename ? old.node->rename(old.node, old.name,
                                                    destination.node, destination.name)
                                 : -ENOTSUP;
            parent_done(anchor, &destination);
        }
        parent_done(anchor, &old);
    }
    mutex_unlock(&creation_lock);
    return error;
}

int vfs_stat_path(const char* path, struct stat* metadata, int flags)
{
    if (!metadata) return -EINVAL;
    vfs_node_t* node = NULL;
    int error = vfs_lookup_path_checked(path, flags, &node);
    if (error) return error;
    error = vfs_stat_node(node, metadata);
    if (node != vfs_root) node->dispose(node);
    return error;
}

int vfs_statvfs_path(const char* path, struct statvfs* information)
{
    if (!information) return -EINVAL;
    vfs_node_t* node = NULL;
    int error = vfs_lookup_path_checked(path, 0, &node);
    if (error) return error;
    if (!node->statvfs) error = -ENOTSUP;
    else error = node->statvfs(node, information);
    if (!error) {
        error = -ENODEV;
        for (int i = 0; i < VFS_MAX_MOUNTS; ++i) {
            if (mounts[i].active && node->mount == &mounts[i]) {
                information->f_fsid = (uint64_t)i + 1;
                error = 0;
                break;
            }
        }
    }
    if (node != vfs_root) node->dispose(node);
    return error;
}

vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name)
{
    if (node == NULL || name == NULL) return NULL;
    if ((node->type & VFS_DIRECTORY) == 0) return NULL;
    if (node->finddir == NULL) return NULL;
    
    return node->finddir(node, name);
}

int vfs_access(const char* path, int mode)
{
    if (mode & ~7) return -EINVAL;
    struct stat metadata;
    int error = vfs_stat_path(path, &metadata, 0);
    if (error) return error;
    /* ALOS n'a pas encore de controle d'identite/autorisation. */
    return mode ? -ENOTSUP : 0;
}

static int create_checked_locked(const char* path, uint32_t type, uint32_t mode)
{
    if (!path || path[0] != '/' || (type != VFS_FILE && type != VFS_DIRECTORY))
        return -EINVAL;
    size_t length = strlen(path);
    if (!length) return -ENOENT;
    if (length >= VFS_MAX_PATH) return -ENAMETOOLONG;
    const char* name = NULL;
    for (const char* c = path; *c; c++) if (*c == '/') name = c;
    if (!name || !name[1]) return -EINVAL;
    name++;
    if (strlen(name) > VFS_MAX_NAME) return -ENAMETOOLONG;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EEXIST;
    char* parent_path = kmalloc(length + 1);
    if (!parent_path) return -ENOMEM;
    size_t parent_length = (size_t)(name - path - 1);
    if (!parent_length) parent_length = 1;
    for (size_t i = 0; i < parent_length; i++) parent_path[i] = path[i];
    parent_path[parent_length] = 0;
    vfs_node_t* parent = NULL;
    int error = vfs_lookup_path_checked(parent_path, 0, &parent);
    kfree(parent_path);
    if (error) return error;
    if (parent->type != VFS_DIRECTORY) error = -ENOTDIR;
    else if (!parent->create || !parent->lookup_checked) error = -ENOTSUP;
    else {
        vfs_node_t* existing = NULL;
        error = parent->lookup_checked(parent, name, &existing);
        if (!error) {
            existing->dispose(existing);
            error = -EEXIST;
        } else if (error == -ENOENT) {
            error = parent->create(parent, name, type | ((mode & 0777) << 16));
            if (error == -1) error = -EIO;
        }
    }
    if (parent != vfs_root) parent->dispose(parent);
    return error;
}

int vfs_create_checked(const char* path, uint32_t type, uint32_t mode)
{
    if (mutex_lock(&creation_lock)) return -EIO;
    int error = create_checked_locked(path, type, mode);
    mutex_unlock(&creation_lock);
    return error;
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
    return parent->create(parent, name, VFS_FILE | (0644 << 16));
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
