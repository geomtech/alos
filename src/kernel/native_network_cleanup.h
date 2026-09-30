#ifndef KERNEL_NATIVE_NETWORK_CLEANUP_H
#define KERNEL_NATIVE_NETWORK_CLEANUP_H
#include "thread.h"
#include "../fs/file.h"
typedef struct native_network_resources {
    thread_t *thread;
    struct native_network_resources *next;
    open_file_description_t *description;
    open_file_description_t **descriptions;
    uint64_t description_count;
    void *allocation1, *allocation2;
    bool own_description;
    bool poll_resources;
} native_network_resources_t;
void native_network_resources_register(native_network_resources_t *);
void native_network_resources_unregister(native_network_resources_t *);
/* Appeler avant de recuperer la pile d'un thread mort, jamais a la demande kill. */
void native_network_thread_cleanup(thread_t *);
void native_socket_thread_cleanup(thread_t *);
void native_poll_thread_cleanup(thread_t *);
#endif
