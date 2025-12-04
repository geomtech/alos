/* userland/libc.h - Minimal C Library for ALOS User Space */
#ifndef USERLAND_LIBC_H
#define USERLAND_LIBC_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ========================================
 * Type Definitions
 * ======================================== */

typedef int64_t ssize_t;
typedef int64_t off_t;
typedef uint32_t mode_t;
typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t time_t;

/* ========================================
 * Boolean Type
 * ======================================== */

#ifndef __cplusplus
#define bool    _Bool
#define true    1
#define false   0
#endif

/* ========================================
 * NULL Definition
 * ======================================== */

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ========================================
 * Error Codes (errno-like)
 * ======================================== */

#define ENONE       0       /* No error */
#define EPERM       1       /* Operation not permitted */
#define ENOENT      2       /* No such file or directory */
#define EIO         5       /* I/O error */
#define EBADF       9       /* Bad file descriptor */
#define ENOMEM      12      /* Out of memory */
#define EACCES      13      /* Permission denied */
#define EEXIST      17      /* File exists */
#define ENOTDIR     20      /* Not a directory */
#define EISDIR      21      /* Is a directory */
#define EINVAL      22      /* Invalid argument */
#define EMFILE      24      /* Too many open files */
#define ENOSPC      28      /* No space left on device */
#define ECONNREFUSED 111    /* Connection refused */
#define ETIMEDOUT   110     /* Connection timed out */

/* Global errno variable */
static int errno = 0;

/* ========================================
 * Syscall Numbers (must match kernel)
 * ======================================== */

#define SYS_EXIT        1
#define SYS_FORK        2
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_WAITPID     7
#define SYS_UNLINK      10
#define SYS_CHDIR       12
#define SYS_TIME        13
#define SYS_LSEEK       19
#define SYS_GETPID      20
#define SYS_SETUID      23
#define SYS_GETUID      24
#define SYS_ALARM       27
#define SYS_FSTAT       28
#define SYS_PAUSE       29
#define SYS_KILL        37
#define SYS_MKDIR       39
#define SYS_RMDIR       40
#define SYS_SOCKET      41
#define SYS_CONNECT     42
#define SYS_ACCEPT      43
#define SYS_SEND        44
#define SYS_RECV        45
#define SYS_BIND        49
#define SYS_LISTEN      50
#define SYS_SETSOCKOPT  54
#define SYS_GETSOCKOPT  55
#define SYS_EXECVE      59
#define SYS_IOCTL       54
#define SYS_MMAP        90
#define SYS_MUNMAP      91
#define SYS_STAT        106
#define SYS_CREATE      85
#define SYS_READDIR     89
#define SYS_KBHIT       100
#define SYS_CLEAR       101
#define SYS_MEMINFO     102
#define SYS_SLEEP       162
#define SYS_NANOSLEEP   162
#define SYS_GETCWD      183

/* ========================================
 * Socket Definitions (BSD-like)
 * ======================================== */

/* Address families */
#define AF_UNSPEC       0           /* Unspecified */
#define AF_LOCAL        1           /* Local/Unix domain */
#define AF_UNIX         AF_LOCAL    /* Alias */
#define AF_INET         2           /* Internet IP Protocol */
#define AF_INET6        10          /* IPv6 */

/* Socket types */
#define SOCK_STREAM     1           /* TCP */
#define SOCK_DGRAM      2           /* UDP */
#define SOCK_RAW        3           /* Raw socket */

/* Protocols */
#define IPPROTO_IP      0           /* Dummy for IP */
#define IPPROTO_ICMP    1           /* ICMP */
#define IPPROTO_TCP     6           /* TCP */
#define IPPROTO_UDP     17          /* UDP */

/* Special addresses */
#define INADDR_ANY       0x00000000  /* 0.0.0.0 */
#define INADDR_LOOPBACK  0x7F000001  /* 127.0.0.1 */
#define INADDR_BROADCAST 0xFFFFFFFF  /* 255.255.255.255 */

/* Socket options levels */
#define SOL_SOCKET      1

/* Socket options */
#define SO_REUSEADDR    2
#define SO_KEEPALIVE    9
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21

/* Shutdown modes */
#define SHUT_RD         0           /* No more receptions */
#define SHUT_WR         1           /* No more transmissions */
#define SHUT_RDWR       2           /* No more receptions or transmissions */

