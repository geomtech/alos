/* src/kernel/linux_compat.c - Linux Binary Compatibility Layer Implementation */
#include "linux_compat.h"
#include "console.h"
#include "klog.h"
#include "process.h"
#include "syscall.h"
#include "../fs/vfs.h"
#include "../fs/file.h"
#include "../mm/kheap.h"
#include "../include/string.h"

/* Mode de compatibilité Linux (par processus) */
static int linux_mode_enabled = 0;

/* ========================================
 * Fonctions utilitaires
 * ======================================== */

/**
 * Convertit les flags open Linux vers notre format interne.
 */
static uint32_t linux_to_native_flags(uint32_t linux_flags)
{
    uint32_t native_flags = 0;
    
    if ((linux_flags & 3) == LINUX_O_RDONLY) native_flags |= O_RDONLY;
    if ((linux_flags & 3) == LINUX_O_WRONLY) native_flags |= O_WRONLY;
    if ((linux_flags & 3) == LINUX_O_RDWR)   native_flags |= O_RDWR;
    if (linux_flags & LINUX_O_CREAT)         native_flags |= O_CREAT;
    if (linux_flags & LINUX_O_TRUNC)         native_flags |= O_TRUNC;
    if (linux_flags & LINUX_O_APPEND)        native_flags |= O_APPEND;
    
    return native_flags;
}

/**
 * Convertit un code d'erreur interne en errno Linux (négatif).
 */
static int32_t native_to_linux_errno(int native_error)
{
    /* Pour l'instant, on retourne simplement des valeurs négatives */
    if (native_error < 0) return native_error;
    if (native_error == 0) return 0;
    return -native_error;
}

/* ========================================
 * Implémentation des syscalls Linux
 * ======================================== */

/**
 * sys_exit - Terminer le processus
 */
static int32_t linux_sys_exit(int status)
{
    syscall_do_exit(status);
    return 0; /* Ne devrait jamais arriver ici */
}

/**
 * sys_read - Lire depuis un file descriptor
 */
static int32_t linux_sys_read(int fd, void* buf, uint32_t count)
{
    int result = syscall_do_read(fd, buf, count);
    return native_to_linux_errno(result);
}

/**
 * sys_write - Écrire vers un file descriptor
 */
static int32_t linux_sys_write(int fd, const void* buf, uint32_t count)
{
    int result = syscall_do_write(fd, buf, count);
    return native_to_linux_errno(result);
}

/**
 * sys_open - Ouvrir un fichier
 */
static int32_t linux_sys_open(const char* path, uint32_t flags, uint32_t mode)
{
    (void)mode;
    
    /* Convertir les flags */
    uint32_t native_flags = linux_to_native_flags(flags);
    
    int result = syscall_do_open(path, native_flags);
    return native_to_linux_errno(result);
}

/**
 * sys_close - Fermer un file descriptor
 */
static int32_t linux_sys_close(int fd)
{
    int result = syscall_do_close(fd);
    return native_to_linux_errno(result);
}

/**
 * sys_getpid - Obtenir le PID du processus courant
 */
static int32_t linux_sys_getpid(void)
{
    return syscall_do_getpid();
}

/**
 * sys_brk - Changer la limite du segment de données
 * Simple implémentation : on ignore pour l'instant
 */
static int32_t linux_sys_brk(void* addr)
{
    /* Pour l'instant, on retourne simplement l'adresse demandée */
    /* Un vrai implémentation nécessiterait de gérer le heap userspace */
    return (int32_t)addr;
}

/**
 * sys_mmap/mmap2 - Mapper de la mémoire
 * Stub pour l'instant
 */
static int32_t linux_sys_mmap(void* addr, uint32_t length, int prot, int flags, 
                              int fd, uint32_t offset)
{
    (void)addr; (void)length; (void)prot; (void)flags; (void)fd; (void)offset;
    
    /* Pour un binaire statique simple, on peut souvent ignorer mmap */
    /* Retourner une erreur ou une adresse fictive */
    return -1; /* EPERM */
}

/**
 * sys_getcwd - Obtenir le répertoire courant
 */
static int32_t linux_sys_getcwd(char* buf, uint32_t size)
{
    int result = syscall_do_getcwd(buf, size);
    return native_to_linux_errno(result);
}

/**
 * sys_chdir - Changer de répertoire
 */
static int32_t linux_sys_chdir(const char* path)
{
    int result = syscall_do_chdir(path);
    return native_to_linux_errno(result);
}

/**
 * sys_mkdir - Créer un répertoire
 */
static int32_t linux_sys_mkdir(const char* path, uint32_t mode)
{
    (void)mode;
    
    int result = syscall_do_mkdir(path);
    return native_to_linux_errno(result);
}

/**
 * sys_uname - Obtenir les informations système
 */
