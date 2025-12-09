/* src/kernel/syscall.c - System Calls Implementation */
#include "syscall.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/io.h"
#include "../fs/file.h"
#include "../fs/vfs.h"
#include "../include/string.h"
#include "../mm/kheap.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../net/core/net.h"
#include "../net/l4/tcp.h"
#include "../shell/shell.h"
#include "console.h"
#include "keyboard.h"
#include "klog.h"
#include "linux_compat.h"
#include "process.h"
#include "sync.h"
#include "thread.h"
#include "timer.h"

/* Macro pour activer/désactiver les interruptions */
static inline void enable_interrupts(void) { __asm__ volatile("sti"); }
static inline void disable_interrupts(void) { __asm__ volatile("cli"); }

/* ========================================
 * File Descriptor Table (per-process, simplifié)
 * Pour V1, on utilise une table globale allouée dynamiquement
 * ======================================== */

static file_descriptor_t *fd_table = NULL;
static int fd_table_initialized = 0;

/* Socket serveur global pour contourner le bug fd_table */
static tcp_socket_t *g_server_socket = NULL;
static int g_server_fd = -1;
static int g_server_closing = 0; /* Flag pour fermeture demandée par CTRL+D */

/**
 * Initialise la table des file descriptors.
 */
static void fd_table_init(void) {
  if (fd_table_initialized && fd_table != NULL)
    return;

  /* Allouer la table dynamiquement pour éviter les problèmes de .bss */
  if (fd_table == NULL) {
    extern void *kmalloc(size_t size);
    fd_table = (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t) * MAX_FD);
    if (fd_table == NULL) {
      console_puts("[SYSCALL] FATAL: Cannot allocate fd_table!\n");
      return;
    }

    KLOG_INFO_HEX("SYSCALL", "fd_table allocated at address: ",
                  (uint32_t)(uintptr_t)fd_table);

    /* Forcer le rechargement du TLB pour être sûr que les nouveaux mappings
     * sont visibles */
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3"
                     :
                     :
                     : "rax", "memory");
  }

  for (int i = 0; i < MAX_FD; i++) {
    fd_table[i].type = FILE_TYPE_NONE;
    fd_table[i].flags = 0;
    fd_table[i].position = 0;
    fd_table[i].socket = NULL;
    fd_table[i].ref_count = 0;
  }

  /* Réserver stdin, stdout, stderr comme console */
  fd_table[FD_STDIN].type = FILE_TYPE_CONSOLE;
  fd_table[FD_STDIN].flags = O_RDONLY;
  fd_table[FD_STDIN].ref_count = 1;

  fd_table[FD_STDOUT].type = FILE_TYPE_CONSOLE;
  fd_table[FD_STDOUT].flags = O_WRONLY;
  fd_table[FD_STDOUT].ref_count = 1;

  fd_table[FD_STDERR].type = FILE_TYPE_CONSOLE;
  fd_table[FD_STDERR].flags = O_WRONLY;
  fd_table[FD_STDERR].ref_count = 1;

  fd_table_initialized = 1;
}

/**
 * Alloue un nouveau file descriptor.
 * @return FD number, ou -1 si plus de place
 */
static int fd_alloc(void) {
  fd_table_init();

  /* Commencer à 3 (après stdin/stdout/stderr) */
  for (int i = 3; i < MAX_FD; i++) {
    if (fd_table[i].type == FILE_TYPE_NONE) {
      fd_table[i].ref_count = 1;
      KLOG_DEBUG_DEC("SYSCALL", "fd_alloc: found free fd ", i);
      return i;
    }
  }
  return -1;
}

/**
 * Libère un file descriptor.
 */
static void fd_free(int fd) {
  if (fd < 0 || fd >= MAX_FD)
    return;
  if (fd < 3)
    return; /* Ne pas libérer stdin/stdout/stderr */

  fd_table[fd].type = FILE_TYPE_NONE;
  fd_table[fd].flags = 0;
  fd_table[fd].position = 0;
  fd_table[fd].socket = NULL;
  fd_table[fd].ref_count = 0;
}

/* ========================================
 * Déclarations externes
 * ======================================== */

/* Handler ASM dans interrupts.s */
extern void syscall_handler_asm(void);

/* ========================================
 * Implémentation des Syscalls
 * ======================================== */

/**
 * SYS_EXIT (1) - Terminer le processus courant
 *
 * @param status  Code de sortie (dans EBX)
 */
static int sys_exit(int status) {
  KLOG_INFO("SYSCALL", "sys_exit called with status:");
  KLOG_INFO_HEX("SYSCALL", "  Exit code: ", (uint32_t)status);

  console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
  console_puts("\n[SYSCALL] Process exited with code: ");
  console_put_dec(status);
  console_puts("\n");
  console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

  /* Terminer proprement le thread/processus courant */
  thread_t *current = thread_current();
  if (current && current->owner) {
    /* Sauvegarder le code de sortie dans le processus */
    current->owner->exit_status = status;
    KLOG_INFO("SYSCALL", "Terminating user process thread");
  }

  /* Terminer le thread via le scheduler */
  /* thread_exit() ne retourne jamais */
  thread_exit(status);

  /* Ne devrait jamais arriver */
  while (1) {
    asm volatile("hlt");
  }

  return 0; /* Jamais atteint */
}

/**
 * SYS_NANOSLEEP (162) - Dormir pendant un temps spécifié
 *
 * @param milliseconds  Temps de sommeil en millisecondes (simplifié)
 * @return 0 si succès, -1 si interrompu
 *
 * Note: Implémentation simplifiée - accepte des ms au lieu de struct timespec
 */
static int sys_nanosleep(uint64_t milliseconds) {
  (void)milliseconds; /* Ignore for now - just yield */
  
  /* Simple yield to let other threads run */
  thread_t *current = thread_current();
  if (current) {
    /* Mark that we want to yield, scheduler will handle it */
    current->needs_yield = true;
  }
  return 0;
}

/**
 * SYS_WRITE (4) - Écrire une chaîne
 *
 * @param fd      File descriptor (ignoré pour l'instant, tout va à la console)
 * @param buf     Pointeur vers la chaîne (dans EBX)
 * @param count   Nombre de caractères (dans ECX), 0 = null-terminated
 */
static int sys_write(int fd, const char *buf, uint64_t count) {
  /* For now, only support stdout (fd=1) and stderr (fd=2) */
  if (fd != 1 && fd != 2) {
    return -1; /* Invalid file descriptor */
  }

  /* Safety check: ensure buffer is not NULL */
  if (!buf) {
    KLOG_ERROR("SYSCALL", "sys_write: NULL buffer");
    return -1;
  }

  /* Logging disabled for performance:
   * KLOG_INFO("SYSCALL", "sys_write called:");
   * KLOG_INFO_DEC("SYSCALL", "  fd: ", fd);
   * KLOG_INFO_DEC("SYSCALL", "  count: ", (uint32_t)count);
   */

  /* Write to console AND log */
  for (uint64_t i = 0; i < count; i++) {
    console_putc(buf[i]);
  }

  return (int)count;
}