/* ========================================
 * File open flags
 * ======================================== */

#define O_RDONLY    0x0000      /* Open for reading only */
#define O_WRONLY    0x0001      /* Open for writing only */
#define O_RDWR      0x0002      /* Open for reading and writing */
#define O_ACCMODE   0x0003      /* Mask for access mode */
#define O_CREAT     0x0100      /* Create file if it doesn't exist */
#define O_EXCL      0x0200      /* Error if O_CREAT and file exists */
#define O_TRUNC     0x0400      /* Truncate file to zero length */
#define O_APPEND    0x0800      /* Append to file */
#define O_NONBLOCK  0x1000      /* Non-blocking I/O */
#define O_SYNC      0x2000      /* Synchronous writes */

/* Seek whence values */
#define SEEK_SET    0           /* Seek from beginning of file */
#define SEEK_CUR    1           /* Seek from current position */
#define SEEK_END    2           /* Seek from end of file */

/* Standard file descriptors */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* File mode bits */
#define S_IRWXU     0700        /* RWX for owner */
#define S_IRUSR     0400        /* Read for owner */
#define S_IWUSR     0200        /* Write for owner */
#define S_IXUSR     0100        /* Execute for owner */
#define S_IRWXG     0070        /* RWX for group */
#define S_IRGRP     0040        /* Read for group */
#define S_IWGRP     0020        /* Write for group */
#define S_IXGRP     0010        /* Execute for group */
#define S_IRWXO     0007        /* RWX for others */
#define S_IROTH     0004        /* Read for others */
#define S_IWOTH     0002        /* Write for others */
#define S_IXOTH     0001        /* Execute for others */

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
 * Syscall Wrapper (inline assembly) - x86-64
 * ======================================== */

/**
 * Generic syscall with 3 arguments
 * 
 * x86-64 Linux syscall convention:
 *   RAX = syscall number
 *   RDI = arg1
 *   RSI = arg2
 *   RDX = arg3
 *   R10 = arg4
 *   R8  = arg5
 *   R9  = arg6
 *   Return value in RAX
 */
static inline long syscall3(long num, long arg1, long arg2, long arg3)
{
    long result;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (num), "D" (arg1), "S" (arg2), "d" (arg3)
        : "memory", "rcx", "r11"
    );
    return result;
}

/**
 * Syscall with 4 arguments
 */
static inline long syscall4(long num, long arg1, long arg2, long arg3, long arg4)
{
    long result;
    register long r10 __asm__("r10") = arg4;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (num), "D" (arg1), "S" (arg2), "d" (arg3), "r" (r10)
        : "memory", "rcx", "r11"
    );
    return result;
}

/**
 * Syscall with 5 arguments
 */
static inline long syscall5(long num, long arg1, long arg2, long arg3, long arg4, long arg5)
{
    long result;
    register long r10 __asm__("r10") = arg4;
    register long r8 __asm__("r8") = arg5;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (num), "D" (arg1), "S" (arg2), "d" (arg3), "r" (r10), "r" (r8)
        : "memory", "rcx", "r11"
    );
    return result;
}

/* ========================================
 * Standard Library Functions
 * ======================================== */

/* Forward declaration of main - supports argc/argv */
int main(int argc, char *argv[]);

/**
 * Entry point for userland programs (x86-64).
 * This is called by the kernel and calls main(), then exit().
 * 
 * x86-64 ABI: arguments passed in RDI, RSI, RDX, RCX, R8, R9
 * Stack layout when _start is called:
 *   [argc]      <- RSP points here
 *   [argv]      <- Pointer to argv array
 *   ...
 */
void _start(void) __attribute__((section(".text.start"), naked));
void _start(void)
{
    __asm__ volatile (
        /* Get argc from stack */
        "popq %%rdi\n"          /* RDI = argc (1st arg to main) */
        /* Get argv from stack */
        "popq %%rsi\n"          /* RSI = argv (2nd arg to main) */
        
        /* Align stack to 16 bytes (required by x86-64 ABI) */
        "andq $-16, %%rsp\n"
        
        /* Call main(argc, argv) */
        "call main\n"
        
        /* main returned in RAX, call exit(RAX) */
        "movq %%rax, %%rdi\n"   /* RDI = return value (1st arg to syscall) */
        "movq $1, %%rax\n"      /* RAX = SYS_EXIT (1) */
        "int $0x80\n"           /* syscall */
        
        /* Should never reach here */
        "1: jmp 1b\n"
        :
        :
        : "rax", "rdi", "rsi", "memory"
    );
}

