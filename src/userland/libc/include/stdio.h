#ifndef _STDIO_H
#define _STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>
#define EOF (-1)

#include <bits/alos_wchar.h>

struct _IO_FILE {
  int fd;
  int error;
  int eof;
  unsigned char pushed, has_pushback;
  /* Un seul caractere large en attente ; pas d'orientation FILE generale. */
  wint_t wide_pushed;
  unsigned char has_wide_pushback;
  unsigned char access_mode;
};

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vprintf(const char *format, va_list ap);
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list ap);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int vasprintf(char **str, const char *format, va_list ap);
int asprintf(char **str, const char *format, ...);
int sscanf(const char *str, const char *format, ...);
int vsscanf(const char *str, const char *format, va_list ap);

int putchar(int c);
int puts(const char *s);
int getchar(void);
int getc(FILE *);
int fgetc(FILE *);
char *fgets(char *buffer, int size, FILE *stream);
int ungetc(int, FILE *);
int putc(int, FILE *);
int fputc(int, FILE *);
int fputs(const char *, FILE *);
void perror(const char *);


#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *fopen(const char *pathname, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fileno(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);

#ifdef __cplusplus
}
#endif
#endif