/**
 * SYS_OPEN (5) - Ouvrir un fichier
 *
 * @param path   Chemin du fichier
 * @param flags  Flags d'ouverture (O_RDONLY, etc.)
 * @return File descriptor, ou -1 si erreur
 */
static int sys_open(const char *path, int flags) {
  fd_table_init();

  if (path == NULL) {
    return -1;
  }

  KLOG_INFO("SYSCALL", "sys_open called");
  KLOG_INFO("SYSCALL", "[SYSCALL] open:");
  KLOG_INFO("SYSCALL", path);

  /* Ouvrir le fichier via VFS */
  vfs_node_t *node = vfs_open(path, flags);
  if (node == NULL) {
    KLOG_ERROR("SYSCALL", "[SYSCALL] open: file not found");
    return -1;
  }

  /* Allouer un file descriptor */
  int fd = fd_alloc();
  if (fd < 0) {
    vfs_close(node);
    KLOG_ERROR("SYSCALL", "[SYSCALL] open: no free file descriptors");
    return -1;
  }

  /* Configurer le FD */
  fd_table[fd].type = FILE_TYPE_FILE;
  fd_table[fd].flags = flags;
  fd_table[fd].position = 0;
  fd_table[fd].vfs_node = node;

  KLOG_INFO_DEC("SYSCALL", "[SYSCALL] open: fd=", fd);
  KLOG_INFO_DEC("SYSCALL", "file size=", node->size);

  return fd;
}

/**
 * SYS_READ (3) - Lire depuis un fichier
 *
 * @param fd     File descriptor
 * @param buf    Buffer de destination
 * @param count  Nombre de bytes à lire
 * @return Nombre de bytes lus, ou -1 si erreur
 */
static int sys_read(int fd, void *buf, uint64_t count) {
  fd_table_init();

  if (buf == NULL || fd < 0 || fd >= MAX_FD) {
    return -1;
  }

  if (fd_table[fd].type == FILE_TYPE_NONE) {
    return -1;
  }

  /* Lecture depuis un fichier VFS */
  if (fd_table[fd].type == FILE_TYPE_FILE) {
    vfs_node_t *node = (vfs_node_t *)fd_table[fd].vfs_node;
    if (node == NULL) {
      return -1;
    }

    /* Lire depuis la position courante */
    int bytes_read =
        vfs_read(node, fd_table[fd].position, count, (uint8_t *)buf);
    if (bytes_read > 0) {
      fd_table[fd].position += bytes_read;
    }

    return bytes_read;
  }

  /* Lecture depuis la console (stdin) - non implémenté */
  if (fd_table[fd].type == FILE_TYPE_CONSOLE) {
    /* TODO: keyboard input */
    return 0;
  }

  return -1;
}

/**
 * SYS_GETPID (20) - Obtenir le PID du processus courant
 */
static int sys_getpid(void) {
  if (current_process != NULL) {
    return current_process->pid;
  }
  return -1;
}

/**
 * SYS_KBHIT (100) - Lire un caractère du clavier (non-bloquant)
 *
 * @return Le caractère lu, ou 0 si aucun caractère disponible
 */
static int sys_kbhit(void) { return (int)keyboard_getchar_nonblock(); }

/* ========================================
 * Filesystem Syscalls
 * ======================================== */

/* Répertoire courant global (pour V1, simplifié) */
static char current_working_dir[VFS_MAX_PATH] = "/";

/**
 * SYS_GETCWD (183) - Obtenir le répertoire courant
 *
 * @param buf   Buffer pour stocker le chemin
 * @param size  Taille du buffer
 * @return 0 si succès, -1 si erreur
 */
static int sys_getcwd(char *buf, uint64_t size) {
  if (buf == NULL || size == 0) {
    return -1;
  }

  uint32_t len = 0;
  while (current_working_dir[len] != '\0' && len < VFS_MAX_PATH) {
    len++;
  }

  if (len >= size) {
    return -1; /* Buffer trop petit */
  }

  for (uint32_t i = 0; i <= len; i++) {
    buf[i] = current_working_dir[i];
  }

  return 0;
}

/**
 * SYS_CHDIR (12) - Changer de répertoire courant
 *
 * @param path  Nouveau chemin (absolu ou relatif)
 * @return 0 si succès, -1 si erreur
 */
static int sys_chdir(const char *path) {
  if (path == NULL) {
    return -1;
  }

  char new_path[VFS_MAX_PATH];

  /* Construire le chemin absolu */
  if (path[0] == '/') {
    /* Chemin absolu */
    uint32_t i = 0;
    while (path[i] != '\0' && i < VFS_MAX_PATH - 1) {
      new_path[i] = path[i];
      i++;
    }
    new_path[i] = '\0';
  } else {
    /* Chemin relatif */
    uint32_t cwd_len = 0;
    while (current_working_dir[cwd_len] != '\0')
      cwd_len++;

    uint32_t i = 0;
    /* Copier le cwd */
    for (; i < cwd_len && i < VFS_MAX_PATH - 1; i++) {
      new_path[i] = current_working_dir[i];
    }
    /* Ajouter / si nécessaire */
    if (i > 0 && new_path[i - 1] != '/' && i < VFS_MAX_PATH - 1) {
      new_path[i++] = '/';
    }
    /* Ajouter le chemin relatif */
    uint32_t j = 0;
    while (path[j] != '\0' && i < VFS_MAX_PATH - 1) {
      new_path[i++] = path[j++];
    }
    new_path[i] = '\0';
  }

  /* Vérifier que le répertoire existe */
  vfs_node_t *node = vfs_resolve_path(new_path);
  if (node == NULL) {
    return -1;
  }

  if (!(node->type & VFS_DIRECTORY)) {
    return -1; /* Ce n'est pas un répertoire */
  }

  /* Mettre à jour le cwd */
  uint32_t i = 0;
  while (new_path[i] != '\0' && i < VFS_MAX_PATH - 1) {
    current_working_dir[i] = new_path[i];
    i++;
  }
  current_working_dir[i] = '\0';

  return 0;
}

/**
 * Structure pour READDIR userspace
 */
typedef struct {
  char name[256];
  uint32_t type;
  uint32_t size;
} userspace_dirent_t;

/**
 * SYS_READDIR (89) - Lire une entrée de répertoire
 *
 * @param path   Chemin du répertoire
 * @param index  Index de l'entrée
 * @param entry  Structure de sortie userspace
 * @return 0 si succès, 1 si fin de répertoire, -1 si erreur
 */