/**
 * Exit the process
 */
static inline void __attribute__((noreturn)) exit(int status)
{
    syscall3(SYS_EXIT, status, 0, 0);
    /* Ne devrait jamais retourner */
    __builtin_unreachable();
}

/**
 * Write to a file descriptor
 */
static inline ssize_t write(int fd, const void* buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

/**
 * Read from a file descriptor
 */
static inline ssize_t read(int fd, void* buf, size_t count)
{
    return syscall3(SYS_READ, fd, (long)buf, (long)count);
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
 * @param mode   File mode (permissions) when creating
 * @return File descriptor, or -1 on error
 * 
 * Example:
 *   int fd = open("/index.html", O_RDONLY, 0);
 */
static inline int open(const char* path, int flags, ...)
{
    return syscall3(SYS_OPEN, (long)path, flags, 0);
}

/**
 * Seek in a file
 * 
 * @param fd      File descriptor
 * @param offset  Offset to seek to
 * @param whence  SEEK_SET, SEEK_CUR, or SEEK_END
 * @return New position, or -1 on error
 */
static inline off_t lseek(int fd, off_t offset, int whence)
{
    return syscall3(SYS_LSEEK, fd, (long)offset, whence);
}

/**
 * Get current process ID
 */
static inline int getpid(void)
{
    return syscall3(SYS_GETPID, 0, 0, 0);
}

/**
 * Get parent process ID (stub - returns 0)
 */
static inline int getppid(void)
{
    return 0; /* TODO: Add SYS_GETPPID */
}

/**
 * Get current user ID
 */
static inline int getuid(void)
{
    return syscall3(SYS_GETUID, 0, 0, 0);
}

/**
 * Sleep for a specified number of seconds
 * 
 * @param seconds  Number of seconds to sleep
 * @return Remaining seconds if interrupted, 0 otherwise
 */
static inline unsigned int sleep(unsigned int seconds)
{
    return syscall3(SYS_SLEEP, seconds * 1000, 0, 0);
}

/**
 * Sleep for a specified number of milliseconds
 * 
 * @param ms  Number of milliseconds to sleep
 */
static inline void msleep(unsigned int ms)
{
    syscall3(SYS_SLEEP, ms, 0, 0);
}

/**
 * Read a character from keyboard (non-blocking)
 * 
 * @return The character read, or 0 if no character available
 * 
 * Example:
 *   int c = kbhit();
 *   if (c == 0x04) { // CTRL+D
 *       print("Exiting...\\n");
 *       exit(0);
 *   }
 */
static inline int kbhit(void)
{
    return syscall3(SYS_KBHIT, 0, 0, 0);
}

/**
 * Read a character from stdin (blocking)
 * 
 * @return The character read, or -1 on error/EOF
 */
static inline int getchar(void)
{
    char c;
    int ret = read(STDIN_FILENO, &c, 1);
    if (ret <= 0) return -1;
    return (unsigned char)c;
}

/**
 * Write a character to stdout
 * 
 * @param c  Character to write
 * @return The character written, or -1 on error
 */
static inline int putchar(int c)
{
    char ch = (char)c;
    if (write(STDOUT_FILENO, &ch, 1) != 1) return -1;
    return (unsigned char)ch;
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
    return syscall3(SYS_BIND, sockfd, (long)addr, addrlen);
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
    return syscall3(SYS_ACCEPT, sockfd, (long)addr, (long)addrlen);
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
    return syscall4(SYS_RECV, sockfd, (long)buf, (long)len, flags);
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
static inline ssize_t send(int sockfd, const void* buf, size_t len, int flags)
{
    return syscall4(SYS_SEND, sockfd, (long)buf, (long)len, flags);
}

/**
 * Connect to a remote address
 * 
 * @param sockfd  Socket file descriptor
 * @param addr    Address to connect to
 * @param addrlen Size of the address structure
 * @return 0 on success, -1 on error
 */
static inline int connect(int sockfd, const struct sockaddr* addr, int addrlen)
{
    return syscall3(SYS_CONNECT, sockfd, (long)addr, addrlen);
}

/**
 * Shutdown a socket connection
 * 
 * @param sockfd  Socket file descriptor
 * @param how     SHUT_RD, SHUT_WR, or SHUT_RDWR
 * @return 0 on success, -1 on error
 */
static inline int shutdown(int sockfd, int how)
{
    /* For now, just close the socket */
    (void)how;
    return close(sockfd);
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
    write(STDOUT_FILENO, s, strlen(s));
}

/**
 * Print a string followed by newline
 */
static inline void puts(const char* s)
{
    print(s);
    write(STDOUT_FILENO, "\n", 1);
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

/**
 * Print an unsigned number in hexadecimal
 */
static inline void print_hex(uint32_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11] = "0x00000000";
    
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    
    print(buf);
}

/**
 * Convert string to integer
 */
static inline int atoi(const char* s)
{
    int result = 0;
    int sign = 1;
    
    /* Skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    
    /* Handle sign */
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    /* Convert digits */
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    
    return sign * result;
}

/**
 * Convert integer to string
 * 
 * @param value  Integer to convert
 * @param str    Output buffer (must be large enough)
 * @param base   Numeric base (2-16)
 * @return Pointer to str
 */
static inline char* itoa(int value, char* str, int base)
{
    static const char digits[] = "0123456789ABCDEF";
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    int tmp_value;
    int negative = 0;
    
    if (base < 2 || base > 16) {
        *str = '\0';
        return str;
    }
    
    if (value < 0 && base == 10) {
        negative = 1;
        value = -value;
    }
    
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = digits[tmp_value - value * base];
    } while (value);
    
    if (negative) *ptr++ = '-';
    
    *ptr-- = '\0';
    
    /* Reverse */
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    
    return str;
}

/**
 * Simple printf implementation
 * Supports: %s, %d, %i, %u, %x, %X, %c, %p, %%
 */
static inline int printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    int written = 0;
    char buf[32];
    
    while (*fmt) {
        if (*fmt != '%') {
            write(STDOUT_FILENO, fmt, 1);
            written++;
            fmt++;
            continue;
        }
        
        fmt++; /* Skip '%' */
        
        switch (*fmt) {
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                size_t len = strlen(s);
                write(STDOUT_FILENO, s, len);
                written += len;
                break;
            }
            case 'd':
            case 'i': {
                int n = va_arg(args, int);
                itoa(n, buf, 10);
                size_t len = strlen(buf);
                write(STDOUT_FILENO, buf, len);
                written += len;
                break;
            }
            case 'u': {
                unsigned int n = va_arg(args, unsigned int);
                itoa(n, buf, 10);
                size_t len = strlen(buf);
                write(STDOUT_FILENO, buf, len);
                written += len;
                break;
            }
            case 'x':
            case 'X': {
                unsigned int n = va_arg(args, unsigned int);
                itoa(n, buf, 16);
                size_t len = strlen(buf);
                write(STDOUT_FILENO, buf, len);
                written += len;
                break;
            }
            case 'p': {
                void* p = va_arg(args, void*);
                write(STDOUT_FILENO, "0x", 2);
                itoa((uint64_t)p, buf, 16);
                size_t len = strlen(buf);
                write(STDOUT_FILENO, buf, len);
                written += 2 + len;
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                write(STDOUT_FILENO, &c, 1);
                written++;
                break;
            }
            case '%':
                write(STDOUT_FILENO, "%", 1);
                written++;
                break;
            default:
                write(STDOUT_FILENO, fmt - 1, 2);
                written += 2;
                break;
        }
        fmt++;
    }
    
    va_end(args);
    return written;
}

