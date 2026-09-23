/* src/fs/file.c - Tables de descripteurs et descriptions ouvertes */
#include "file.h"
#include "vfs.h"
#include "../mm/kheap.h"
#include "../net/core/net.h"
#include "../net/l4/tcp.h"
#include "../include/string.h"

static open_file_description_t console_stdin = {
    FILE_TYPE_CONSOLE, O_RDONLY, 0, {.vfs_node = NULL}, 0};
static open_file_description_t console_stdout = {
    FILE_TYPE_CONSOLE, O_WRONLY, 0, {.vfs_node = NULL}, 0};
static open_file_description_t console_stderr = {
    FILE_TYPE_CONSOLE, O_WRONLY, 0, {.vfs_node = NULL}, 0};

void file_description_retain(open_file_description_t *description) {
  if (description != NULL) {
    __atomic_add_fetch(&description->ref_count, 1, __ATOMIC_SEQ_CST);
  }
}

void file_description_release(open_file_description_t *description) {
  if (description == NULL) {
    return;
  }

  int refs =
      __atomic_sub_fetch(&description->ref_count, 1, __ATOMIC_SEQ_CST);
  if (refs > 0) {
    return;
  }

  if (description->type == FILE_TYPE_FILE &&
      description->vfs_node != NULL) {
    vfs_close((vfs_node_t *)description->vfs_node);
  } else if (description->type == FILE_TYPE_SOCKET &&
             description->socket != NULL) {
    net_lock();
    tcp_close(description->socket);
    net_unlock();
  }

  if (description->type != FILE_TYPE_CONSOLE) {
    kfree(description);
  }
}

open_file_description_t *file_description_create(file_type_t type,
                                                 uint32_t flags,
                                                 void *resource) {
  open_file_description_t *description =
      (open_file_description_t *)kmalloc(sizeof(open_file_description_t));
  if (description == NULL) {
    return NULL;
  }

  description->type = type;
  description->flags = flags;
  description->position = 0;
  description->vfs_node = resource;
  description->ref_count = 1;
  return description;
}

void file_table_init(file_descriptor_t table[MAX_FD],
                     const file_descriptor_t parent[MAX_FD]) {
  memset(table, 0, sizeof(file_descriptor_t) * MAX_FD);

  if (parent != NULL) {
    for (int fd = 0; fd < MAX_FD; fd++) {
      table[fd] = parent[fd];
      file_description_retain(table[fd].description);
    }
    return;
  }

  table[FD_STDIN].description = &console_stdin;
  table[FD_STDOUT].description = &console_stdout;
  table[FD_STDERR].description = &console_stderr;
  file_description_retain(&console_stdin);
  file_description_retain(&console_stdout);
  file_description_retain(&console_stderr);
}

void file_table_destroy(file_descriptor_t table[MAX_FD]) {
  for (int fd = 0; fd < MAX_FD; fd++) {
    file_table_close(table, fd);
  }
}

int file_table_install(file_descriptor_t table[MAX_FD],
                       open_file_description_t *description) {
  if (description == NULL) {
    return -1;
  }

  for (int fd = 3; fd < MAX_FD; fd++) {
    if (table[fd].description == NULL) {
      table[fd].description = description;
      table[fd].descriptor_flags = 0;
      return fd;
    }
  }
  return -1;
}

open_file_description_t *file_table_get(file_descriptor_t table[MAX_FD],
                                        int fd) {
  if (fd < 0 || fd >= MAX_FD) {
    return NULL;
  }
  return table[fd].description;
}

int file_table_close(file_descriptor_t table[MAX_FD], int fd) {
  if (fd < 0 || fd >= MAX_FD || table[fd].description == NULL) {
    return -1;
  }

  open_file_description_t *description = table[fd].description;
  table[fd].description = NULL;
  table[fd].descriptor_flags = 0;
  file_description_release(description);
  return 0;
}