static int sys_readdir(const char *path, uint32_t index,
                       userspace_dirent_t *entry) {
  if (path == NULL || entry == NULL) {
    return -1;
  }

  vfs_node_t *dir = vfs_resolve_path(path);
  if (dir == NULL) {
    return -1;
  }

  if (!(dir->type & VFS_DIRECTORY)) {
    return -1;
  }

  vfs_dirent_t *dirent = vfs_readdir(dir, index);
  if (dirent == NULL) {
    return 1; /* Fin du répertoire */
  }

  /* Copier les infos */
  uint32_t i = 0;
  while (dirent->name[i] != '\0' && i < 255) {
    entry->name[i] = dirent->name[i];
    i++;
  }
  entry->name[i] = '\0';
  entry->type = dirent->type;

  /* Obtenir la taille du fichier */
  vfs_node_t *file_node = vfs_finddir(dir, dirent->name);
  entry->size = (file_node != NULL) ? file_node->size : 0;

  return 0;
}

/**
 * SYS_MKDIR (39) - Créer un répertoire
 *
 * @param path  Chemin du répertoire à créer
 * @return 0 si succès, -1 si erreur
 */
static int sys_mkdir(const char *path) {
  if (path == NULL) {
    return -1;
  }

  return vfs_mkdir(path);
}

/**
 * SYS_CREATE (85) - Créer un fichier
 *
 * @param path  Chemin du fichier à créer
 * @return 0 si succès, -1 si erreur
 */
static int sys_create(const char *path) {
  if (path == NULL) {
    return -1;
  }

  return vfs_create(path);
}

/**
 * SYS_CLEAR (101) - Effacer l'écran
 */
static int sys_clear(void) {
  console_clear(VGA_COLOR_BLACK);
  return 0;
}

/**
 * Structure pour les infos mémoire
 */
typedef struct {
  uint32_t total_size;
  uint32_t free_size;
  uint32_t block_count;
  uint32_t free_block_count;
} meminfo_t;

/**
 * SYS_MEMINFO (102) - Obtenir les informations mémoire
 *
 * @param info  Structure de sortie
 * @return 0 si succès, -1 si erreur
 */
static int sys_meminfo(meminfo_t *info) {
  if (info == NULL) {
    return -1;
  }

  info->total_size = (uint32_t)kheap_get_total_size();
  info->free_size = (uint32_t)kheap_get_free_size();
  info->block_count = (uint32_t)kheap_get_block_count();
  info->free_block_count = (uint32_t)kheap_get_free_block_count();

  return 0;
}

/* Helper externe depuis kernel.c */
extern struct limine_framebuffer *kernel_get_primary_framebuffer(void);

/**
 * SYS_GET_FRAMEBUFFER (110) - Obtenir les infos et mapper le framebuffer
 *
 * @param info  Pointeur vers la structure framebuffer_info_t
 * @return 0 si succès, -1 si erreur
 */
static int sys_get_framebuffer(framebuffer_info_t *info) {
  if (info == NULL) {
    return -1;
  }

  struct limine_framebuffer *fb = kernel_get_primary_framebuffer();
  if (!fb) {
    return -1;
  }

  /* Calculer la taille alignée sur les pages */
  uint64_t size = (uint64_t)fb->pitch * fb->height;
  uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

  /* Adresse virtuelle cible pour le framebuffer en userland */
  /* On choisit une adresse fixe (1.5GB) qui rentre dans 32 bits pour éviter
   * les problèmes de troncature */
  uint64_t vaddr_base = 0x60000000ULL;

  /* Mapper les pages */
  /* Limine donne l'adresse physique dans address (si mapped en hhdm, on peut
   * convertir) */
  /* Mais fb->address est l'adresse virtuelle HHDM ou physique ? */
  /* Limine spec: address is the virtual address of the framebuffer in the HHDM.
   */
  /* On doit convertir en physique pour vmm_map_page */

  uint64_t phys_base = vmm_virt_to_phys((void *)fb->address);
  /* Fallback: si vmm_virt_to_phys échoue ou si l'adresse n'est pas dans HHDM */
  /* (Ce qui ne devrait pas arriver avec Limine) */

  /* Debug log */
  KLOG_INFO_HEX("SYSCALL", "Mapping FB Phys: ", phys_base);
  KLOG_INFO_HEX("SYSCALL", "Mapping FB Virt: ", vaddr_base);
  KLOG_INFO_DEC("SYSCALL", "Pages: ", pages);

  /* Get the current process's page directory */
  process_t *proc = process_current();
  if (!proc || !proc->pml4) {
    KLOG_ERROR("SYSCALL", "sys_get_framebuffer: no process or pml4");
    return -1;
  }

  /* proc->pml4 is actually a page_directory_t* cast to uint64_t* */
  page_directory_t *user_dir = (page_directory_t *)proc->pml4;

  for (uint64_t i = 0; i < pages; i++) {
    uint64_t offset = i * PAGE_SIZE;
    /* PAGE_USER | PAGE_RW | PAGE_PRESENT | PAGE_WRITETHROUGH (bit 3) */
    vmm_map_page_in_dir(user_dir, phys_base + offset, vaddr_base + offset,
                        PAGE_USER | PAGE_RW | PAGE_PRESENT | PAGE_WRITETHROUGH);
  }

  /* Remplir la structure */
  info->addr = vaddr_base;
  info->width = (uint32_t)fb->width;
  info->height = (uint32_t)fb->height;
  info->pitch = (uint32_t)fb->pitch;
  info->bpp = (uint16_t)fb->bpp;
  info->red_mask_size = fb->red_mask_size;
  info->red_mask_shift = fb->red_mask_shift;
  info->green_mask_size = fb->green_mask_size;
  info->green_mask_shift = fb->green_mask_shift;
  info->blue_mask_size = fb->blue_mask_size;
  info->blue_mask_shift = fb->blue_mask_shift;

  return 0;
}

#include "input.h"

/**
 * SYS_GET_EVENT (111) - Obtenir un événement d'entrée
 * @return 1 si événement trouvé, 0 sinon
 */
static int sys_get_event(input_event_t *event) {
  if (event == NULL)
    return -1;
  return input_pop_event(event);
}

/* ========================================
 * Socket Syscalls
 * ======================================== */

/**
 * SYS_SOCKET (41) - Créer un socket
 *
 * @param domain    Famille d'adresses (AF_INET)
 * @param type      Type de socket (SOCK_STREAM pour TCP)
 * @param protocol  Protocole (IPPROTO_TCP ou 0)
 * @return File descriptor du socket, ou -1 si erreur
 */