static int32_t linux_sys_uname(struct linux_utsname* buf)
{
    if (buf == NULL) return -1;
    
    /* Remplir la structure avec nos informations */
    memset(buf, 0, sizeof(struct linux_utsname));
    strcpy(buf->sysname, "ALOS");
    strcpy(buf->nodename, "alos");
    strcpy(buf->release, "1.0.0");
    strcpy(buf->version, "#1 ALOS");
    strcpy(buf->machine, "i686");
    strcpy(buf->domainname, "(none)");
    
    return 0;
}

/**
 * sys_access - Vérifier les permissions d'accès à un fichier
 * Stub simplifié
 */
static int32_t linux_sys_access(const char* path, int mode)
{
    (void)mode;
    
    /* Pour simplifier, on dit que tous les fichiers existent */
    /* Une vraie implémentation vérifierait via VFS */
    vfs_node_t* node = vfs_open(path, VFS_O_RDONLY);
    if (node) {
        vfs_close(node);
        return 0;
    }
    return -2; /* ENOENT */
}

/**
 * sys_socketcall - Multiplexeur pour les opérations socket (Linux i386)
 */
static int32_t linux_sys_socketcall(int call, uint32_t* args)
{
    /* Les syscalls socket Linux i386 passent par un multiplexeur */
    /* On retourne un stub pour l'instant - l'implémentation complète 
     * nécessiterait d'exposer les fonctions syscall internes */
    (void)call;
    (void)args;
    
    /* Stub - non implémenté pour le moment */
    return -38; /* ENOSYS */
}

/**
 * Stubs pour syscalls non implémentés
 */
static int32_t linux_sys_stub(const char* name)
{
    (void)name;
    return -38; /* ENOSYS */
}

/* ========================================
 * Dispatcher principal
 * ======================================== */

int32_t linux_syscall_handler(syscall_regs_t* regs)
{
    uint64_t syscall_num = regs->rax;
    uint64_t arg1 = regs->rdi;
    uint64_t arg2 = regs->rsi;
    uint64_t arg3 = regs->rdx;
    uint64_t arg4 = regs->r10;
    uint64_t arg5 = regs->r8;
    
    /* Dispatcher selon le numéro de syscall */
    switch (syscall_num) {
        case LINUX_SYS_EXIT:
        case LINUX_SYS_EXIT_GROUP:
            return linux_sys_exit(arg1);
        
        case LINUX_SYS_READ:
            return linux_sys_read(arg1, (void*)arg2, arg3);
        
        case LINUX_SYS_WRITE:
            return linux_sys_write(arg1, (const void*)arg2, arg3);
        
        case LINUX_SYS_OPEN:
            return linux_sys_open((const char*)arg1, arg2, arg3);
        
        case LINUX_SYS_CLOSE:
            return linux_sys_close(arg1);
        
        case LINUX_SYS_GETPID:
            return linux_sys_getpid();
        
        case LINUX_SYS_BRK:
            return linux_sys_brk((void*)arg1);
        
        case LINUX_SYS_MMAP:
        case LINUX_SYS_MMAP2:
            return linux_sys_mmap((void*)arg1, arg2, arg3, arg4, arg5, 
                                  regs->r9); /* 6ème arg sur la stack */
        
        case LINUX_SYS_GETCWD:
            return linux_sys_getcwd((char*)arg1, arg2);
        
        case LINUX_SYS_CHDIR:
            return linux_sys_chdir((const char*)arg1);
        
        case LINUX_SYS_MKDIR:
            return linux_sys_mkdir((const char*)arg1, arg2);
        
        case LINUX_SYS_UNAME:
            return linux_sys_uname((struct linux_utsname*)arg1);
        
        case LINUX_SYS_ACCESS:
            return linux_sys_access((const char*)arg1, arg2);
        
        case LINUX_SYS_SOCKETCALL:
            return linux_sys_socketcall(arg1, (uint32_t*)arg2);
        
        /* Syscalls ignorés/stubs */
        case LINUX_SYS_FORK:
            return linux_sys_stub("fork");
        
        case LINUX_SYS_EXECVE:
            return linux_sys_stub("execve");
        
        case LINUX_SYS_GETUID:
        case LINUX_SYS_GETGID:
        case LINUX_SYS_GETEUID:
        case LINUX_SYS_GETEGID:
            return 0; /* Root user */
        
        case LINUX_SYS_IOCTL:
            return linux_sys_stub("ioctl");
        
        case LINUX_SYS_FCNTL:
            return linux_sys_stub("fcntl");
        
        default:
            return -38; /* ENOSYS */
    }
}

/* ========================================
 * Fonctions publiques
 * ======================================== */

void linux_compat_init(void)
{
    KLOG_INFO("LINUX", "Linux compatibility layer initialized");
    linux_mode_enabled = 0;
}

void linux_compat_set_mode(int enable)
{
    linux_mode_enabled = enable;
    
    if (enable) {
        KLOG_INFO("LINUX", "Linux compatibility mode ENABLED");
    } else {
        KLOG_INFO("LINUX", "Linux compatibility mode DISABLED");
    }
}

int linux_compat_is_active(void)
{
    return linux_mode_enabled;
}
