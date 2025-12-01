/* src/net/l4/dns.c - DNS Resolver Client Implementation */
#include "dns.h"
#include "udp.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/console.h"
#include "../../mm/kheap.h"

/* ========================================
 * Variables globales du résolveur DNS
 * ======================================== */

/* Serveur DNS configuré */
static uint32_t g_dns_server = 0;
static uint8_t g_dns_server_bytes[4] = {0};
static bool g_dns_initialized = false;

/* Transaction ID (incrémenté à chaque requête) */
static uint16_t g_dns_transaction_id = 0x1234;

/* Requête en cours */
static dns_pending_query_t g_pending_query = {0};

/* ========================================
 * Fonctions utilitaires internes
 * ======================================== */

/**
 * Convertit un uint32_t IP en tableau de 4 bytes
 */
static void ip_to_bytes(uint32_t ip, uint8_t* out)
{
    out[0] = (ip >> 24) & 0xFF;
    out[1] = (ip >> 16) & 0xFF;
    out[2] = (ip >> 8) & 0xFF;
    out[3] = ip & 0xFF;
}

/**
 * Affiche une adresse IP
 */
static void print_ip_addr(const uint8_t* ip)
{
    for (int i = 0; i < 4; i++) {
        if (i > 0) console_putc('.');
        console_put_dec(ip[i]);
    }
}

/**
 * Copie de chaîne simple
 */