static int sys_socket(int domain, int type, int protocol) {
  (void)protocol;

  KLOG_INFO("SYSCALL", "sys_socket called");
  KLOG_INFO_HEX("SYSCALL", "  domain: ", domain);
  KLOG_INFO_HEX("SYSCALL", "  type: ", type);

  /* Vérifier les paramètres */
  if (domain != AF_INET) {
    KLOG_ERROR_DEC("SYSCALL", "sys_socket: unsupported domain ", domain);
    return -1;
  }

  if (type != SOCK_STREAM) {
    KLOG_ERROR_DEC("SYSCALL", "sys_socket: unsupported type ", type);
    return -1;
  }

  /* Créer le socket TCP kernel (protected) */
  net_lock();
  tcp_socket_t *sock = tcp_socket_create();
  net_unlock();

  if (sock == NULL) {
    KLOG_ERROR("SYSCALL", "sys_socket: failed to create TCP socket");
    return -1;
  }

  /* Allouer un file descriptor */
  int fd = fd_alloc();
  if (fd < 0) {
    tcp_close(sock);
    KLOG_ERROR("SYSCALL", "sys_socket: no free file descriptors");
    return -1;
  }

  KLOG_DEBUG("SYSCALL", "sys_socket: fd allocated, setting up table...");

  /* Associer le socket au FD */
  fd_table[fd].type = FILE_TYPE_SOCKET;
  fd_table[fd].flags = O_RDWR;
  fd_table[fd].socket = sock;

  KLOG_DEBUG("SYSCALL", "sys_socket: table entry set");

  /* Sauvegarder globalement pour contourner le bug fd_table */
  g_server_socket = sock;
  g_server_fd = fd;
  g_server_closing = 0; /* Reset du flag de fermeture */

  KLOG_DEBUG_DEC("SYSCALL", "sys_socket: created fd (global socket saved) ",
                 fd);

  return fd;
}

/**
 * SYS_BIND (49) - Lier un socket à une adresse
 *
 * @param fd    File descriptor du socket
 * @param addr  Pointeur vers sockaddr_in
 * @param len   Taille de la structure (ignoré)
 * @return 0 si succès, -1 si erreur
 */
static int sys_bind(int fd, sockaddr_in_t *addr, int len) {
  (void)len;

  fd_table_init(); /* S'assurer que la table est initialisée */

  KLOG_INFO("SYSCALL", "sys_bind called");
  KLOG_INFO_HEX("SYSCALL", "  fd: ", fd);
  KLOG_INFO_HEX("SYSCALL", "  addr: ", (uint32_t)addr);

  /* Vérifier le FD */
  if (fd < 0 || fd >= MAX_FD) {
    KLOG_ERROR_DEC("SYSCALL", "sys_bind: invalid fd ", fd);
    return -1;
  }

  KLOG_DEBUG_DEC("SYSCALL",
                 "sys_bind: fd_table[fd].type = ", fd_table[fd].type);

  if (fd_table[fd].type != FILE_TYPE_SOCKET) {
    KLOG_ERROR("SYSCALL", "sys_bind: not a socket");
    return -1;
  }

  tcp_socket_t *sock = fd_table[fd].socket;
  if (sock == NULL) {
    KLOG_ERROR("SYSCALL", "sys_bind: socket is NULL");
    return -1;
  }

  /* Vérifier que addr est valide */
  if (addr == NULL) {
    KLOG_ERROR("SYSCALL", "sys_bind: addr is NULL");
    return -1;
  }

  /* Extraire le port (attention: network byte order) */
  uint16_t port = ntohs(addr->sin_port);

  KLOG_DEBUG_DEC("SYSCALL", "sys_bind: binding to port ", port);

  net_lock();
  int result = tcp_bind(sock, port);
  net_unlock();

  return result;
}

/**
 * SYS_LISTEN (50) - Mettre un socket en écoute
 *
 * @param fd       File descriptor du socket
 * @param backlog  Taille de la queue (ignoré pour V1)
 * @return 0 si succès, -1 si erreur
 */
static int sys_listen(int fd, int backlog) {
  (void)backlog;

  KLOG_INFO("SYSCALL", "sys_listen called");
  KLOG_INFO_HEX("SYSCALL", "  fd: ", fd);

  /* Vérifier le FD */
  if (fd < 0 || fd >= MAX_FD) {
    return -1;
  }

  if (fd_table[fd].type != FILE_TYPE_SOCKET) {
    KLOG_ERROR("SYSCALL", "sys_listen: not a socket");
    return -1;
  }

  tcp_socket_t *sock = fd_table[fd].socket;
  if (sock == NULL) {
    return -1;
  }

  /* Vérifier que le socket est bindé */
  if (sock->local_port == 0) {
    KLOG_ERROR("SYSCALL", "sys_listen: socket not bound");
    return -1;
  }

  /* Passer en mode LISTEN (protected) */
  net_lock();
  sock->state = TCP_STATE_LISTEN;
  net_unlock();

  KLOG_DEBUG_DEC("SYSCALL", "sys_listen: listening on port ", sock->local_port);

  KLOG_INFO("SYSCALL", "sys_listen returning 0");
  return 0;
}

/**
 * SYS_ACCEPT (43) - Accepter une connexion entrante (non-bloquant avec timeout)
 *
 * Modèle MULTI-SOCKET: le socket serveur reste TOUJOURS en LISTEN.
 * Les sockets clients sont créés automatiquement par tcp_handle_packet
 * quand un SYN arrive. Cette fonction trouve juste le socket client prêt.
 *
 * Polling non-bloquant avec timeout de 10 secondes (100 tentatives × 100ms).
 * Interruptible par CTRL+C ou CTRL+D.
 *
 * @param fd    File descriptor du socket en écoute
 * @param addr  Pointeur vers sockaddr_in pour l'adresse du client (peut être
 * NULL)
 * @param len   Pointeur vers la taille (ignoré)
 * @return NOUVEAU FD pour le socket client, ou -1 si
 * erreur/timeout/interruption
 */
/* Prédicat pour wait_queue: vérifie si un client est prêt */
static bool accept_client_ready(void *context) {
  tcp_socket_t *listen_sock = (tcp_socket_t *)context;
  return tcp_find_ready_client(listen_sock->local_port) != NULL;
}

