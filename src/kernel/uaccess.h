/* src/kernel/uaccess.h - Acces controles a la memoire userland */
#ifndef UACCESS_H
#define UACCESS_H

#include <stdbool.h>
#include <stddef.h>

bool user_range_valid(const void *address, size_t size, bool write);
int copy_from_user(void *destination, const void *source, size_t size);
int copy_to_user(void *destination, const void *source, size_t size);
int copy_string_from_user(char *destination, const char *source,
                          size_t destination_size);

#endif
