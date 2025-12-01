/* userland/server.c - Simple HTTP Server for ALOS */
#include "../../userland/libc.h"

static const char http_200[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
static const char http_404[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1>\n";

int main()
{
    print("Starting ALOS Web Server...\n");

    /* 1. Créer le socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        print("Error: socket() failed\n");
        return 1;
    }
    print("Socket created: fd=");
    print_num(server_fd);
    print("\n");

    /* 2. Bind sur le port 8080 */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        print("Error: bind() failed\n");
        return 1;
    }
    print("Bound to port 8080\n");

    /* 3. Listen */
    if (listen(server_fd, 5) < 0)
    {
        print("Error: listen() failed\n");
        return 1;
    }
    print("Listening on port 8080...\n");

    /* Boucle principale du serveur */
    while (1)
    {
        /* 4. Accepter un client (Bloquant) */
        print("Waiting for connection...\n");
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
        {
            print("Error: accept() failed\n");
            continue;
        }

        print("Client connected! fd=");
        print_num(client_fd);
        print("\n");

        /* 5. Lire la requête HTTP (on l'ignore, on sert toujours index.html) */
        char buffer[512];
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0)
        {
            buffer[n] = '\0';
            print("Received request (");
            print_num(n);
            print(" bytes)\n");
        }

        /* 6. Lire le fichier index.html du disque */
        int file_fd = open("/index.html", O_RDONLY);
        if (file_fd >= 0)
        {
            char file_buf[4096];
            int read_len = read(file_fd, file_buf, sizeof(file_buf));
            close(file_fd);

            /* 7. Envoyer la réponse HTTP 200 + contenu */
            send(client_fd, http_200, strlen(http_200), 0);
            if (read_len > 0)
            {
                send(client_fd, file_buf, read_len, 0);
            }
            print("Served index.html (");
            print_num(read_len);
            print(" bytes)\n");
        }
        else
        {
            /* Fichier non trouvé - 404 */
            send(client_fd, http_404, strlen(http_404), 0);
            print("File not found, sent 404\n");
        }

        /* 8. Fermer la connexion client */
        close(client_fd);
        print("Client disconnected.\n\n");
    }

    return 0;
}