static int sys_accept(int fd, sockaddr_in_t *addr, int *len) {
  (void)len;

  /* Vérifier le FD du socket serveur */
  if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FILE_TYPE_SOCKET) {
    return -1;
  }

  tcp_socket_t *listen_sock = fd_table[fd].socket;
  if (listen_sock == NULL || listen_sock->state != TCP_STATE_LISTEN) {
    return -1;
  }

  /* Chercher un client prêt SANS bloquer */
  tcp_socket_t *client_sock = tcp_find_ready_client(listen_sock->local_port);

  if (client_sock == NULL) {
    /* Pas de connexion prête - attendre avec wait_queue (IRQ-safe) */
    KLOG_DEBUG("SYSCALL", "sys_accept: no client ready, waiting...");

    /* Attendre avec timeout de 10 secondes */
    bool got_client =
        wait_queue_wait_timeout(&listen_sock->accept_waitqueue,
                                accept_client_ready, listen_sock, 10000);
    if (!got_client) {
      KLOG_DEBUG("SYSCALL", "sys_accept: timeout waiting for connection");
      return -1;
    }

    /* Re-chercher le client après le réveil */
    client_sock = tcp_find_ready_client(listen_sock->local_port);
    if (client_sock == NULL) {
      KLOG_DEBUG("SYSCALL", "sys_accept: spurious wakeup, no client");
      return -1;
    }
  }

  /* Allouer un nouveau FD pour le socket client */
  int client_fd = fd_alloc();
  if (client_fd < 0) {
    KLOG_ERROR("SYSCALL", "sys_accept: no free fd");
    net_lock();
    tcp_close(client_sock);
    net_unlock();
    return -1;
  }

  /* Configurer le FD */
  fd_table[client_fd].type = FILE_TYPE_SOCKET;
  fd_table[client_fd].socket = client_sock;
  fd_table[client_fd].flags = O_RDWR;

  /* Remplir l'adresse du client si demandé */
  if (addr != NULL) {
    addr->sin_family = AF_INET;
    addr->sin_port = htons(client_sock->remote_port);
    addr->sin_addr = ((uint32_t)client_sock->remote_ip[0]) |
                     ((uint32_t)client_sock->remote_ip[1] << 8) |
                     ((uint32_t)client_sock->remote_ip[2] << 16) |
                     ((uint32_t)client_sock->remote_ip[3] << 24);
  }

  KLOG_DEBUG_DEC("SYSCALL", "sys_accept: new client fd ", client_fd);

  /* Retourner le NOUVEAU FD client (pas le FD serveur!) */
  return client_fd;
}

/**
 * SYS_RECV (45) - Recevoir des données depuis un socket
 * Utilise le FD pour trouver le socket (modèle multi-socket).
 */
static int sys_recv(int fd, uint8_t *buf, int len, int flags) {
  (void)flags;

  /* Vérifier le FD */
  if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FILE_TYPE_SOCKET) {
    return -1;
  }

  tcp_socket_t *sock = fd_table[fd].socket;
  if (sock == NULL || sock->state != TCP_STATE_ESTABLISHED) {
    return 0;
  }

  /* Attente des données avec sleep pour permettre aux IRQ de s'exécuter.
   * On ne prend PAS le lock pendant l'attente pour éviter les deadlocks
   * avec l'IRQ réseau qui traite les paquets entrants.
   */
  while (tcp_available(sock) == 0) {
    if (sock->state != TCP_STATE_ESTABLISHED) {
      return 0;
    }
    /* Attendre les données (IRQ-safe wait queue) */
    wait_queue_wait(&sock->recv_waitqueue, NULL, NULL);
  }

  /* Prendre le lock seulement pour la lecture des données */
  net_lock();
  int n = tcp_recv(sock, buf, len);
  net_unlock();

  return n;
}

/**
 * SYS_SEND (44) - Envoyer des données via un socket
 * Utilise le FD pour trouver le socket (modèle multi-socket).
 */
static int sys_send(int fd, const uint8_t *buf, int len, int flags) {
  (void)flags;

  /* Vérifier le FD */
  if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FILE_TYPE_SOCKET) {
    return -1;
  }

  tcp_socket_t *sock = fd_table[fd].socket;
  if (sock == NULL) {
    return -1;
  }

  return tcp_send(sock, buf, len);
}

/**
 * SYS_CLOSE (6) - Fermer un file descriptor
 *
 * @param fd  File descriptor à fermer
 * @return 0 si succès, -1 si erreur
 */
static int sys_close(int fd) {
  KLOG_INFO("SYSCALL", "sys_close called");
  KLOG_INFO_HEX("SYSCALL", "  fd: ", fd);

  /* Vérifier le FD */
  if (fd < 0 || fd >= MAX_FD) {
    return -1;
  }

  /* Ne pas fermer stdin/stdout/stderr */
  if (fd < 3) {
    return -1;
  }

  if (fd_table == NULL || fd_table[fd].type == FILE_TYPE_NONE) {
    return -1;
  }

  /* Si c'est le socket serveur global - fermeture CTRL+D uniquement */
  if (fd == g_server_fd && g_server_socket != NULL && g_server_closing) {
    /* Libérer complètement le socket pour permettre un nouveau bind */
    tcp_close(g_server_socket);
    g_server_socket = NULL;
    g_server_fd = -1;
    g_server_closing = 0;
    fd_free(fd);
    return 0;
  }

  /* Si c'est un socket client (créé par accept) ou le serveur après connexion
   */
  if (fd_table[fd].type == FILE_TYPE_SOCKET && fd_table[fd].socket != NULL) {
    tcp_socket_t *sock = fd_table[fd].socket;
    uint16_t port = sock->local_port;

    /* Vérifier si c'est le socket serveur (même socket que g_server_socket) */
    if (sock == g_server_socket) {
      /* Socket serveur : fermer connexion et remettre en LISTEN */
      tcp_close_and_relisten(sock, port);
      /* Ne pas libérer le FD ni le socket - on le réutilise */
      return 0;
    } else {
      /* Socket client séparé : fermer et libérer */
      tcp_close(sock);
      fd_free(fd);
      return 0;
    }
  }

  /* Si c'est un fichier VFS, le fermer */
  if (fd_table[fd].type == FILE_TYPE_FILE && fd_table[fd].vfs_node != NULL) {
    vfs_close((vfs_node_t *)fd_table[fd].vfs_node);
  }

  /* Libérer le FD */
  fd_free(fd);

  return 0;
}

/* ========================================
 * Forward declarations for new syscalls
 * ======================================== */
static int sys_fork(void);
static int sys_execve(const char *filename, char **argv, char **envp);
static int sys_waitpid(int pid, int *status, int options);
static int sys_create_thread(void *entry, void *stack, void *arg);

/* ========================================
 * Memory Management Syscalls
 * ======================================== */

/**
 * SYS_BRK (120) - Change data segment size (heap)
 *
 * @param addr  Requested new program break (0 to get current)
 * @return Current/New program break on success, -1 on failure
 */
