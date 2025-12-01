/* src/kernel/syscall.c - System Calls Implementation */
#include "syscall.h"
#include "console.h"
#include "process.h"
#include "klog.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/io.h"
#include "../shell/shell.h"
#include "../fs/file.h"
#include "../fs/vfs.h"
#include "../net/l4/tcp.h"
#include "../net/core/net.h"

/* Macro pour activer/désactiver les interruptions */
static inline void enable_interrupts(void) { __asm__ volatile("sti"); }
static inline void disable_interrupts(void) { __asm__ volatile("cli"); }

/* ========================================
 * File Descriptor Table (per-process, simplifié)
 * Pour V1, on utilise une table globale allouée dynamiquement
 * ======================================== */

static file_descriptor_t* fd_table = NULL;
static int fd_table_initialized = 0;

/**
 * Initialise la table des file descriptors.
 */
static void fd_table_init(void)
{
    if (fd_table_initialized && fd_table != NULL) return;
    
    /* Allouer la table dynamiquement pour éviter les problèmes de .bss */
    if (fd_table == NULL) {
        extern void* kmalloc(uint32_t size);
        fd_table = (file_descriptor_t*)kmalloc(sizeof(file_descriptor_t) * MAX_FD);
        if (fd_table == NULL) {
            console_puts("[SYSCALL] FATAL: Cannot allocate fd_table!\n");
            return;
        }
        console_puts("[SYSCALL] fd_table allocated at ");
        console_put_hex((uint32_t)fd_table);
        console_puts("\n");
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
static int fd_alloc(void)
{
    fd_table_init();
    
    /* Commencer à 3 (après stdin/stdout/stderr) */
    for (int i = 3; i < MAX_FD; i++) {
        if (fd_table[i].type == FILE_TYPE_NONE) {
            fd_table[i].ref_count = 1;
            return i;
        }
    }
    return -1;
}

/**
 * Libère un file descriptor.
 */
static void fd_free(int fd)
{
    if (fd < 0 || fd >= MAX_FD) return;
    if (fd < 3) return;  /* Ne pas libérer stdin/stdout/stderr */
    
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
static int sys_exit(int status)
{
    KLOG_INFO("SYSCALL", "sys_exit called with status:");
    KLOG_INFO_HEX("SYSCALL", "  Exit code: ", (uint32_t)status);
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("\n[SYSCALL] Process exited with code: ");
    console_put_dec(status);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Retourner au shell */
    /* Note: Ceci est temporaire - idéalement on devrait faire un vrai process_exit() */
    asm volatile("sti");  /* Réactiver les interruptions */
    shell_run();          /* Relancer le shell */
    
    /* Ne devrait jamais arriver */
    while (1) {
        asm volatile("hlt");
    }
    
    return 0; /* Jamais atteint */
}

/**
 * SYS_WRITE (4) - Écrire une chaîne
 * 
 * @param fd      File descriptor (ignoré pour l'instant, tout va à la console)
 * @param buf     Pointeur vers la chaîne (dans EBX)
 * @param count   Nombre de caractères (dans ECX), 0 = null-terminated
 */
static int sys_write(int fd, const char* buf, uint32_t count)
{
    (void)fd;  /* Ignoré pour l'instant */
    
    if (buf == NULL) {
        return -1;
    }
    
    KLOG_INFO("SYSCALL", "sys_write called");
    KLOG_INFO_HEX("SYSCALL", "  Buffer at: ", (uint32_t)buf);
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    
    if (count == 0) {
        /* Null-terminated string */
        console_puts(buf);
    } else {
        /* Écrire exactement count caractères */
        for (uint32_t i = 0; i < count && buf[i] != '\0'; i++) {
            console_putc(buf[i]);
        }
    }
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return 0;
}

/**
 * SYS_OPEN (5) - Ouvrir un fichier
 * 
 * @param path   Chemin du fichier
 * @param flags  Flags d'ouverture (O_RDONLY, etc.)
 * @return File descriptor, ou -1 si erreur
 */
static int sys_open(const char* path, int flags)
{
    fd_table_init();
    
    if (path == NULL) {
        return -1;
    }
    
    KLOG_INFO("SYSCALL", "sys_open called");
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[SYSCALL] open: ");
    console_puts(path);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Ouvrir le fichier via VFS */
    vfs_node_t* node = vfs_open(path, flags);
    if (node == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[SYSCALL] open: file not found\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    
    /* Allouer un file descriptor */
    int fd = fd_alloc();
    if (fd < 0) {
        vfs_close(node);
        console_puts("[SYSCALL] open: no free file descriptors\n");
        return -1;
    }
    
    /* Configurer le FD */
    fd_table[fd].type = FILE_TYPE_FILE;
    fd_table[fd].flags = flags;
    fd_table[fd].position = 0;
    fd_table[fd].vfs_node = node;
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[SYSCALL] open: fd=");
    console_put_dec(fd);
    console_puts(", size=");
    console_put_dec(node->size);
    console_puts(" bytes\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
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
static int sys_read(int fd, void* buf, uint32_t count)
{
    fd_table_init();
    
    if (buf == NULL || fd < 0 || fd >= MAX_FD) {
        return -1;
    }
    
    if (fd_table[fd].type == FILE_TYPE_NONE) {
        return -1;
    }
    
    /* Lecture depuis un fichier VFS */
    if (fd_table[fd].type == FILE_TYPE_FILE) {
        vfs_node_t* node = (vfs_node_t*)fd_table[fd].vfs_node;
        if (node == NULL) {
            return -1;
        }
        
        /* Lire depuis la position courante */
        int bytes_read = vfs_read(node, fd_table[fd].position, count, (uint8_t*)buf);
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
static int sys_getpid(void)
{
    if (current_process != NULL) {
        return current_process->pid;
    }
    return -1;
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
static int sys_socket(int domain, int type, int protocol)
{
    (void)protocol;
    
    KLOG_INFO("SYSCALL", "sys_socket called");
    KLOG_INFO_HEX("SYSCALL", "  domain: ", domain);
    KLOG_INFO_HEX("SYSCALL", "  type: ", type);
    
    /* Vérifier les paramètres */
    if (domain != AF_INET) {
        console_puts("[SYSCALL] socket: unsupported domain\n");
        return -1;
    }
    
    if (type != SOCK_STREAM) {
        console_puts("[SYSCALL] socket: only TCP (SOCK_STREAM) supported\n");
        return -1;
    }
    
    /* Créer le socket TCP kernel */
    tcp_socket_t* sock = tcp_socket_create();
    if (sock == NULL) {
        console_puts("[SYSCALL] socket: failed to create TCP socket\n");
        return -1;
    }
    
    /* Allouer un file descriptor */
    int fd = fd_alloc();
    if (fd < 0) {
        tcp_close(sock);
        console_puts("[SYSCALL] socket: no free file descriptors\n");
        return -1;
    }
    
    /* Associer le socket au FD */
    fd_table[fd].type = FILE_TYPE_SOCKET;
    fd_table[fd].flags = O_RDWR;
    fd_table[fd].socket = sock;
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[SYSCALL] socket: fd_table addr = ");
    console_put_hex((uint32_t)fd_table);
    console_puts(", created fd ");
    console_put_dec(fd);
    console_puts(", type set to ");
    console_put_dec(fd_table[fd].type);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
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
static int sys_bind(int fd, sockaddr_in_t* addr, int len)
{
    (void)len;
    
    fd_table_init();  /* S'assurer que la table est initialisée */
    
    KLOG_INFO("SYSCALL", "sys_bind called");
    KLOG_INFO_HEX("SYSCALL", "  fd: ", fd);
    KLOG_INFO_HEX("SYSCALL", "  addr: ", (uint32_t)addr);
    
    /* Vérifier le FD */
    if (fd < 0 || fd >= MAX_FD) {
        console_puts("[SYSCALL] bind: invalid fd\n");
        return -1;
    }
    
    console_puts("[SYSCALL] bind: fd_table[fd].type = ");
    console_put_dec(fd_table[fd].type);
    console_puts("\n");
    
    if (fd_table[fd].type != FILE_TYPE_SOCKET) {
        console_puts("[SYSCALL] bind: not a socket\n");
        return -1;
    }
    
    tcp_socket_t* sock = fd_table[fd].socket;
    if (sock == NULL) {
        console_puts("[SYSCALL] bind: socket is NULL\n");
        return -1;
    }
    
    /* Vérifier que addr est valide */
    if (addr == NULL) {
        console_puts("[SYSCALL] bind: addr is NULL\n");
        return -1;
    }
    
    /* Extraire le port (attention: network byte order) */
    uint16_t port = ntohs(addr->sin_port);
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[SYSCALL] bind: fd ");
    console_put_dec(fd);
    console_puts(" to port ");
    console_put_dec(port);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return tcp_bind(sock, port);
}

/**
 * SYS_LISTEN (50) - Mettre un socket en écoute
 * 
 * @param fd       File descriptor du socket
 * @param backlog  Taille de la queue (ignoré pour V1)
 * @return 0 si succès, -1 si erreur
 */
static int sys_listen(int fd, int backlog)
{
    (void)backlog;
    
    KLOG_INFO("SYSCALL", "sys_listen called");
    KLOG_INFO_HEX("SYSCALL", "  fd: ", fd);
    
    /* Vérifier le FD */
    if (fd < 0 || fd >= MAX_FD) {
        return -1;
    }
    
    if (fd_table[fd].type != FILE_TYPE_SOCKET) {
        console_puts("[SYSCALL] listen: not a socket\n");
        return -1;
    }
    
    tcp_socket_t* sock = fd_table[fd].socket;
    if (sock == NULL) {
        return -1;
    }
    
    /* Vérifier que le socket est bindé */
    if (sock->local_port == 0) {
        console_puts("[SYSCALL] listen: socket not bound\n");
        return -1;
    }
    
    /* Passer en mode LISTEN */
    sock->state = TCP_STATE_LISTEN;
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[SYSCALL] listen: fd ");
    console_put_dec(fd);
    console_puts(" listening on port ");
    console_put_dec(sock->local_port);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    return 0;
}

/**
 * SYS_ACCEPT (43) - Accepter une connexion entrante
 * 
 * Bloquant: attend qu'une connexion soit établie.
 * 
 * @param fd    File descriptor du socket en écoute
 * @param addr  Pointeur vers sockaddr_in pour l'adresse du client (peut être NULL)
 * @param len   Pointeur vers la taille (ignoré)
 * @return Nouveau FD pour la connexion, ou -1 si erreur
 */
static int sys_accept(int fd, sockaddr_in_t* addr, int* len)
{
    (void)len;
    static int accept_call_count = 0;
    accept_call_count++;
    
    /* Debug seulement au premier appel */
    if (accept_call_count == 1) {
        console_puts("[SYSCALL] accept: fd_table addr = ");
        console_put_hex((uint32_t)fd_table);
        console_puts(", fd_table[");
        console_put_dec(fd);
        console_puts("].type = ");
        console_put_dec(fd_table[fd].type);
        console_puts(" (expected ");
        console_put_dec(FILE_TYPE_SOCKET);
        console_puts(")\n");
    }
    
    /* Vérifier le FD */
    if (fd < 0 || fd >= MAX_FD) {
        console_puts("[SYSCALL] accept: invalid fd\n");
        return -1;
    }
    
    if (fd_table[fd].type != FILE_TYPE_SOCKET) {
        if (accept_call_count == 1) {
            console_puts("[SYSCALL] accept: not a socket\n");
        }
        return -1;
    }
    
    tcp_socket_t* sock = fd_table[fd].socket;
    if (sock == NULL) {
        return -1;
    }
    
    /* Vérifier que le socket est en écoute */
    if (sock->state != TCP_STATE_LISTEN && sock->state != TCP_STATE_SYN_RCVD) {
        console_puts("[SYSCALL] accept: socket not listening\n");
        return -1;
    }
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[SYSCALL] accept: waiting for connection on port ");
    console_put_dec(sock->local_port);
    console_puts("...\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Boucle d'attente bloquante */
    /* On attend que le socket passe à ESTABLISHED */
    int timeout = 0;
    while (sock->state != TCP_STATE_ESTABLISHED) {
        /* Vérifier si le socket a été fermé */
        if (sock->state == TCP_STATE_CLOSED) {
            console_puts("[SYSCALL] accept: socket closed\n");
            return -1;
        }
        
        /* Activer les interruptions pour permettre au réseau de fonctionner */
        enable_interrupts();
        
        /* Traiter les paquets réseau en attente */
        net_poll();
        
        /* Petit délai pour ne pas surcharger le CPU */
        for (volatile int i = 0; i < 10000; i++);
        
        /* Log périodique pour debug */
        timeout++;
        if (timeout % 1000 == 0) {
            console_puts("[SYSCALL] accept: still waiting... state=");
            console_puts(tcp_state_name(sock->state));
            console_puts("\n");
        }
        
        /* Céder le CPU */
        schedule();
    }
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[SYSCALL] accept: connection established!\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Remplir l'adresse du client si demandé */
    if (addr != NULL) {
        addr->sin_family = AF_INET;
        addr->sin_port = htons(sock->remote_port);
        addr->sin_addr = ((uint32_t)sock->remote_ip[0]) |
                         ((uint32_t)sock->remote_ip[1] << 8) |
                         ((uint32_t)sock->remote_ip[2] << 16) |
                         ((uint32_t)sock->remote_ip[3] << 24);
    }
    
    /* Pour V1 simplifiée: retourner le même FD */
    /* Dans une vraie implémentation, on créerait un nouveau socket */
    return fd;
}

/**
 * SYS_RECV (45) - Recevoir des données depuis un socket
 * 
 * @param fd     File descriptor du socket
 * @param buf    Buffer de destination
 * @param len    Taille maximale à lire
 * @param flags  Flags (ignorés pour V1)
 * @return Nombre de bytes lus, 0 si connexion fermée, -1 si erreur
 */
static int sys_recv(int fd, uint8_t* buf, int len, int flags)
{
    (void)flags;
    
    /* Vérifier le FD */
    if (fd < 0 || fd >= MAX_FD) {
        return -1;
    }
    
    if (fd_table[fd].type != FILE_TYPE_SOCKET) {
        return -1;
    }
    
    tcp_socket_t* sock = fd_table[fd].socket;
    if (sock == NULL) {
        return -1;
    }
    
    /* Vérifier que le socket est connecté */
    if (sock->state != TCP_STATE_ESTABLISHED &&
        sock->state != TCP_STATE_CLOSE_WAIT) {
        return 0;  /* Connexion fermée */
    }
    
    /* Attente bloquante des données */
    while (tcp_available(sock) == 0) {
        /* Vérifier si la connexion est toujours active */
        if (sock->state != TCP_STATE_ESTABLISHED &&
            sock->state != TCP_STATE_CLOSE_WAIT) {
            return 0;  /* Connexion fermée */
        }
        
        /* Activer les interruptions et traiter les paquets réseau */
        enable_interrupts();
        net_poll();
        
        /* Petit délai */
        for (volatile int i = 0; i < 10000; i++);
        
        schedule();
    }
    
    return tcp_recv(sock, buf, len);
}

/**
 * SYS_SEND (44) - Envoyer des données via un socket
 * 
 * @param fd     File descriptor du socket
 * @param buf    Buffer source
 * @param len    Nombre de bytes à envoyer
 * @param flags  Flags (ignorés pour V1)
 * @return Nombre de bytes envoyés, -1 si erreur
 */
static int sys_send(int fd, const uint8_t* buf, int len, int flags)
{
    (void)flags;
    
    /* Vérifier le FD */
    if (fd < 0 || fd >= MAX_FD) {
        return -1;
    }
    
    if (fd_table[fd].type != FILE_TYPE_SOCKET) {
        return -1;
    }
    
    tcp_socket_t* sock = fd_table[fd].socket;
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
static int sys_close(int fd)
{
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
    
    if (fd_table[fd].type == FILE_TYPE_NONE) {
        return -1;
    }
    
    /* Si c'est un socket, le fermer */
    if (fd_table[fd].type == FILE_TYPE_SOCKET && fd_table[fd].socket != NULL) {
        tcp_close(fd_table[fd].socket);
    }
    
    /* Si c'est un fichier VFS, le fermer */
    if (fd_table[fd].type == FILE_TYPE_FILE && fd_table[fd].vfs_node != NULL) {
        vfs_close((vfs_node_t*)fd_table[fd].vfs_node);
    }
    
    /* Libérer le FD */
    fd_free(fd);
    
    return 0;
}

/* ========================================
 * Dispatcher principal
 * ======================================== */

void syscall_dispatcher(syscall_regs_t* regs)
{
    uint32_t syscall_num = regs->eax;
    int result = -1;
    
    KLOG_INFO("SYSCALL", "=== Syscall Dispatcher ===");
    KLOG_INFO_HEX("SYSCALL", "Syscall number (EAX): ", syscall_num);
    KLOG_INFO_HEX("SYSCALL", "Arg1 (EBX): ", regs->ebx);
    KLOG_INFO_HEX("SYSCALL", "Arg2 (ECX): ", regs->ecx);
    KLOG_INFO_HEX("SYSCALL", "Arg3 (EDX): ", regs->edx);
    
    switch (syscall_num) {
        case SYS_EXIT:
            result = sys_exit((int)regs->ebx);
            break;
        
        case SYS_READ:
            result = sys_read((int)regs->ebx, (void*)regs->ecx, regs->edx);
            break;
            
        case SYS_WRITE:
            result = sys_write((int)regs->ebx, (const char*)regs->ecx, regs->edx);
            break;
        
        case SYS_OPEN:
            result = sys_open((const char*)regs->ebx, (int)regs->ecx);
            break;
            
        case SYS_GETPID:
            result = sys_getpid();
            break;
        
        /* Socket syscalls */
        case SYS_SOCKET:
            result = sys_socket((int)regs->ebx, (int)regs->ecx, (int)regs->edx);
            break;
            
        case SYS_BIND:
            result = sys_bind((int)regs->ebx, (sockaddr_in_t*)regs->ecx, (int)regs->edx);
            break;
            
        case SYS_LISTEN:
            result = sys_listen((int)regs->ebx, (int)regs->ecx);
            break;
            
        case SYS_ACCEPT:
            result = sys_accept((int)regs->ebx, (sockaddr_in_t*)regs->ecx, (int*)regs->edx);
            break;
            
        case SYS_RECV:
            /* recv(fd, buf, len, flags) - EDI contient flags */
            result = sys_recv((int)regs->ebx, (uint8_t*)regs->ecx, (int)regs->edx, (int)regs->edi);
            break;
            
        case SYS_SEND:
            /* send(fd, buf, len, flags) - EDI contient flags */
            result = sys_send((int)regs->ebx, (const uint8_t*)regs->ecx, (int)regs->edx, (int)regs->edi);
            break;
            
        case SYS_CLOSE:
            result = sys_close((int)regs->ebx);
            break;
            
        default:
            KLOG_ERROR("SYSCALL", "Unknown syscall number!");
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            console_puts("[SYSCALL] Unknown syscall: ");
            console_put_dec(syscall_num);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            result = -1;
            break;
    }
    
    /* Retourner le résultat dans EAX */
    regs->eax = (uint32_t)result;
}

/* ========================================
 * Initialisation
 * ======================================== */

void syscall_init(void)
{
    KLOG_INFO("SYSCALL", "=== Initializing Syscall Interface ===");
    
    /* 
     * Enregistrer l'interruption 0x80 avec DPL=3.
     * CRITIQUE: Le DPL doit être 3 pour que Ring 3 puisse déclencher l'interruption !
     * 
     * Flags = 0xEE:
     *   - Bit 7 (0x80): Present = 1
     *   - Bits 5-6 (0x60): DPL = 3 (Ring 3 autorisé)
     *   - Bit 4 (0x00): Storage Segment = 0 (pour une gate)
     *   - Bits 0-3 (0x0E): Type = 0xE (32-bit Interrupt Gate)
     *   
     *   0x80 | 0x60 | 0x0E = 0xEE
     */
    extern void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
    
    /* 0x08 = Kernel Code Segment, 0xEE = Present | DPL=3 | 32-bit Interrupt Gate */
    idt_set_gate(0x80, (uint32_t)syscall_handler_asm, 0x08, 0xEE);
    
    KLOG_INFO("SYSCALL", "INT 0x80 registered (DPL=3)");
    KLOG_INFO("SYSCALL", "Syscall interface ready!");
}