static void str_copy(char* dest, const char* src, int max_len)
{
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/**
 * Longueur de chaîne
 */
static int str_len(const char* s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Initialise le résolveur DNS
 */
void dns_init(uint32_t dns_server)
{
    g_dns_server = dns_server;
    ip_to_bytes(dns_server, g_dns_server_bytes);
    g_dns_initialized = true;
    
    /* Réinitialiser la requête en attente */
    g_pending_query.id = 0;
    g_pending_query.hostname[0] = '\0';
    g_pending_query.completed = false;
    g_pending_query.success = false;
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    console_puts("[DNS] Resolver initialized, server: ");
    print_ip_addr(g_dns_server_bytes);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

/**
 * Encode un nom de domaine au format DNS
 * "google.com" -> "\x06google\x03com\x00"
 */
int dns_encode_name(uint8_t* buffer, const char* hostname)
{
    int pos = 0;
    int label_start = 0;
    int i = 0;
    
    while (1) {
        /* Chercher le prochain '.' ou la fin de chaîne */
        if (hostname[i] == '.' || hostname[i] == '\0') {
            /* Écrire la longueur du label */
            int label_len = i - label_start;
            buffer[pos++] = (uint8_t)label_len;
            
            /* Copier le label */
            for (int j = label_start; j < i; j++) {
                buffer[pos++] = (uint8_t)hostname[j];
            }
            
            /* Si fin de chaîne, terminer */
            if (hostname[i] == '\0') {
                buffer[pos++] = 0;  /* Null terminator */
                break;
            }
            
            /* Passer au label suivant */
            label_start = i + 1;
        }
        i++;
    }
    
    return pos;
}

/**
 * Envoie une requête DNS pour résoudre un nom d'hôte
 */
void dns_send_query(const char* hostname)
{
    if (!g_dns_initialized) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[DNS] Error: resolver not initialized!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Allouer un buffer pour le paquet DNS */
    uint8_t buffer[DNS_MAX_PACKET_SIZE];
    int offset = 0;
    
    /* Générer un nouveau transaction ID */
    g_dns_transaction_id++;
    
    /* === Construire le header DNS === */
    dns_header_t* header = (dns_header_t*)buffer;
    header->id = htons(g_dns_transaction_id);
    header->flags = htons(DNS_FLAG_RD);  /* Recursion Desired = 0x0100 */
    header->qd_count = htons(1);         /* 1 question */
    header->an_count = 0;
    header->ns_count = 0;
    header->ar_count = 0;
    
    offset = DNS_HEADER_SIZE;
    
    /* === Encoder le nom de domaine (Question Section) === */
    int name_len = dns_encode_name(buffer + offset, hostname);
    offset += name_len;
    
    /* === Ajouter QTYPE et QCLASS === */
    /* QTYPE = A (1) - Host address */
    buffer[offset++] = 0x00;
    buffer[offset++] = DNS_TYPE_A;
    
    /* QCLASS = IN (1) - Internet */
    buffer[offset++] = 0x00;
    buffer[offset++] = DNS_CLASS_IN;
    
    /* === Enregistrer la requête en attente === */
    g_pending_query.id = g_dns_transaction_id;
    str_copy(g_pending_query.hostname, hostname, sizeof(g_pending_query.hostname));
    g_pending_query.completed = false;
    g_pending_query.success = false;
    
    /* === Log === */
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    console_puts("[DNS] Resolving: ");
    console_puts(hostname);
    console_puts(" (ID: 0x");
    console_put_hex(g_dns_transaction_id);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* === Envoyer via UDP === */
    udp_send_packet(
        g_dns_server_bytes,     /* IP du serveur DNS */
        12345,                  /* Port source (quelconque) */
        DNS_PORT,               /* Port destination = 53 */
        buffer,                 /* Données DNS */
        offset                  /* Longueur totale */
    );
}

/**
 * Saute un nom DNS encodé (gère la compression)
 * Retourne le nombre de bytes à avancer dans le buffer
 */
static int dns_skip_name(uint8_t* data, int offset, int max_len)
{
    int jumped = 0;
    int count = 0;
    
    while (offset < max_len && count < 256) {
        uint8_t len = data[offset];
        
        if (len == 0) {
            /* Fin du nom */
            return jumped ? jumped : (offset + 1);
        }
        
        if ((len & 0xC0) == 0xC0) {
            /* Compression pointer (2 bytes) */
            if (!jumped) {
                jumped = offset + 2;
            }
            /* Suivre le pointeur */
            offset = ((len & 0x3F) << 8) | data[offset + 1];
        } else {
            /* Label normal */
            offset += len + 1;
        }
        count++;
    }
    
    return jumped ? jumped : offset;
}

/**
 * Traite une réponse DNS reçue
 */
void dns_handle_packet(uint8_t* data, int len)
{
    if (data == NULL || len < DNS_HEADER_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[DNS] Packet too short\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* === Parser le header === */
    dns_header_t* header = (dns_header_t*)data;
    
    uint16_t id = ntohs(header->id);
    uint16_t flags = ntohs(header->flags);
    uint16_t qd_count = ntohs(header->qd_count);
    uint16_t an_count = ntohs(header->an_count);
    
    /* Vérifier que c'est une réponse (QR = 1) */
    if (!(flags & DNS_FLAG_QR)) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        console_puts("[DNS] Received query instead of response\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Vérifier l'ID de transaction */
    if (id != g_pending_query.id) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        console_puts("[DNS] Transaction ID mismatch: got 0x");
        console_put_hex(id);
        console_puts(", expected 0x");
        console_put_hex(g_pending_query.id);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        return;
    }
    
    /* Vérifier le code de réponse */
    uint8_t rcode = flags & DNS_FLAG_RCODE;
    if (rcode != DNS_RCODE_OK) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[DNS] Error response, RCODE: ");
        console_put_dec(rcode);
        if (rcode == DNS_RCODE_NXDOMAIN) {
            console_puts(" (NXDOMAIN - name not found)");
        } else if (rcode == DNS_RCODE_SERVFAIL) {
            console_puts(" (Server failure)");
        }
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        
        g_pending_query.completed = true;
        g_pending_query.success = false;
        return;
    }
    
    /* Log */
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    console_puts("[DNS] Response received: ");
    console_put_dec(an_count);
    console_puts(" answer(s)\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    if (an_count == 0) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        console_puts("[DNS] No answers in response\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        g_pending_query.completed = true;
        g_pending_query.success = false;
        return;
    }
    
    /* === Sauter la section Question === */
    int offset = DNS_HEADER_SIZE;
    
    for (uint16_t i = 0; i < qd_count && offset < len; i++) {
        /* Sauter le QNAME */
        offset = dns_skip_name(data, offset, len);
        /* Sauter QTYPE (2) + QCLASS (2) */
        offset += 4;
    }
    
    /* === Lire la section Answer === */
    for (uint16_t i = 0; i < an_count && offset < len; i++) {
        /* Sauter le NAME (peut être un pointer de compression) */
        int name_end = dns_skip_name(data, offset, len);
        offset = name_end;
        
        if (offset + 10 > len) break;  /* Pas assez de données */
        
        /* Lire TYPE (2 bytes) */
        uint16_t type = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        
        /* Lire CLASS (2 bytes) */
        uint16_t class = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        
        /* Lire TTL (4 bytes) - ignoré pour l'instant */
        offset += 4;
        
        /* Lire RDLENGTH (2 bytes) */
        uint16_t rdlength = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        
        if (offset + rdlength > len) break;  /* Pas assez de données */
        
        /* Vérifier si c'est un enregistrement A (IPv4) */
        if (type == DNS_TYPE_A && class == DNS_CLASS_IN && rdlength == 4) {
            /* Lire l'adresse IP (4 bytes) */
            g_pending_query.resolved_ip[0] = data[offset];
            g_pending_query.resolved_ip[1] = data[offset + 1];
            g_pending_query.resolved_ip[2] = data[offset + 2];
            g_pending_query.resolved_ip[3] = data[offset + 3];
            
            g_pending_query.completed = true;
            g_pending_query.success = true;
            
            /* Afficher le résultat */
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
            console_puts("[DNS] Resolved ");
            console_puts(g_pending_query.hostname);
            console_puts(" to ");
            print_ip_addr(g_pending_query.resolved_ip);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            return;  /* Trouvé! */
        }
        
        /* Sauter RDATA */
        offset += rdlength;
    }
    
    /* Aucun enregistrement A trouvé */
    if (!g_pending_query.success) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        console_puts("[DNS] No A record found for ");
        console_puts(g_pending_query.hostname);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        
        g_pending_query.completed = true;
        g_pending_query.success = false;
    }
}

/**
 * Vérifie si une résolution est en attente
 */
bool dns_is_pending(void)
{
    return !g_pending_query.completed && g_pending_query.id != 0;
}

/**
 * Récupère le résultat de la dernière résolution
 */
bool dns_get_result(uint8_t* out_ip)
{
    if (g_pending_query.completed && g_pending_query.success && out_ip != NULL) {
        out_ip[0] = g_pending_query.resolved_ip[0];
        out_ip[1] = g_pending_query.resolved_ip[1];
        out_ip[2] = g_pending_query.resolved_ip[2];
        out_ip[3] = g_pending_query.resolved_ip[3];
        return true;
    }
    return false;
}