static void *sys_brk(void *addr) {
  process_t *proc = current_process;
  if (!proc)
    return (void *)-1;

  /* If addr is NULL or 0, just return current break */
  if (addr == NULL || addr == 0) {
    return (void *)proc->heap_brk;
  }

  uint64_t new_brk = (uint64_t)addr;
  uint64_t old_brk = proc->heap_brk;

  /* Validate range - cannot shrink below heap_start */
  if (new_brk < proc->heap_start) {
    return (void *)-1;
  }

  /* Page align addresses for allocation/deallocation */
  uint64_t old_page_aligned = PAGE_ALIGN_UP(old_brk);
  uint64_t new_page_aligned = PAGE_ALIGN_UP(new_brk);

  if (new_brk > old_brk) {
    /* Expanding heap */
    KLOG_DEBUG_HEX("SYSCALL", "sys_brk expand to ", new_brk);

    /* Check if we need to allocate new pages */
    if (new_page_aligned > old_page_aligned) {
      uint64_t pages_needed = (new_page_aligned - old_page_aligned) / PAGE_SIZE;

      for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t virt = old_page_aligned + (i * PAGE_SIZE);

        /* Check safety limit (e.g. dont overwrite stack 2GB limit) */
        if (virt >= USER_STACK_TOP - USER_STACK_SIZE) {
          KLOG_ERROR("SYSCALL", "sys_brk: Heap/Stack collision");
          return (void *)-1;
        }

        /* Allocate physical page */
        void *phys_virt = pmm_alloc_block();
        if (!phys_virt) {
          /* Out of memory - should rollback here but for V1 simple fail */
          KLOG_ERROR("SYSCALL", "sys_brk: OOM");
          return (void *)-1;
        }

        /* Convert to physical address */
        uint64_t phys = pmm_virt_to_phys(phys_virt);

        /* Map page in the PROCESS's page directory, not kernel's */
        if (vmm_map_page_in_dir((page_directory_t *)proc->pml4, phys, virt,
                                PAGE_USER | PAGE_RW | PAGE_PRESENT) != 0) {
          KLOG_ERROR("SYSCALL", "sys_brk: Failed to map page");
          pmm_free_block(phys_virt);
          return (void *)-1;
        }

        /* Zero out new memory for security
         * We need to zero it in the process's address space, not kernel's.
         * Since we're in kernel mode with the process's CR3 active (during
         * syscall), we can access it directly, OR use vmm_copy_to_dir for
         * safety.
         */
        static uint8_t zero_page[PAGE_SIZE] = {0};
        if (vmm_copy_to_dir((page_directory_t *)proc->pml4, virt, zero_page,
                            PAGE_SIZE) != 0) {
          KLOG_ERROR("SYSCALL", "sys_brk: Failed to zero page");
          /* Continue anyway, just a warning */
        }
      }
    }
  } else {
    /* Shrinking heap (optional optimization) */
    /* For V1, we can just update the pointer or actually free pages */
    KLOG_DEBUG_HEX("SYSCALL", "sys_brk shrink to ", new_brk);

    if (new_page_aligned < old_page_aligned) {
      uint64_t pages_to_free =
          (old_page_aligned - new_page_aligned) / PAGE_SIZE;

      for (uint64_t i = 0; i < pages_to_free; i++) {
        uint64_t virt = new_page_aligned + (i * PAGE_SIZE);
        /* Note: vmm_unmap_page usually doesn't free physical memory in simple
         * VMMs */
        /* Ideally we should free the physical frame back to PMM */
        uint64_t phys = vmm_get_physical(virt);
        if (phys) {
          pmm_free_block((void *)phys);
          vmm_unmap_page(virt);
        }
      }
    }
  }

  proc->heap_brk = new_brk;
  return (void *)new_brk;
}

/* ========================================
 * Dispatcher principal
 * ======================================== */

void syscall_dispatcher(syscall_regs_t *regs) {
  uint32_t syscall_num = regs->rax;
  int result = -1;

  /* DEBUG: Vérification de sanité du RIP */
  uint64_t entry_rip_low = regs->rip & 0xFFFFFFFF;
  if (entry_rip_low >= 0xBFFF0000 && entry_rip_low <= 0xC0000000) {
    KLOG_ERROR("SYSCALL", "FATAL: Entry RIP corrupted (points to stack)!");
    KLOG_ERROR_HEX("SYSCALL", "  RIP (high): ", (uint32_t)(regs->rip >> 32));
    KLOG_ERROR_HEX("SYSCALL", "  RIP (low): ", (uint32_t)regs->rip);
    KLOG_ERROR_HEX("SYSCALL", "  RSP (high): ", (uint32_t)(regs->rsp >> 32));
    KLOG_ERROR_HEX("SYSCALL", "  RSP (low): ", (uint32_t)regs->rsp);
    KLOG_ERROR_HEX("SYSCALL", "  RFLAGS: ", (uint32_t)regs->rflags);
    for (;;)
      __asm__ volatile("hlt");
  }

  /* Vérifier si le mode compatibilité Linux est actif */
  if (linux_compat_is_active()) {
    /* Déléguer au handler Linux */
    result = linux_syscall_handler(regs);
    regs->rax = (uint32_t)result;
    return;
  }

  switch (syscall_num) {
  case SYS_EXIT:
    result = sys_exit((int)regs->rdi);
    break;

  case SYS_READ:
    result = sys_read((int)regs->rdi, (void *)regs->rsi, regs->rdx);
    break;

  case SYS_WRITE:
    result = sys_write((int)regs->rdi, (const char *)regs->rsi, regs->rdx);
    break;

  case SYS_OPEN:
    result = sys_open((const char *)regs->rdi, (int)regs->rsi);
    break;

  case SYS_GETPID:
    result = sys_getpid();
    break;

  /* Socket syscalls */
  case SYS_SOCKET:
    result = sys_socket((int)regs->rdi, (int)regs->rsi, (int)regs->rdx);
    break;

  case SYS_BIND:
    result =
        sys_bind((int)regs->rdi, (sockaddr_in_t *)regs->rsi, (int)regs->rdx);
    break;

  case SYS_LISTEN:
    result = sys_listen((int)regs->rdi, (int)regs->rsi);
    break;

  case SYS_ACCEPT:
    result = sys_accept((int)regs->rdi, (sockaddr_in_t *)regs->rsi,
                        (int *)regs->rdx);
    KLOG_INFO_HEX("SYSCALL", "sys_accept returned: ", (uint32_t)result);
    KLOG_INFO_HEX("SYSCALL", "Post-accept RIP: ", regs->rip);
    KLOG_INFO_HEX("SYSCALL", "Post-accept RSP: ", regs->rsp);
    break;

  case SYS_RECV:
    /* recv(fd, buf, len, flags) - EDI contient flags */
    result = sys_recv((int)regs->rdi, (uint8_t *)regs->rsi, (int)regs->rdx,
                      (int)regs->r8);
    break;

  case SYS_SEND:
    /* send(fd, buf, len, flags) - EDI contient flags */
    result = sys_send((int)regs->rdi, (const uint8_t *)regs->rsi,
                      (int)regs->rdx, (int)regs->r8);
    break;

  case SYS_CLOSE:
    result = sys_close((int)regs->rdi);
    break;

  case SYS_KBHIT:
    result = sys_kbhit();
    break;

  /* Filesystem syscalls */
  case SYS_GETCWD:
    result = sys_getcwd((char *)regs->rdi, regs->rsi);
    break;

  case SYS_CHDIR:
    result = sys_chdir((const char *)regs->rdi);
    break;

  case SYS_READDIR:
    result = sys_readdir((const char *)regs->rdi, regs->rsi,
                         (userspace_dirent_t *)regs->rdx);
    break;

  case SYS_MKDIR:
    result = sys_mkdir((const char *)regs->rdi);
    break;

  case SYS_CREATE:
    result = sys_create((const char *)regs->rdi);
    break;

  /* System syscalls */
  case SYS_CLEAR:
    result = sys_clear();
    break;

  case SYS_MEMINFO:
    result = sys_meminfo((meminfo_t *)regs->rdi);
    break;

  case SYS_GET_FRAMEBUFFER:
    result = sys_get_framebuffer((framebuffer_info_t *)regs->rdi);
    break;

  case SYS_BRK:
    regs->rax = (uint64_t)sys_brk((void *)regs->rdi);
    /* Special handling: sys_brk returns void*, possibly > 2GB or negative error
     */
    /* If we cast void* to int directly, it might truncate or look weird */
    /* But for syscall result variable 'result' (int), we might have issues if
     * address > 2GB */
    /* So we write directly to RAX and return via return; or break; */
    /* However, the end of function does regs->rax = result; */
    /* We should probably change result to uint64_t or int64_t */
    /* For now, let's treat it carefully. */
    return; /* Early return to avoid overwriting RAX with (int)result */

  case SYS_GET_EVENT:
    result = sys_get_event((input_event_t *)regs->rdi);
    break;

  /* Process syscalls - forward declarations */
  case SYS_FORK:
    result = sys_fork();
    break;

  case SYS_EXECVE:
    result = sys_execve((const char *)regs->rdi, (char **)regs->rsi,
                        (char **)regs->rdx);
    break;

  case SYS_WAITPID:
    result = sys_waitpid((int)regs->rdi, (int *)regs->rsi, (int)regs->rdx);
    break;

  case SYS_THREAD_CREATE:
    result = sys_create_thread((void *)regs->rdi, (void *)regs->rsi,
                               (void *)regs->rdx);
    break;

  case SYS_NANOSLEEP:
    result = sys_nanosleep(regs->rdi);
    break;

  default:
    KLOG_ERROR("SYSCALL", "Unknown syscall number!");
    KLOG_ERROR_DEC("SYSCALL", "  syscall_num: ", syscall_num);
    result = -1;
    break;
  }

  /* Retourner le résultat dans EAX */
  regs->rax = (uint32_t)result;

  /* NOUVEAU: Vérifier si le thread doit céder le CPU */
  thread_t *current = thread_current();
  if (current && current->needs_yield &&
      current->state == THREAD_STATE_BLOCKED) {
    /* Le thread est bloqué (ex: dans condvar_wait).
     * On doit céder le CPU MAINTENANT, avec le contexte syscall complet.
     *
     * IMPORTANT: Ne PAS retourner au user space - appeler scheduler.
     */
    KLOG_INFO("SYSCALL", "Thread blocked, yielding from syscall");

    /* Sauvegarder le contexte complet (RIP, RSP, etc. sont dans regs) */
    current->rsp = (uint64_t)regs; /* Sauvegarder le frame syscall */

    /* Marquer comme BLOCKED (déjà fait par condvar_wait) */
    /* current->state = THREAD_STATE_BLOCKED; */

    /* Retirer de la run queue */
    scheduler_dequeue(current);

    /* Appeler le scheduler - il va choisir un autre thread */
    scheduler_schedule();

    /* Quand on revient ici, on a été réveillé.
     * Le contexte est restauré, on peut continuer. */
    KLOG_INFO("SYSCALL", "Thread woken up, resuming syscall");
  }

  /* Logging disabled for performance - uncomment for debugging:
   * KLOG_INFO("SYSCALL", "syscall_dispatcher returning");
   */
}

