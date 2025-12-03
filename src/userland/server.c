/* userland/server.c - Ultra-Optimized HTTP Server for ALOS */
#include "../../userland/libc.h"

#define PORT 8080

/* Réponse HTTP pré-construite en mémoire - ZERO allocation dans la boucle */
static char prebuilt_response[8192];
static int prebuilt_response_len = 0;

/* Headers HTTP */
static const char http_200_template[] = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "Content-Length: ";

static const char http_404[] = 
    "HTTP/1.1 404 Not Found\r\n"
    "Connection: close\r\n"
    "Content-Length: 22\r\n"
    "\r\n"
    "<h1>404 Not Found</h1>";

/* Pré-construire la réponse HTTP complète au démarrage */
void build_response(void)
{
    print("Building HTTP response...\n");
    
    /* Lire le fichier index.html */
    int fd = open("/index.html", O_RDONLY);
    if (fd < 0) {
        print("Warning: /index.html not found\n");
        return;
    }
    
    /* Lire le contenu dans un buffer temporaire */
    char body[4096];
    int body_len = read(fd, body, sizeof(body) - 1);
    close(fd);
    
    if (body_len <= 0) {
        print("Warning: Empty index.html\n");
        return;
    }
    body[body_len] = '\0';
    
    /* Construire la réponse complète */
    char *ptr = prebuilt_response;
    
    /* 1. Header HTTP */
    int hlen = strlen(http_200_template);
    memcpy(ptr, http_200_template, hlen);
    ptr += hlen;
    
    /* 2. Content-Length (valeur) */
    char len_str[16];
    itoa(body_len, len_str, 10);
    int clen = strlen(len_str);
    memcpy(ptr, len_str, clen);
    ptr += clen;
    
    /* 3. Fin des headers */
    memcpy(ptr, "\r\n\r\n", 4);
    ptr += 4;
    
    /* 4. Body */
    memcpy(ptr, body, body_len);
    ptr += body_len;
    
    prebuilt_response_len = (int)(ptr - prebuilt_response);
    
    print("Response ready: ");
    print_num(prebuilt_response_len);
    print(" bytes\n");
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    
    print("\n=== ALOS HTTP Server ===\n");
    print("Port: ");
    print_num(PORT);
    print("\n");
    
    /* Pré-construire la réponse au démarrage */
    build_response();
    
    /* Créer le socket serveur */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        print("Error: socket() failed\n");
        return 1;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr = INADDR_ANY;
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        print("Error: bind() failed\n");
        return 1;
    }
    
    if (listen(server_fd, 128) < 0) {
        print("Error: listen() failed\n");
        return 1;
    }
    
    print("Server ready. Waiting for connections...\n\n");
    
    /* Buffer minimal pour consommer la requête */
    char req_buf[512];
    
    /* Boucle principale ultra-optimisée */
    while (1) {
        /* 1. Accept - bloquant jusqu'à connexion */
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        
        /* 2. Lire la requête (on la consomme, pas de parsing) */
        recv(client_fd, req_buf, sizeof(req_buf), 0);
        
        /* 3. Envoyer la réponse pré-construite - UN SEUL syscall */
        if (prebuilt_response_len > 0) {
            send(client_fd, prebuilt_response, prebuilt_response_len, 0);
        } else {
            send(client_fd, http_404, sizeof(http_404) - 1, 0);
        }
        
        /* 4. Fermer - libère le socket pour la prochaine connexion */
        close(client_fd);
    }
    
    return 0;
}