/**
 * Format string to buffer (sprintf)
 */
static inline int sprintf(char* str, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    char* ptr = str;
    char buf[32];
    
    while (*fmt) {
        if (*fmt != '%') {
            *ptr++ = *fmt++;
            continue;
        }
        
        fmt++;
        
        switch (*fmt) {
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                while (*s) *ptr++ = *s++;
                break;
            }
            case 'd':
            case 'i': {
                int n = va_arg(args, int);
                itoa(n, buf, 10);
                char* b = buf;
                while (*b) *ptr++ = *b++;
                break;
            }
            case 'u': {
                unsigned int n = va_arg(args, unsigned int);
                itoa(n, buf, 10);
                char* b = buf;
                while (*b) *ptr++ = *b++;
                break;
            }
            case 'x':
            case 'X': {
                unsigned int n = va_arg(args, unsigned int);
                itoa(n, buf, 16);
                char* b = buf;
                while (*b) *ptr++ = *b++;
                break;
            }
            case 'c':
                *ptr++ = (char)va_arg(args, int);
                break;
            case '%':
                *ptr++ = '%';
                break;
            default:
                *ptr++ = '%';
                *ptr++ = *fmt;
                break;
        }
        fmt++;
    }
    
    *ptr = '\0';
    va_end(args);
    return ptr - str;
}