/* ========================================
 * Initialisation
 * ======================================== */

void syscall_init(void) {
  KLOG_INFO("SYSCALL", "=== Initializing Syscall Interface ===");

  /*
   * Enregistrer l'interruption 0x80 avec DPL=3.
   * CRITIQUE: Le DPL doit être 3 pour que Ring 3 puisse déclencher
   * l'interruption !
   *
   * Flags = 0xEE:
   *   - Bit 7 (0x80): Present = 1
   *   - Bits 5-6 (0x60): DPL = 3 (Ring 3 autorisé)
   *   - Bit 4 (0x00): Storage Segment = 0 (pour une gate)
   *   - Bits 0-3 (0x0E): Type = 0xE (32-bit Interrupt Gate)
   *
   *   0x80 | 0x60 | 0x0E = 0xEE
   */
  /* 0x08 = Kernel Code Segment, 0x8E = Present | DPL=0 | Interrupt Gate
   * Pour permettre l'appel depuis Ring 3, on utilise 0xEE (DPL=3)
   * IST=0 pour utiliser la stack normale */
  idt_set_gate(0x80, (uint64_t)syscall_handler_asm, 0x08, 0xEE, 0);

  KLOG_INFO("SYSCALL", "INT 0x80 registered (DPL=3)");

  /* Initialiser la couche de compatibilité Linux */
  linux_compat_init();

  KLOG_INFO("SYSCALL", "Syscall interface ready!");
}

/* ========================================
 * Implémentation des nouveaux syscalls
 * ======================================== */

/**
 * SYS_FORK (57) - Créer un nouveau processus
 *
 * @return PID du processus fils, ou -1 si erreur
 */
static int sys_fork(void) {
  KLOG_INFO("SYSCALL", "sys_fork called");

  /* Obtenir le processus courant */
  process_t *parent = current_process;
  if (parent == NULL) {
    KLOG_ERROR("SYSCALL", "sys_fork: no current process");
    return -1;
  }

  /* Créer un nouveau processus en copiant le processus courant */
  process_t *child = process_create_kernel(parent->name, NULL, NULL, 0);
  if (child == NULL) {
    KLOG_ERROR("SYSCALL", "sys_fork: failed to create child process");
    return -1;
  }

  /* Configurer la relation parent-enfant */
  child->parent = parent;
  child->sibling_next = parent->first_child;
  if (parent->first_child) {
    parent->first_child->sibling_prev = child;
  }
  parent->first_child = child;

  /* Copier les informations de base */
  child->state = PROCESS_STATE_READY;
  child->should_terminate = 0;
  child->exit_status = 0;

  /* Retourner le PID du processus fils */
  KLOG_INFO_DEC("SYSCALL", "sys_fork: created child PID ", child->pid);
  return child->pid;
}

/**
 * SYS_EXECVE (59) - Exécuter un programme
 *
 * @param filename  Chemin du fichier à exécuter
 * @param argv      Tableau d'arguments
 * @param envp      Tableau d'environnement
 * @return 0 si succès, -1 si erreur
 */
