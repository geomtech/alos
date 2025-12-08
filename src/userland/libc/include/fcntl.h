#ifndef _FCNTL_H
#define _FCNTL_H

#define O_RDONLY 0x0000   /* Open for reading only */
#define O_WRONLY 0x0001   /* Open for writing only */
#define O_RDWR 0x0002     /* Open for reading and writing */
#define O_ACCMODE 0x0003  /* Mask for access mode */
#define O_CREAT 0x0100    /* Create file if it doesn't exist */
#define O_EXCL 0x0200     /* Error if O_CREAT and file exists */
#define O_TRUNC 0x0400    /* Truncate file to zero length */
#define O_APPEND 0x0800   /* Append to file */
#define O_NONBLOCK 0x1000 /* Non-blocking I/O */
#define O_SYNC 0x2000     /* Synchronous writes */

/* Seek whence values */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int open(const char *pathname, int flags, ...);

#endif