/* ========================================
 * Filesystem Functions
 * ======================================== */

/**
 * Directory entry structure (must match kernel's userspace_dirent_t)
 */
typedef struct {
    char name[256];
    uint32_t type;
    uint32_t size;
} dirent_t;

/* File types */
#define DT_FILE     0x01
#define DT_DIR      0x02

/**
 * Get current working directory
 * 
 * @param buf   Buffer to store the path
 * @param size  Size of the buffer
 * @return 0 on success, -1 on error
 */
static inline int getcwd(char* buf, size_t size)
{
    return syscall3(SYS_GETCWD, (long)buf, (long)size, 0);
}

/**
 * Change current working directory
 * 
 * @param path  Path to the new directory
 * @return 0 on success, -1 on error
 */
static inline int chdir(const char* path)
{
    return syscall3(SYS_CHDIR, (long)path, 0, 0);
}

/**
 * Read a directory entry
 * 
 * @param path   Path to the directory
 * @param index  Index of the entry (0, 1, 2, ...)
 * @param entry  Output structure
 * @return 0 on success, 1 if end of directory, -1 on error
 */
static inline int readdir(const char* path, int index, dirent_t* entry)
{
    return syscall3(SYS_READDIR, (long)path, index, (long)entry);
}

/**
 * Create a directory
 * 
 * @param path  Path to the directory to create
 * @return 0 on success, -1 on error
 */
static inline int mkdir(const char* path)
{
    return syscall3(SYS_MKDIR, (long)path, 0, 0);
}

/**
 * Create a file
 * 
 * @param path  Path to the file to create
 * @return 0 on success, -1 on error
 */
static inline int creat(const char* path)
{
    return syscall3(SYS_CREATE, (long)path, 0, 0);
}

/* ========================================
 * System Functions
 * ======================================== */

/**
 * Clear the screen
 */
static inline int clear_screen(void)
{
    return syscall3(SYS_CLEAR, 0, 0, 0);
}

/**
 * Memory info structure
 */
typedef struct {
    uint32_t total_size;
    uint32_t free_size;
    uint32_t block_count;
    uint32_t free_block_count;
} meminfo_t;

/**
 * Get memory information
 * 
 * @param info  Output structure
 * @return 0 on success, -1 on error
 */
static inline int meminfo(meminfo_t* info)
{
    return syscall3(SYS_MEMINFO, (long)info, 0, 0);
}

/* ========================================
 * Additional String Utilities
 * ======================================== */

/**
 * Compare two strings
 * 
 * @return 0 if equal, non-zero otherwise
 */
static inline int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

/**
 * Copy a string
 */
static inline char* strcpy(char* dest, const char* src)
{
    char* d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

/**
 * Copy n characters of a string
 */
static inline char* strncpy(char* dest, const char* src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

/**
 * Concatenate strings
 */
static inline char* strcat(char* dest, const char* src)
{
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++) != '\0');
    return dest;
}

/**
 * Concatenate n characters
 */