static int sys_execve(const char *filename, char **argv, char **envp) {
  (void)argv; /* Unused parameter */
  (void)envp; /* Unused parameter */

  KLOG_INFO("SYSCALL", "sys_execve called");
  KLOG_INFO("SYSCALL", filename);

  /* Pour l'instant, utiliser process_execute qui est déjà implémenté */
  int result = process_execute(filename);
  if (result < 0) {
    KLOG_ERROR("SYSCALL", "sys_execve: failed to execute program");
    return -1;
  }

  /* Note: process_execute retourne le PID, mais execve devrait retourner 0 en
   * cas de succès */
  return 0;
}

/**
 * SYS_WAITPID (61) - Attendre la fin d'un processus fils
 *
 * @param pid       PID du processus à attendre (-1 pour n'importe quel fils)
 * @param status    Pointeur pour stocker le code de sortie
 * @param options   Options (0 pour l'instant)
 * @return PID du processus terminé, ou -1 si erreur
 */
static int sys_waitpid(int pid, int *status, int options) {
  (void)options; /* Unused parameter */

  KLOG_INFO("SYSCALL", "sys_waitpid called");
  KLOG_INFO_DEC("SYSCALL", "  pid: ", pid);

  /* Obtenir le processus courant */
  process_t *parent = current_process;
  if (parent == NULL) {
    KLOG_ERROR("SYSCALL", "sys_waitpid: no current process");
    return -1;
  }

  /* Si pid = -1, attendre n'importe quel processus fils */
  if (pid == -1) {
    /* Trouver un processus fils terminé */
    process_t *child = parent->first_child;
    while (child) {
      if (child->state == PROCESS_STATE_TERMINATED ||
          child->state == PROCESS_STATE_ZOMBIE) {
        /* Retourner le PID et le code de sortie */
        if (status) {
          *status = child->exit_status;
        }
        KLOG_INFO_DEC("SYSCALL", "sys_waitpid: reaped child PID ", child->pid);
        return child->pid;
      }
      child = child->sibling_next;
    }

    /* Aucun processus fils terminé trouvé */
    KLOG_INFO("SYSCALL", "sys_waitpid: no terminated child found");
    return -1;
  }

  /* Attendre un processus fils spécifique */
  process_t *child = parent->first_child;
  while (child) {
    if (child->pid == (uint32_t)pid) {
      /* Attendre que le processus se termine */
      if (child->state != PROCESS_STATE_TERMINATED &&
          child->state != PROCESS_STATE_ZOMBIE) {
        /* Pour l'instant, retourner -1 si le processus n'est pas encore terminé
         */
        KLOG_INFO("SYSCALL", "sys_waitpid: child not yet terminated");
        return -1;
      }

      /* Retourner le PID et le code de sortie */
      if (status) {
        *status = child->exit_status;
      }
      KLOG_INFO_DEC("SYSCALL", "sys_waitpid: reaped child PID ", child->pid);
      return child->pid;
    }
    child = child->sibling_next;
  }

  /* Processus fils non trouvé */
  KLOG_ERROR("SYSCALL", "sys_waitpid: child not found");
  return -1;
}

/**
 * SYS_THREAD_CREATE (60) - Créer un nouveau thread dans le processus courant
 *
 * Ce syscall permet aux programmes utilisateurs (Ring 3) de créer des threads
 * partageant le même espace mémoire (même processus).
 *
 * @param entry  Point d'entrée du thread (adresse en user space)
 * @param stack  Stack pointer user pour le nouveau thread
 * @param arg    Argument à passer au thread (non utilisé pour l'instant)
 * @return TID du nouveau thread si succès, -1 si erreur
 */
static int sys_create_thread(void *entry, void *stack, void *arg) {
  KLOG_INFO("SYSCALL", "sys_create_thread called");
  KLOG_INFO_HEX("SYSCALL", "  entry: ", (uint64_t)entry);
  KLOG_INFO_HEX("SYSCALL", "  stack: ", (uint64_t)stack);
  KLOG_INFO_HEX("SYSCALL", "  arg: ", (uint64_t)arg);

  /* 1. Récupérer le processus parent via current_process */
  process_t *proc = current_process;
  if (proc == NULL) {
    KLOG_ERROR("SYSCALL", "sys_create_thread: no current process");
    return -1;
  }

  /* 2. Allouer une nouvelle kernel stack pour le nouveau thread */
  void *kernel_stack = kmalloc(THREAD_DEFAULT_STACK_SIZE);
  if (kernel_stack == NULL) {
    KLOG_ERROR("SYSCALL", "sys_create_thread: failed to allocate kernel stack");
    return -1;
  }

  KLOG_INFO_HEX("SYSCALL", "  kernel_stack: ", (uint64_t)kernel_stack);

  /* 3. Créer le thread user via thread_create_user */
  thread_t *new_thread = thread_create_user(
      proc,                     /* Processus propriétaire */
      "user_thread",            /* Nom du thread */
      (uint64_t)entry,          /* Point d'entrée user */
      (uint64_t)stack,          /* Stack pointer user */
      arg,                      /* Argument pour le thread */
      kernel_stack,             /* Kernel stack allouée */
      THREAD_DEFAULT_STACK_SIZE /* Taille de la kernel stack */
  );

  if (new_thread == NULL) {
    KLOG_ERROR("SYSCALL", "sys_create_thread: thread_create_user failed");
    /* Libérer la kernel stack en cas d'échec */
    kfree(kernel_stack);
    return -1;
  }

  /* Incrémenter le compteur de threads du processus */
  proc->thread_count++;

  KLOG_INFO_DEC("SYSCALL", "sys_create_thread: created thread TID ",
                new_thread->tid);

  /* 4. Retourner le TID du nouveau thread */
  return (int)new_thread->tid;
}

/* ========================================
 * Fonctions syscall exportées (pour linux_compat)
 * ======================================== */

void syscall_do_exit(int status) { sys_exit(status); }

int syscall_do_read(int fd, void *buf, uint64_t count) {
  return sys_read(fd, buf, count);
}

int syscall_do_write(int fd, const void *buf, uint64_t count) {
  return sys_write(fd, buf, count);
}

int syscall_do_open(const char *path, uint64_t flags) {
  return sys_open(path, flags);
}

int syscall_do_close(int fd) { return sys_close(fd); }

int syscall_do_getpid(void) { return sys_getpid(); }

int syscall_do_getcwd(char *buf, uint64_t size) {
  return sys_getcwd(buf, size);
}

int syscall_do_chdir(const char *path) { return sys_chdir(path); }

int syscall_do_mkdir(const char *path) { return sys_mkdir(path); }

/* Implémentations des fonctions exportées pour les nouveaux syscalls */
int syscall_do_fork(void) { return sys_fork(); }

int syscall_do_execve(const char *filename, char **argv, char **envp) {
  return sys_execve(filename, argv, envp);
}

int syscall_do_waitpid(int pid, int *status, int options) {
  return sys_waitpid(pid, status, options);
}
