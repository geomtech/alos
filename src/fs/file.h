/* src/fs/file.h - File Descriptor Management */
#ifndef FS_FILE_H
#define FS_FILE_H

#include <stdint.h>

/* Forward declaration */
struct tcp_socket;
struct ipc_endpoint;
struct shm_object;

/* ========================================
 * Constantes
 * ======================================== */

#define MAX_FD              32      /* Maximum file descriptors per process */
#define FD_STDIN            0       /* Standard input */
#define FD_STDOUT           1       /* Standard output */
#define FD_STDERR           2       /* Standard error */
#define FD_CLOEXEC          0x0001

/* ========================================
 * Types de fichiers
 * ======================================== */

typedef enum {
    FILE_TYPE_NONE = 0,         /* Unused slot */
    FILE_TYPE_CONSOLE,          /* Console (stdin/stdout/stderr) */
    FILE_TYPE_FILE,             /* Regular file (VFS) */
    FILE_TYPE_SOCKET,           /* Network socket (TCP/UDP) */
    FILE_TYPE_PIPE,             /* Pipe (future) */
    FILE_TYPE_IPC,
    FILE_TYPE_SHM
} file_type_t;

/* ========================================
 * Flags pour les fichiers
 * ======================================== */

#define O_RDONLY    0x0001      /* Open for reading only */
#define O_WRONLY    0x0002      /* Open for writing only */
#define O_RDWR      0x0003      /* Open for reading and writing */
#define O_CREAT     0x0100      /* Create file if it doesn't exist */
#define O_TRUNC     0x0200      /* Truncate file to zero length */
#define O_APPEND    0x0400      /* Append to file */

/* ========================================
 * Structure File Descriptor
 * ======================================== */

typedef struct open_file_description {
    file_type_t type;           /* Type of file */
    uint32_t    flags;          /* Open flags (O_RDONLY, etc.) */
    uint32_t    position;       /* Shared offset after fork() */
    
    union {
        void*               vfs_node;   /* VFS node (for FILE_TYPE_FILE) */
        struct tcp_socket*  socket;     /* TCP socket (for FILE_TYPE_SOCKET) */
        struct ipc_endpoint* ipc_endpoint;
        struct shm_object* shm_object;
    };
    
    volatile int ref_count;     /* Descriptor-table references */
} open_file_description_t;

/**
 * Entrée d'une table de descripteurs propre à un processus.
 * Plusieurs entrées/processus peuvent référencer la même description ouverte.
 */
typedef struct file_descriptor {
    open_file_description_t* description;
    uint32_t descriptor_flags; /* Réservé pour FD_CLOEXEC */
} file_descriptor_t;

void file_table_init(file_descriptor_t table[MAX_FD],
                     const file_descriptor_t parent[MAX_FD]);
void file_table_destroy(file_descriptor_t table[MAX_FD]);
void file_table_close_on_exec(file_descriptor_t table[MAX_FD]);
int file_table_install(file_descriptor_t table[MAX_FD],
                       open_file_description_t* description);
open_file_description_t* file_table_get(file_descriptor_t table[MAX_FD],
                                        int fd);
int file_table_close(file_descriptor_t table[MAX_FD], int fd);
open_file_description_t* file_description_create(file_type_t type,
                                                 uint32_t flags,
                                                 void* resource);
void file_description_retain(open_file_description_t* description);
void file_description_release(open_file_description_t* description);

/* ========================================
 * Socket Address Structures (BSD-like)
 * ======================================== */

/* Address families */
#define AF_INET     2           /* Internet IP Protocol */

/* Socket types */
#define SOCK_STREAM 1           /* TCP - Sequenced, reliable, connection-based byte streams */
#define SOCK_DGRAM  2           /* UDP - Connectionless, unreliable datagrams */

/* Protocols */
#define IPPROTO_TCP 6           /* TCP */
#define IPPROTO_UDP 17          /* UDP */

/**
 * Generic socket address structure.
 * Used as a generic pointer type for socket functions.
 */
typedef struct sockaddr {
    uint16_t sa_family;         /* Address family (AF_INET, etc.) */
    char     sa_data[14];       /* Protocol-specific address */
} sockaddr_t;

/**
 * IPv4 socket address structure.
 * Used for bind(), connect(), accept(), etc.
 */
typedef struct sockaddr_in {
    uint16_t sin_family;        /* Address family (AF_INET) */
    uint16_t sin_port;          /* Port number (network byte order!) */
    uint32_t sin_addr;          /* IPv4 address (network byte order!) */
    char     sin_zero[8];       /* Padding to match sockaddr size */
} sockaddr_in_t;

/* ========================================
 * Helper macros
 * ======================================== */

/**
 * Convert host byte order to network byte order (16-bit).
 */
#define htons(x) ((uint16_t)(((x) >> 8) | (((x) & 0xFF) << 8)))

/**
 * Convert network byte order to host byte order (16-bit).
 */
#define ntohs(x) htons(x)

/**
 * Convert host byte order to network byte order (32-bit).
 */
#define htonl(x) ((uint32_t)( \
    (((x) & 0xFF000000) >> 24) | \
    (((x) & 0x00FF0000) >> 8)  | \
    (((x) & 0x0000FF00) << 8)  | \
    (((x) & 0x000000FF) << 24)))

/**
 * Convert network byte order to host byte order (32-bit).
 */
#define ntohl(x) htonl(x)

/**
 * Create an IPv4 address from 4 bytes.
 * Example: INADDR(10, 0, 2, 15) = 10.0.2.15
 */
#define INADDR(a, b, c, d) ((uint32_t)((a) | ((b) << 8) | ((c) << 16) | ((d) << 24)))

#define INADDR_ANY      0x00000000      /* 0.0.0.0 - Listen on all interfaces */

#endif /* FS_FILE_H */
