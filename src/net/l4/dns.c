/* src/net/l4/dns.c - DNS Resolver Client Implementation */
#include "dns.h"
#include "udp.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/console.h"
#include "../../mm/kheap.h"

/* ========================================
 * Variables globales
 * ======================================== */

static uint32_t g_dns_server = 0;
static uint8_t g_dns_server_bytes[4] = {0};
static bool g_dns_initialized = false;
static uint16_t g_dns_transaction_id = 0x1234;
static dns_pending_query_t g_pending_query = {0};

/* Cache DNS */
static dns_cache_entry_t g_dns_cache[DNS_CACHE_SIZE];
static uint32_t g_cache_hits = 0;
static uint32_t g_cache_misses = 0;

/* ========================================
 * Fonctions utilitaires internes
 * ======================================== */

static void ip_to_bytes(uint32_t ip, uint8_t* out)
{
    out[0] = (ip >> 24) & 0xFF;
    out[1] = (ip >> 16) & 0xFF;
    out[2] = (ip >> 8) & 0xFF;
    out[3] = ip & 0xFF;
}

static void print_ip_addr(const uint8_t* ip)
{
    for (int i = 0; i < 4; i++) {
        if (i > 0) console_putc('.');
        console_put_dec(ip[i]);
    }
}

static void str_copy(char* dest, const char* src, int max_len)
{
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int str_len(const char* s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

static bool str_equal(const char* a, const char* b)
{
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return *a == *b;
}

static bool ip_equal(const uint8_t* a, const uint8_t* b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

/* ========================================
 * Cache DNS
 * ======================================== */

void dns_cache_flush(void)
{
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        g_dns_cache[i].valid = false;
    }
    g_cache_hits = 0;
    g_cache_misses = 0;
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[DNS] Cache flushed\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static int dns_cache_find_slot(void)
{
    /* Chercher un slot libre */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!g_dns_cache[i].valid) return i;
    }
    /* Sinon, écraser le plus ancien (simple: le premier) */
    return 0;
}

void dns_cache_add(const char* hostname, const uint8_t* ip, uint32_t ttl)
{
    int slot = dns_cache_find_slot();
    dns_cache_entry_t* entry = &g_dns_cache[slot];
    
    str_copy(entry->hostname, hostname, sizeof(entry->hostname));
    entry->ip[0] = ip[0];
    entry->ip[1] = ip[1];
    entry->ip[2] = ip[2];
    entry->ip[3] = ip[3];
    entry->ttl = ttl > 0 ? ttl : DNS_CACHE_DEFAULT_TTL;
    entry->timestamp = 0;  /* TODO: utiliser un vrai timer */
    entry->record_type = DNS_TYPE_A;
    entry->valid = true;
}

void dns_cache_add_ptr(const uint8_t* ip, const char* hostname, uint32_t ttl)
{
    int slot = dns_cache_find_slot();
    dns_cache_entry_t* entry = &g_dns_cache[slot];
    
    /* Pour PTR, on stocke l'IP dans le champ ip et le nom dans hostname */
    entry->ip[0] = ip[0];
    entry->ip[1] = ip[1];
    entry->ip[2] = ip[2];
    entry->ip[3] = ip[3];
    str_copy(entry->hostname, hostname, sizeof(entry->hostname));
    entry->ttl = ttl > 0 ? ttl : DNS_CACHE_DEFAULT_TTL;
    entry->timestamp = 0;
    entry->record_type = DNS_TYPE_PTR;
    entry->valid = true;
}

bool dns_cache_lookup(const char* hostname, uint8_t* out_ip)
{
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache_entry_t* entry = &g_dns_cache[i];
        if (entry->valid && entry->record_type == DNS_TYPE_A) {
            if (str_equal(entry->hostname, hostname)) {
                out_ip[0] = entry->ip[0];
                out_ip[1] = entry->ip[1];
                out_ip[2] = entry->ip[2];
                out_ip[3] = entry->ip[3];
                g_cache_hits++;
                
                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                console_puts("[DNS] Cache hit: ");
                console_puts(hostname);
                console_puts(" -> ");
                print_ip_addr(out_ip);
                console_puts("\n");
                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                return true;
            }
        }
    }
    g_cache_misses++;
    return false;
}

bool dns_cache_reverse_lookup(const uint8_t* ip, char* out_name, int max_len)
{
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache_entry_t* entry = &g_dns_cache[i];
        if (entry->valid && entry->record_type == DNS_TYPE_PTR) {
            if (ip_equal(entry->ip, ip)) {
                str_copy(out_name, entry->hostname, max_len);
                g_cache_hits++;
                return true;
            }
        }
    }
    g_cache_misses++;
    return false;
}

