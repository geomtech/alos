/* userland/libc.h - Minimal C Library for ALOS User Space */
#ifndef USERLAND_LIBC_H
#define USERLAND_LIBC_H

#include <stdint.h>
#include <stddef.h>

/* ========================================
 * Syscall Numbers (must match kernel)
 * ======================================== */

#define SYS_EXIT        1
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_GETPID      20
#define SYS_SOCKET      41
#define SYS_ACCEPT      43
#define SYS_SEND        44
#define SYS_RECV        45
#define SYS_BIND        49
#define SYS_LISTEN      50

/* ========================================
 * Socket Definitions (BSD-like)
 * ======================================== */

/* Address families */
#define AF_INET         2           /* Internet IP Protocol */

/* Socket types */
#define SOCK_STREAM     1           /* TCP */
#define SOCK_DGRAM      2           /* UDP */

/* Protocols */
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

/* Special addresses */
#define INADDR_ANY      0x00000000  /* 0.0.0.0 */

/* ========================================
 * File open flags
 * ======================================== */

#define O_RDONLY    0x0001      /* Open for reading only */
#define O_WRONLY    0x0002      /* Open for writing only */
#define O_RDWR      0x0003      /* Open for reading and writing */
#define O_CREAT     0x0100      /* Create file if it doesn't exist */
#define O_TRUNC     0x0200      /* Truncate file to zero length */
#define O_APPEND    0x0400      /* Append to file */

/**
 * Generic socket address
 */
struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

/**
 * IPv4 socket address
 * 
 * Usage:
 *   struct sockaddr_in addr;
 *   addr.sin_family = AF_INET;
 *   addr.sin_port = htons(8080);
 *   addr.sin_addr = INADDR_ANY;  // or htonl(IP)
 */
struct sockaddr_in {
    uint16_t sin_family;        /* AF_INET */
    uint16_t sin_port;          /* Port (network byte order!) */
    uint32_t sin_addr;          /* IP address (network byte order!) */
    char     sin_zero[8];       /* Padding */
};

/* ========================================
 * Byte Order Conversion
 * ======================================== */

/**
 * Host to network short (16-bit)
 */
static inline uint16_t htons(uint16_t x)
{
    return ((x >> 8) & 0xFF) | ((x & 0xFF) << 8);
}

/**
 * Network to host short (16-bit)
 */
static inline uint16_t ntohs(uint16_t x)
{
    return htons(x);
}

/**
 * Host to network long (32-bit)
 */
static inline uint32_t htonl(uint32_t x)
{
    return ((x >> 24) & 0x000000FF) |
           ((x >> 8)  & 0x0000FF00) |
           ((x << 8)  & 0x00FF0000) |
           ((x << 24) & 0xFF000000);
}

/**
 * Network to host long (32-bit)
 */
static inline uint32_t ntohl(uint32_t x)
{
    return htonl(x);
}

/* ========================================
 * Syscall Wrapper (inline assembly)
 * ======================================== */

/**
 * Generic syscall with 3 arguments
 * 
 * Convention:
 *   EAX = syscall number
 *   EBX = arg1
 *   ECX = arg2
 *   EDX = arg3
 *   Return value in EAX
 */
static inline int syscall3(int num, int arg1, int arg2, int arg3)
{
    int result;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3)
        : "memory"
    );
    return result;
}

/**
 * Syscall with 4 arguments (uses ESI for 4th arg)
 */
static inline int syscall4(int num, int arg1, int arg2, int arg3, int arg4)
{
    int result;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3), "S" (arg4)
        : "memory"
    );
    return result;
}

/* ========================================
 * Standard Library Functions
 * ======================================== */

/* Forward declaration of main */
int main(void);

/**
 * Entry point for userland programs.
 * This is called by the kernel and calls main(), then exit().
 */
void _start(void) __attribute__((section(".text.start")));
void _start(void)
{
    int ret = main();
    /* Call exit syscall */
    __asm__ volatile (
        "movl %0, %%ebx\n"
        "movl $1, %%eax\n"  /* SYS_EXIT = 1 */
        "int $0x80\n"
        :
        : "r" (ret)
        : "eax", "ebx"
    );
    /* Ne devrait jamais arriver */
    while (1) {}
}

/**
 * Exit the process
 */
static inline void exit(int status)
{
    syscall3(SYS_EXIT, status, 0, 0);
    /* Ne devrait jamais retourner */
    while (1) {}
}

/**
 * Write to a file descriptor
 */
