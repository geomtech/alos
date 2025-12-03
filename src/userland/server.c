/* userland/server.c - Optimized HTTP Server */
#include "../../userland/libc.h"

#define PORT 8080
#define BACKLOG 128            /* Augmenté de 5 à 128 pour encaisser la charge */
#define MAX_RESPONSE_SIZE 8192 /* Buffer large pour tout envoyer d'un coup */

/* Cache du fichier en mémoire pour éviter les accès disque lents */
static char cached_index_data[4096];
static int cached_index_len = 0;

/* Headers pré-calculés */
static const char http_200_head[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: ";
static const char http_404[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1>";

/* Charger le fichier en mémoire une seule fois au démarrage */
void cache_index_file()
{
    print("Caching index.html... ");
    int fd = open("/index.html", O_RDONLY);
    if (fd >= 0)
    {
        cached_index_len = read(fd, cached_index_data, sizeof(cached_index_data) - 1);
        cached_index_data[cached_index_len] = '\0'; /* Safety null */
        close(fd);
        print("OK (");
        print_num(cached_index_len);
        print(" bytes)\n");
    }
    else
    {
        print("FAILED (File not found)\n");
    }
}

int main(int argc, char *argv[])
{
    print("Starting ALOS High-Perf Server on port ");
    print_num(PORT);
    print("...\n");

    /* 1. Pré-charger le site en RAM (Zero-Copy Disk I/O pendant les requêtes) */
    cache_index_file();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
        return 1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        print("Error: bind failed\n");
        return 1;
    }

    /* 2. Augmenter la file d'attente (Backlog) */
    if (listen(server_fd, BACKLOG) < 0)
        return 1;

    /* Buffer unique pour construire la réponse complète */
    char response_buffer[MAX_RESPONSE_SIZE];

    while (1)
    {
        /* 3. Accept (Bloquant) */
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
            continue;

        /* 4. Lire la requête (On la consomme mais on ne la parse pas pour aller vite) */
        /* Note: On utilise un petit buffer jetable sur la stack */
        char dump_buf[256];
        recv(client_fd, dump_buf, 256, 0);

        /* 5. Construire la réponse ATOMIQUE (Header + Body) */
        /* Cela résout le bug "Malformed status code" */
        if (cached_index_len > 0)
        {
            char len_str[16];
            itoa(cached_index_len, len_str, 10);

            /* Copie manuelle rapide dans le buffer unique */
            char *ptr = response_buffer;

            /* Copie Header */
            int hlen = strlen(http_200_head);
            memcpy(ptr, http_200_head, hlen);
            ptr += hlen;

            /* Copie Content-Length */
            int clen = strlen(len_str);
            memcpy(ptr, len_str, clen);
            ptr += clen;

            /* Copie Fin Header (\r\n\r\n) */
            memcpy(ptr, "\r\n\r\n", 4);
            ptr += 4;

            /* Copie Body */
            memcpy(ptr, cached_index_data, cached_index_len);
            ptr += cached_index_len;

            /* 6. ENVOI UNIQUE (Un seul syscall = Un seul paquet TCP idéalement) */
            send(client_fd, response_buffer, (int)(ptr - response_buffer), 0);
        }
        else
        {
            send(client_fd, http_404, strlen(http_404), 0);
        }

        /* 7. Fermeture immédiate */
        close(client_fd);
    }
    return 0;
}