void dns_cache_stats(void)
{
    int count = 0;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (g_dns_cache[i].valid) count++;
    }
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[DNS] Cache stats: ");
    console_put_dec(count);
    console_puts("/");
    console_put_dec(DNS_CACHE_SIZE);
    console_puts(" entries, ");
    console_put_dec(g_cache_hits);
    console_puts(" hits, ");
    console_put_dec(g_cache_misses);
    console_puts(" misses\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/* ========================================
 * Fonctions principales
 * ======================================== */

void dns_init(uint32_t dns_server)
{
    g_dns_server = dns_server;
    ip_to_bytes(dns_server, g_dns_server_bytes);
    g_dns_initialized = true;
    
    g_pending_query.id = 0;
    g_pending_query.hostname[0] = '\0';
    g_pending_query.completed = false;
    g_pending_query.success = false;
    g_pending_query.has_cname = false;
    
    /* Initialiser le cache */
    dns_cache_flush();
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[DNS] Resolver initialized, server: ");
    print_ip_addr(g_dns_server_bytes);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

int dns_encode_name(uint8_t* buffer, const char* hostname)
{
    int pos = 0;
    int label_start = 0;
    int i = 0;
    
    while (1) {
        if (hostname[i] == '.' || hostname[i] == '\0') {
            int label_len = i - label_start;
            buffer[pos++] = (uint8_t)label_len;
            
            for (int j = label_start; j < i; j++) {
                buffer[pos++] = (uint8_t)hostname[j];
            }
            
            if (hostname[i] == '\0') {
                buffer[pos++] = 0;
                break;
            }
            label_start = i + 1;
        }
        i++;
    }
    return pos;
}

/**
 * Décode un nom DNS (avec support de compression)
 */
static int dns_decode_name(uint8_t* packet, int offset, int max_len, 
                           char* out_name, int out_max)
{
    int pos = 0;
    int jumped = 0;
    int orig_offset = offset;
    int count = 0;
    
    while (offset < max_len && count < 256) {
        uint8_t len = packet[offset];
        
        if (len == 0) {
            if (pos > 0) pos--;  /* Enlever le dernier '.' */
            out_name[pos] = '\0';
            return jumped ? jumped : (offset + 1 - orig_offset);
        }
        
        if ((len & 0xC0) == 0xC0) {
            /* Compression pointer */
            if (!jumped) {
                jumped = offset + 2 - orig_offset;
            }
            offset = ((len & 0x3F) << 8) | packet[offset + 1];
        } else {
            offset++;
            for (int i = 0; i < len && pos < out_max - 2; i++) {
                out_name[pos++] = packet[offset++];
            }
            out_name[pos++] = '.';
        }
        count++;
    }
    
    out_name[pos] = '\0';
    return jumped ? jumped : (offset - orig_offset);
}

void dns_send_query(const char* hostname)
{
    if (!g_dns_initialized) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[DNS] Error: resolver not initialized!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* Vérifier le cache d'abord */
    uint8_t cached_ip[4];
    if (dns_cache_lookup(hostname, cached_ip)) {
        g_pending_query.resolved_ip[0] = cached_ip[0];
        g_pending_query.resolved_ip[1] = cached_ip[1];
        g_pending_query.resolved_ip[2] = cached_ip[2];
        g_pending_query.resolved_ip[3] = cached_ip[3];
        g_pending_query.completed = true;
        g_pending_query.success = true;
        str_copy(g_pending_query.hostname, hostname, sizeof(g_pending_query.hostname));
        return;
    }
    
    uint8_t buffer[DNS_MAX_PACKET_SIZE];
    int offset = 0;
    
    g_dns_transaction_id++;
    
    dns_header_t* header = (dns_header_t*)buffer;
    header->id = htons(g_dns_transaction_id);
    header->flags = htons(DNS_FLAG_RD);
    header->qd_count = htons(1);
    header->an_count = 0;
    header->ns_count = 0;
    header->ar_count = 0;
    
    offset = DNS_HEADER_SIZE;
    
    int name_len = dns_encode_name(buffer + offset, hostname);
    offset += name_len;
    
    buffer[offset++] = 0x00;
    buffer[offset++] = DNS_TYPE_A;
    buffer[offset++] = 0x00;
    buffer[offset++] = DNS_CLASS_IN;
    
    g_pending_query.id = g_dns_transaction_id;
    str_copy(g_pending_query.hostname, hostname, sizeof(g_pending_query.hostname));
    g_pending_query.type = DNS_QUERY_A;
    g_pending_query.completed = false;
    g_pending_query.success = false;
    g_pending_query.has_cname = false;
    g_pending_query.cname[0] = '\0';
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[DNS] Resolving: ");
    console_puts(hostname);
    console_puts(" (ID: 0x");
    console_put_hex(g_dns_transaction_id);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    udp_send_packet(g_dns_server_bytes, 12345, DNS_PORT, buffer, offset);
}

void dns_send_reverse_query(const uint8_t* ip)
{
    if (!g_dns_initialized) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[DNS] Error: resolver not initialized!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    /* Vérifier le cache d'abord */
    char cached_name[64];
    if (dns_cache_reverse_lookup(ip, cached_name, sizeof(cached_name))) {
        str_copy(g_pending_query.resolved_name, cached_name, sizeof(g_pending_query.resolved_name));
        g_pending_query.completed = true;
        g_pending_query.success = true;
        return;
    }
    
    /* Construire le nom PTR: 4.3.2.1.in-addr.arpa */
    char ptr_name[64];
    int pos = 0;
    
    /* IP inversée */
    for (int i = 3; i >= 0; i--) {
        uint8_t octet = ip[i];
        if (octet >= 100) {
            ptr_name[pos++] = '0' + (octet / 100);
            octet %= 100;
            ptr_name[pos++] = '0' + (octet / 10);
            ptr_name[pos++] = '0' + (octet % 10);
        } else if (octet >= 10) {
            ptr_name[pos++] = '0' + (octet / 10);
            ptr_name[pos++] = '0' + (octet % 10);
        } else {
            ptr_name[pos++] = '0' + octet;
        }
        ptr_name[pos++] = '.';
    }
    
    /* Ajouter "in-addr.arpa" */
    const char* suffix = "in-addr.arpa";
    while (*suffix) {
        ptr_name[pos++] = *suffix++;
    }
    ptr_name[pos] = '\0';
    
    uint8_t buffer[DNS_MAX_PACKET_SIZE];
    int offset = 0;
    
    g_dns_transaction_id++;
    
    dns_header_t* header = (dns_header_t*)buffer;
    header->id = htons(g_dns_transaction_id);
    header->flags = htons(DNS_FLAG_RD);
    header->qd_count = htons(1);
    header->an_count = 0;
    header->ns_count = 0;
    header->ar_count = 0;
    
    offset = DNS_HEADER_SIZE;
    
    int name_len = dns_encode_name(buffer + offset, ptr_name);
    offset += name_len;
    
    buffer[offset++] = 0x00;
    buffer[offset++] = DNS_TYPE_PTR;
    buffer[offset++] = 0x00;
    buffer[offset++] = DNS_CLASS_IN;
    
    g_pending_query.id = g_dns_transaction_id;
    str_copy(g_pending_query.hostname, ptr_name, sizeof(g_pending_query.hostname));
    g_pending_query.resolved_ip[0] = ip[0];
    g_pending_query.resolved_ip[1] = ip[1];
    g_pending_query.resolved_ip[2] = ip[2];
    g_pending_query.resolved_ip[3] = ip[3];
    g_pending_query.type = DNS_QUERY_PTR;
    g_pending_query.completed = false;
    g_pending_query.success = false;
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[DNS] Reverse lookup: ");
    print_ip_addr(ip);
    console_puts(" (ID: 0x");
    console_put_hex(g_dns_transaction_id);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    udp_send_packet(g_dns_server_bytes, 12345, DNS_PORT, buffer, offset);
}

static int dns_skip_name(uint8_t* data, int offset, int max_len)
{
    int jumped = 0;
    int count = 0;
    
    while (offset < max_len && count < 256) {
        uint8_t len = data[offset];
        
        if (len == 0) {
            return jumped ? jumped : (offset + 1);
        }
        
        if ((len & 0xC0) == 0xC0) {
            if (!jumped) {
                jumped = offset + 2;
            }
            offset = ((len & 0x3F) << 8) | data[offset + 1];
        } else {
            offset += len + 1;
        }
        count++;
    }
    
    return jumped ? jumped : offset;
}

void dns_handle_packet(uint8_t* data, int len)
{
    if (data == NULL || len < DNS_HEADER_SIZE) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[DNS] Packet too short\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    dns_header_t* header = (dns_header_t*)data;
    
    uint16_t id = ntohs(header->id);
    uint16_t flags = ntohs(header->flags);
    uint16_t qd_count = ntohs(header->qd_count);
    uint16_t an_count = ntohs(header->an_count);
    
    if (!(flags & DNS_FLAG_QR)) {
        return;
    }
    
    if (id != g_pending_query.id) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        console_puts("[DNS] Transaction ID mismatch\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    uint8_t rcode = flags & DNS_FLAG_RCODE;
    if (rcode != DNS_RCODE_OK) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("[DNS] Error RCODE: ");
        console_put_dec(rcode);
        if (rcode == DNS_RCODE_NXDOMAIN) {
            console_puts(" (NXDOMAIN)");
        }
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        g_pending_query.completed = true;
        g_pending_query.success = false;
        return;
    }
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("[DNS] Response: ");
    console_put_dec(an_count);
    console_puts(" answer(s)\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    if (an_count == 0) {
        g_pending_query.completed = true;
        g_pending_query.success = false;
        return;
    }
    
    /* Sauter la section Question */
    int offset = DNS_HEADER_SIZE;
    for (uint16_t i = 0; i < qd_count && offset < len; i++) {
        offset = dns_skip_name(data, offset, len);
        offset += 4;
    }
    
    /* Parser la section Answer */
    for (uint16_t i = 0; i < an_count && offset < len; i++) {
        int name_end = dns_skip_name(data, offset, len);
        offset = name_end;
        
        if (offset + 10 > len) break;
        
        uint16_t type = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        
        uint16_t class = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        
        uint32_t ttl = (data[offset] << 24) | (data[offset + 1] << 16) |
                       (data[offset + 2] << 8) | data[offset + 3];
        offset += 4;
        
        uint16_t rdlength = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        
        if (offset + rdlength > len) break;
        
        /* Type A - Adresse IPv4 */
        if (type == DNS_TYPE_A && class == DNS_CLASS_IN && rdlength == 4) {
            g_pending_query.resolved_ip[0] = data[offset];
            g_pending_query.resolved_ip[1] = data[offset + 1];
            g_pending_query.resolved_ip[2] = data[offset + 2];
            g_pending_query.resolved_ip[3] = data[offset + 3];
            
            g_pending_query.completed = true;
            g_pending_query.success = true;
            
            /* Ajouter au cache */
            dns_cache_add(g_pending_query.hostname, g_pending_query.resolved_ip, ttl);
            
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            console_puts("[DNS] Resolved ");
            console_puts(g_pending_query.hostname);
            console_puts(" -> ");
            print_ip_addr(g_pending_query.resolved_ip);
            if (g_pending_query.has_cname) {
                console_puts(" (via CNAME: ");
                console_puts(g_pending_query.cname);
                console_puts(")");
            }
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            
            return;
        }
        
        /* Type CNAME - Alias */
        if (type == DNS_TYPE_CNAME && class == DNS_CLASS_IN) {
            char cname[64];
            dns_decode_name(data, offset, len, cname, sizeof(cname));
            str_copy(g_pending_query.cname, cname, sizeof(g_pending_query.cname));
            g_pending_query.has_cname = true;
            
            console_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
            console_puts("[DNS] CNAME: ");
            console_puts(g_pending_query.hostname);
            console_puts(" -> ");
            console_puts(cname);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            
            /* Continuer pour trouver le A record */
        }
        
        /* Type PTR - Résolution inverse */
        if (type == DNS_TYPE_PTR && class == DNS_CLASS_IN) {
            char ptr_name[64];
            dns_decode_name(data, offset, len, ptr_name, sizeof(ptr_name));
            str_copy(g_pending_query.resolved_name, ptr_name, sizeof(g_pending_query.resolved_name));
            
            g_pending_query.completed = true;
            g_pending_query.success = true;
            
            /* Ajouter au cache */
            dns_cache_add_ptr(g_pending_query.resolved_ip, ptr_name, ttl);
            
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            console_puts("[DNS] Reverse: ");
            print_ip_addr(g_pending_query.resolved_ip);
            console_puts(" -> ");
            console_puts(ptr_name);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            
            return;
        }
        
        offset += rdlength;
    }
    
    if (!g_pending_query.success) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        console_puts("[DNS] No matching record found\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        g_pending_query.completed = true;
        g_pending_query.success = false;
    }
}

bool dns_is_pending(void)
{
    return !g_pending_query.completed && g_pending_query.id != 0;
}

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

bool dns_get_reverse_result(char* out_name, int max_len)
{
    if (g_pending_query.completed && g_pending_query.success && 
        g_pending_query.type == DNS_QUERY_PTR && out_name != NULL) {
        str_copy(out_name, g_pending_query.resolved_name, max_len);
        return true;
    }
    return false;
}

bool dns_get_cname(char* out_cname, int max_len)
{
    if (g_pending_query.has_cname && out_cname != NULL) {
        str_copy(out_cname, g_pending_query.cname, max_len);
        return true;
    }
    return false;
}