static inline int write(int fd, const void* buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

/**
 * Read from a file descriptor
 */
static inline int read(int fd, void* buf, size_t count)
{
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

/**
 * Close a file descriptor
 */
static inline int close(int fd)
{
    return syscall3(SYS_CLOSE, fd, 0, 0);
}

/**
 * Open a file
 * 
 * @param path   Path to the file
 * @param flags  Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
 * @return File descriptor, or -1 on error
 * 
 * Example:
 *   int fd = open("/index.html", O_RDONLY);
 */
static inline int open(const char* path, int flags)
{
    return syscall3(SYS_OPEN, (int)path, flags, 0);
}

/**
 * Get current process ID
 */
static inline int getpid(void)
{
    return syscall3(SYS_GETPID, 0, 0, 0);
}

/* ========================================
 * Socket Functions
 * ======================================== */

/**
 * Create a socket
 * 
 * @param domain    Address family (AF_INET)
 * @param type      Socket type (SOCK_STREAM for TCP)
 * @param protocol  Protocol (0 or IPPROTO_TCP)
 * @return Socket file descriptor, or -1 on error
 * 
 * Example:
 *   int sockfd = socket(AF_INET, SOCK_STREAM, 0);
 */
static inline int socket(int domain, int type, int protocol)
{
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

/**
 * Bind a socket to an address
 * 
 * @param sockfd  Socket file descriptor
 * @param addr    Address to bind to (struct sockaddr_in*)
 * @param addrlen Size of the address structure
 * @return 0 on success, -1 on error
 * 
 * Example:
 *   struct sockaddr_in addr;
 *   addr.sin_family = AF_INET;
 *   addr.sin_port = htons(8080);
 *   addr.sin_addr = INADDR_ANY;
 *   bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
 */
static inline int bind(int sockfd, const struct sockaddr* addr, int addrlen)
{
    return syscall3(SYS_BIND, sockfd, (int)addr, addrlen);
}

/**
 * Listen for connections on a socket
 * 
 * @param sockfd  Socket file descriptor
 * @param backlog Maximum pending connections (ignored in V1)
 * @return 0 on success, -1 on error
 * 
 * Example:
 *   listen(sockfd, 5);
 */
static inline int listen(int sockfd, int backlog)
{
    return syscall3(SYS_LISTEN, sockfd, backlog, 0);
}

/**
 * Accept a connection on a socket (BLOCKING)
 * 
 * @param sockfd  Listening socket file descriptor
 * @param addr    Client address (can be NULL)
 * @param addrlen Size of address structure (can be NULL)
 * @return New socket file descriptor for the connection, or -1 on error
 * 
 * Example:
 *   struct sockaddr_in client_addr;
 *   int client_fd = accept(sockfd, (struct sockaddr*)&client_addr, NULL);
 */
static inline int accept(int sockfd, struct sockaddr* addr, int* addrlen)
{
    return syscall3(SYS_ACCEPT, sockfd, (int)addr, (int)addrlen);
}

/**
 * Receive data from a socket (BLOCKING)
 * 
 * @param sockfd  Socket file descriptor
 * @param buf     Buffer to store received data
 * @param len     Maximum bytes to receive
 * @param flags   Flags (ignored in V1)
 * @return Number of bytes received, 0 if connection closed, -1 on error
 * 
 * Example:
 *   char buf[1024];
 *   int n = recv(sockfd, buf, sizeof(buf), 0);
 */
static inline int recv(int sockfd, void* buf, size_t len, int flags)
{
    return syscall4(SYS_RECV, sockfd, (int)buf, (int)len, flags);
}

/**
 * Send data through a socket
 * 
 * @param sockfd  Socket file descriptor
 * @param buf     Data to send
 * @param len     Number of bytes to send
 * @param flags   Flags (ignored in V1)
 * @return Number of bytes sent, or -1 on error
 * 
 * Example:
 *   const char* msg = "Hello, World!";
 *   send(sockfd, msg, strlen(msg), 0);
 */
static inline int send(int sockfd, const void* buf, size_t len, int flags)
{
    return syscall4(SYS_SEND, sockfd, (int)buf, (int)len, flags);
}

/* ========================================
 * String Utilities
 * ======================================== */

/**
 * Get string length
 */
static inline size_t strlen(const char* s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/**
 * Print a string to stdout
 */
static inline void print(const char* s)
{
    write(1, s, strlen(s));
}

/**
 * Simple printf (only supports %s, %d, %x, %c, no formatting)
 */
static inline void printf(const char* fmt, ...)
{
    /* Simple version: just print the format string */
    /* For a real printf, we'd need varargs which is complex in freestanding */
    print(fmt);
}

/**
 * Print a number to stdout
 */
static inline void print_num(int n)
{
    char buf[12];
    int i = 0;
    int neg = 0;
    
    if (n < 0) {
        neg = 1;
        n = -n;
    }
    
    do {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    
    if (neg) buf[i++] = '-';
    
    /* Reverse */
    char out[12];
    for (int j = 0; j < i; j++) {
        out[j] = buf[i - 1 - j];
    }
    out[i] = '\0';
    
    print(out);
}

#endif /* USERLAND_LIBC_H */