static inline char* strncat(char* dest, const char* src, size_t n)
{
    char* d = dest;
    while (*d) d++;
    while (n-- > 0 && *src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

/**
 * Compare n characters of two strings
 */
static inline int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

/**
 * Find first occurrence of character in string
 */
static inline char* strchr(const char* s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

/**
 * Find last occurrence of character in string
 */
static inline char* strrchr(const char* s, int c)
{
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == '\0') ? (char*)s : (char*)last;
}

/**
 * Find substring in string
 */
static inline char* strstr(const char* haystack, const char* needle)
{
    if (!*needle) return (char*)haystack;
    
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        
        if (!*n) return (char*)haystack;
        haystack++;
    }
    
    return NULL;
}

/**
 * Duplicate a string (requires heap allocation - stub)
 * Note: Returns NULL since we don't have malloc yet
 */
static inline char* strdup(const char* s)
{
    (void)s;
    return NULL; /* TODO: Implement when malloc is available */
}

/* ========================================
 * Memory Functions
 * ======================================== */

/**
 * Copy memory block
 */
static inline void* memcpy(void* dest, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

/**
 * Move memory block (handles overlapping regions)
 */
static inline void* memmove(void* dest, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    
    return dest;
}

/**
 * Set memory block to value
 */
static inline void* memset(void* s, int c, size_t n)
{
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

/**
 * Compare memory blocks
 */
static inline int memcmp(const void* s1, const void* s2, size_t n)
{
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    
    return 0;
}

/**
 * Find byte in memory block
 */
static inline void* memchr(const void* s, int c, size_t n)
{
    const uint8_t* p = (const uint8_t*)s;
    
    while (n--) {
        if (*p == (uint8_t)c) return (void*)p;
        p++;
    }
    
    return NULL;
}

/**
 * Zero memory block
 */
static inline void bzero(void* s, size_t n)
{
    memset(s, 0, n);
}

/* ========================================
 * Character Classification
 * ======================================== */

static inline int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int isprint(int c) { return c >= 0x20 && c <= 0x7E; }
static inline int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
static inline int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline int toupper(int c) { return islower(c) ? c - 32 : c; }
static inline int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* ========================================
 * Utility Macros
 * ======================================== */

#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define ABS(x)          ((x) < 0 ? -(x) : (x))
#define CLAMP(x, lo, hi) (MIN(MAX(x, lo), hi))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define UNUSED(x)       ((void)(x))

/* Alignment macros */
#define ALIGN_UP(x, align)   (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align) (((x) & ((align) - 1)) == 0)

/* ========================================
 * IP Address Utilities
 * ======================================== */

/**
 * Convert IP address string to network byte order
 * Simple parser for "a.b.c.d" format
 * 
 * @param str  IP address string (e.g., "192.168.1.1")
 * @return IP in network byte order, or 0 on error
 */
static inline uint32_t inet_addr(const char* str)
{
    uint32_t result = 0;
    uint32_t octet = 0;
    int dots = 0;
    
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            octet = octet * 10 + (*str - '0');
            if (octet > 255) return 0;
        } else if (*str == '.') {
            result = (result << 8) | octet;
            octet = 0;
            dots++;
            if (dots > 3) return 0;
        } else {
            return 0;
        }
        str++;
    }
    
    if (dots != 3) return 0;
    result = (result << 8) | octet;
    
    return htonl(result);
}

/**
 * Convert network byte order IP to string
 * 
 * @param addr  IP address in network byte order
 * @param buf   Output buffer (at least 16 bytes)
 * @return Pointer to buf
 */
static inline char* inet_ntoa_r(uint32_t addr, char* buf)
{
    uint32_t ip = ntohl(addr);
    sprintf(buf, "%d.%d.%d.%d",
            (ip >> 24) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF,
            ip & 0xFF);
    return buf;
}

/* ========================================
 * Assertion (Debug)
 * ======================================== */

#ifdef DEBUG
#define assert(expr) do { \
    if (!(expr)) { \
        printf("Assertion failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)
#else
#define assert(expr) ((void)0)
#endif

/* ========================================
 * Simple Random Number Generator
 * ======================================== */

static uint32_t __rand_seed = 1;

/**
 * Set random seed
 */
static inline void srand(unsigned int seed)
{
    __rand_seed = seed;
}

/**
 * Generate random number (Linear Congruential Generator)
 * 
 * @return Random number between 0 and RAND_MAX
 */
#define RAND_MAX 0x7FFFFFFF

static inline int rand(void)
{
    __rand_seed = __rand_seed * 1103515245 + 12345;
    return (__rand_seed >> 16) & RAND_MAX;
}

#endif /* USERLAND_LIBC_H */
