/* src/net/l4/dhcp.c - DHCP Client Implementation */
#include "dhcp.h"
#include "udp.h"
#include "../l3/ipv4.h"
#include "../l3/route.h"
#include "../l2/ethernet.h"
#include "../core/net.h"
#include "../core/netdev.h"
#include "../utils.h"
#include "../../kernel/console.h"
#include "../../mm/kheap.h"

/* ========================================
 * Contexte DHCP global
 * ======================================== */

static dhcp_context_t dhcp_ctx;
static bool dhcp_initialized = false;

/* Compteur pour générer des transaction IDs pseudo-aléatoires */
static uint32_t dhcp_xid_counter = 0x12345678;

/* ========================================
 * Fonctions utilitaires
 * ======================================== */

/**
 * Affiche une adresse IP au format X.X.X.X
 */
static void print_ip_u32(uint32_t ip)
{
    uint8_t bytes[4];
    ip_u32_to_bytes(ip, bytes);
    for (int i = 0; i < 4; i++) {
        if (i > 0) console_putc('.');
        console_put_dec(bytes[i]);
    }
}

/**
 * Génère un transaction ID pseudo-aléatoire.
 */
static uint32_t dhcp_generate_xid(void)
{
    /* Simple LCG (Linear Congruential Generator) */
    dhcp_xid_counter = dhcp_xid_counter * 1103515245 + 12345;
    return dhcp_xid_counter;
}

/**
 * Met à zéro un buffer.
 */
static void mem_zero(void* ptr, int len)
{
    uint8_t* p = (uint8_t*)ptr;
    for (int i = 0; i < len; i++) {
        p[i] = 0;
    }
}

/**
 * Copie un buffer.
 */
static void mem_copy(void* dest, const void* src, int len)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (int i = 0; i < len; i++) {
        d[i] = s[i];
    }
}

/* ========================================
 * Construction des paquets DHCP
 * ======================================== */

/**
 * Construit l'en-tête DHCP de base.
 */
static int dhcp_build_header(NetInterface* netif, uint8_t* buffer, int buflen,
                              uint8_t msg_type, uint32_t xid)
{
    if (buflen < DHCP_HEADER_SIZE + 64) {
        return -1;  /* Buffer trop petit */
    }
    
    mem_zero(buffer, DHCP_HEADER_SIZE + 64);
    
    dhcp_header_t* dhcp = (dhcp_header_t*)buffer;
    
    /* Header fixe */
    dhcp->op = DHCP_BOOTREQUEST;
    dhcp->htype = DHCP_HTYPE_ETH;
    dhcp->hlen = DHCP_HLEN_ETH;
    dhcp->hops = 0;
    dhcp->xid = htonl(xid);
    dhcp->secs = 0;
    dhcp->flags = htons(0x8000);  /* Broadcast flag */
    
    /* Adresses IP (toutes à 0 pour DISCOVER) */
    dhcp->ciaddr = 0;
    dhcp->yiaddr = 0;
    dhcp->siaddr = 0;
    dhcp->giaddr = 0;
    
    /* Copier notre MAC dans chaddr (16 bytes, mais seulement 6 utilisés) */
    mem_copy(dhcp->chaddr, netif->mac_addr, 6);
    
    /* Magic cookie (après le header de 236 bytes) */
    uint8_t* options = buffer + DHCP_HEADER_SIZE;
    options[0] = 0x63;
    options[1] = 0x82;
    options[2] = 0x53;
    options[3] = 0x63;
    
    /* Option 53: Message Type (commence après le magic cookie) */
    int opt_idx = 4;  /* Position après le cookie */
    options[opt_idx++] = DHCP_OPT_MESSAGE_TYPE;
    options[opt_idx++] = 1;
    options[opt_idx++] = msg_type;
    
    /* Retourne l'index courant dans les options (pour continuer à ajouter) */
    return opt_idx;
}

/**
 * Ajoute les options demandées dans un paquet DHCP.
 */
static int dhcp_add_param_request(uint8_t* options, int start_idx)
{
    int idx = start_idx;
    
    /* Option 55: Parameter Request List */
    options[idx++] = DHCP_OPT_PARAM_REQUEST;
    options[idx++] = 4;  /* Nombre de paramètres demandés */
    options[idx++] = DHCP_OPT_SUBNET_MASK;
    options[idx++] = DHCP_OPT_ROUTER;
    options[idx++] = DHCP_OPT_DNS;
    options[idx++] = DHCP_OPT_LEASE_TIME;
    
    return idx;
}

/**
 * Finalise les options avec END.
 */
static int dhcp_finalize_options(uint8_t* options, int idx)
{
    options[idx++] = DHCP_OPT_END;
    return idx;
}

/* ========================================
 * Envoi de paquets DHCP
 * ======================================== */

/**
 * Envoie un paquet DHCP en broadcast.
 * DHCP utilise UDP sur les ports 67 (serveur) et 68 (client).
 * Comme on n'a pas encore d'IP, on doit envoyer en broadcast L2 et L3.
 */
static void dhcp_send_raw(NetInterface* netif, uint8_t* dhcp_data, int dhcp_len)
{
    /* Buffer pour le paquet complet: Ethernet + IP + UDP + DHCP */
    uint8_t packet[1518];
    int offset = 0;
    
    /* === En-tête Ethernet === */
    ethernet_header_t* eth = (ethernet_header_t*)packet;
    
    /* Destination: broadcast FF:FF:FF:FF:FF:FF */
    for (int i = 0; i < 6; i++) {
        eth->dest_mac[i] = 0xFF;
    }
    
    /* Source: notre MAC */
    mem_copy(eth->src_mac, netif->mac_addr, 6);
    
    /* EtherType: IPv4 */
    eth->ethertype = htons(ETHERTYPE_IPV4);
    offset += ETHERNET_HEADER_SIZE;
    
    /* === En-tête IPv4 === */
    ipv4_header_t* ip = (ipv4_header_t*)(packet + offset);
    
    ip->version_ihl = 0x45;  /* IPv4, IHL=5 (20 bytes) */
    ip->tos = 0;
    ip->total_length = htons(IPV4_HEADER_SIZE + UDP_HEADER_SIZE + dhcp_len);
    ip->identification = htons(0);
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    ip->checksum = 0;
    
    /* Source: 0.0.0.0 (on n'a pas encore d'IP) */
    ip->src_ip[0] = 0;
    ip->src_ip[1] = 0;
    ip->src_ip[2] = 0;
    ip->src_ip[3] = 0;
    
    /* Destination: 255.255.255.255 (broadcast) */
    ip->dest_ip[0] = 255;
    ip->dest_ip[1] = 255;
    ip->dest_ip[2] = 255;
    ip->dest_ip[3] = 255;
    
    /* Calculer le checksum IP */
    ip->checksum = ip_checksum(ip, IPV4_HEADER_SIZE);
    offset += IPV4_HEADER_SIZE;
    
    /* === En-tête UDP === */
    udp_header_t* udp = (udp_header_t*)(packet + offset);
    
    udp->src_port = htons(DHCP_CLIENT_PORT);
    udp->dest_port = htons(DHCP_SERVER_PORT);
    udp->length = htons(UDP_HEADER_SIZE + dhcp_len);
    udp->checksum = 0;  /* Optionnel en IPv4 */
    offset += UDP_HEADER_SIZE;
    
    /* === Payload DHCP === */
    mem_copy(packet + offset, dhcp_data, dhcp_len);
    offset += dhcp_len;
    
    /* Padding à 60 bytes minimum (Ethernet) */
    while (offset < 60) {
        packet[offset++] = 0;
    }
    
    /* === Envoyer le paquet === */
    if (netif->send != NULL) {
        netif->send(netif, packet, offset);
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[DHCP] No send function on interface!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    }
}

/* ========================================
 * Fonctions DHCP principales
 * ======================================== */

/**
 * Initialise le client DHCP.
 */
void dhcp_init(NetInterface* netif)
{
    mem_zero(&dhcp_ctx, sizeof(dhcp_ctx));
    
    dhcp_ctx.netif = netif;
    dhcp_ctx.state = DHCP_STATE_INIT;
    dhcp_ctx.xid = 0;
    
    dhcp_initialized = true;
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    console_puts("[DHCP] Client initialized for interface: ");
    console_puts(netif->name);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

/**
 * Envoie un DHCPDISCOVER.
 */
int dhcp_discover(NetInterface* netif)
{
    if (!dhcp_initialized || dhcp_ctx.netif != netif) {
        dhcp_init(netif);
    }
    
    /* Générer un nouveau transaction ID */
    dhcp_ctx.xid = dhcp_generate_xid();
    dhcp_ctx.state = DHCP_STATE_SELECTING;
    dhcp_ctx.discover_count++;
    
    console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
    console_puts("[DHCP] Discovering...\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* Construire le paquet DHCP DISCOVER */
    uint8_t dhcp_packet[576];  /* Taille minimale BOOTP */
    
    int opt_idx = dhcp_build_header(netif, dhcp_packet, sizeof(dhcp_packet),
                                     DHCPDISCOVER, dhcp_ctx.xid);
    if (opt_idx < 0) {
        return -1;
    }
    
    /* Ajouter les options (opt_idx est l'index courant dans la zone options) */
    uint8_t* options = dhcp_packet + DHCP_HEADER_SIZE;
    
    opt_idx = dhcp_add_param_request(options, opt_idx);
    opt_idx = dhcp_finalize_options(options, opt_idx);
    
    /* Longueur totale */
    int total_len = DHCP_HEADER_SIZE + opt_idx;
    
    /* Padding à 300 bytes minimum (requis par certains serveurs) */
    while (total_len < 300) {
        dhcp_packet[total_len++] = 0;
    }
    
    /* Envoyer */
    dhcp_send_raw(netif, dhcp_packet, total_len);
    
    return 0;
}

/**
 * Envoie un DHCPREQUEST (pour accepter une offre).
 */
static int dhcp_send_request(NetInterface* netif)
{
    /* Pas de log verbose - on attend juste le ACK */
    dhcp_ctx.state = DHCP_STATE_REQUESTING;
    dhcp_ctx.request_count++;
    
    /* Construire le paquet DHCP REQUEST */
    uint8_t dhcp_packet[576];
    
    int opt_idx = dhcp_build_header(netif, dhcp_packet, sizeof(dhcp_packet),
                                     DHCPREQUEST, dhcp_ctx.xid);
    if (opt_idx < 0) {
        return -1;
    }
    
    /* Ajouter les options spécifiques au REQUEST */
    uint8_t* options = dhcp_packet + DHCP_HEADER_SIZE;
    
    /* Option 50: Requested IP Address */
    uint8_t requested_ip[4];
    ip_u32_to_bytes(dhcp_ctx.offered_ip, requested_ip);
    options[opt_idx++] = DHCP_OPT_REQUESTED_IP;
    options[opt_idx++] = 4;
    mem_copy(&options[opt_idx], requested_ip, 4);
    opt_idx += 4;
    
    /* Option 54: Server Identifier */
    uint8_t server_ip[4];
    ip_u32_to_bytes(dhcp_ctx.server_ip, server_ip);
    options[opt_idx++] = DHCP_OPT_SERVER_ID;
    options[opt_idx++] = 4;
    mem_copy(&options[opt_idx], server_ip, 4);
    opt_idx += 4;
    
    /* Option 55: Parameter Request List */
    opt_idx = dhcp_add_param_request(options, opt_idx);
    
    /* END */
    opt_idx = dhcp_finalize_options(options, opt_idx);
    
    /* Longueur totale avec padding */
    int total_len = DHCP_HEADER_SIZE + opt_idx;
    while (total_len < 300) {
        dhcp_packet[total_len++] = 0;
    }
    
    /* Envoyer */
    dhcp_send_raw(netif, dhcp_packet, total_len);
    
    return 0;
}

/**
 * Parse les options DHCP et extrait les informations.
 */
static void dhcp_parse_options(uint8_t* options, int len,
                                uint8_t* msg_type, uint32_t* server_ip,
                                uint32_t* subnet_mask, uint32_t* router,
                                uint32_t* dns, uint32_t* lease_time)
{
    int i = 0;
    
    *msg_type = 0;
    *server_ip = 0;
    *subnet_mask = 0;
    *router = 0;
    *dns = 0;
    *lease_time = 0;
    
    while (i < len) {
        uint8_t opt = options[i++];
        
        if (opt == DHCP_OPT_PAD) {
            continue;
        }
        
        if (opt == DHCP_OPT_END) {
            break;
        }
        
        if (i >= len) break;
        uint8_t opt_len = options[i++];
        
        if (i + opt_len > len) break;
        
        switch (opt) {
            case DHCP_OPT_MESSAGE_TYPE:
                if (opt_len >= 1) {
                    *msg_type = options[i];
                }
                break;
                
            case DHCP_OPT_SERVER_ID:
                if (opt_len >= 4) {
                    *server_ip = ip_bytes_to_u32(&options[i]);
                }
                break;
                
            case DHCP_OPT_SUBNET_MASK:
                if (opt_len >= 4) {
                    *subnet_mask = ip_bytes_to_u32(&options[i]);
                }
                break;
                
            case DHCP_OPT_ROUTER:
                if (opt_len >= 4) {
                    *router = ip_bytes_to_u32(&options[i]);
                }
                break;
                
            case DHCP_OPT_DNS:
                if (opt_len >= 4) {
                    *dns = ip_bytes_to_u32(&options[i]);
                }
                break;
                
            case DHCP_OPT_LEASE_TIME:
                if (opt_len >= 4) {
                    *lease_time = ntohl(*(uint32_t*)&options[i]);
                }
                break;
        }
        
        i += opt_len;
    }
}

/**
 * Traite un DHCPOFFER reçu.
 */
static void dhcp_handle_offer(NetInterface* netif, dhcp_header_t* dhcp,
                               uint8_t* options, int opt_len)
{
    uint8_t msg_type;
    uint32_t server_ip, subnet_mask, router, dns, lease_time;
    
    dhcp_parse_options(options, opt_len, &msg_type, &server_ip,
                       &subnet_mask, &router, &dns, &lease_time);
    
    if (msg_type != DHCPOFFER) {
        return;  /* Pas un OFFER */
    }
    
    /* Extraire l'IP offerte */
    dhcp_ctx.offered_ip = ip_bytes_to_u32((uint8_t*)&dhcp->yiaddr);
    dhcp_ctx.server_ip = server_ip;
    
    /* Envoyer un REQUEST pour accepter l'offre (pas de log) */
    dhcp_send_request(netif);
}

/**
 * Traite un DHCPACK reçu.
 */
static void dhcp_handle_ack(NetInterface* netif, dhcp_header_t* dhcp,
                             uint8_t* options, int opt_len)
{
    uint8_t msg_type;
    uint32_t server_ip, subnet_mask, router, dns, lease_time;
    
    dhcp_parse_options(options, opt_len, &msg_type, &server_ip,
                       &subnet_mask, &router, &dns, &lease_time);
    
    if (msg_type == DHCPNAK) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[DHCP] Received NAK - configuration rejected!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        dhcp_ctx.state = DHCP_STATE_INIT;
        return;
    }
    
    if (msg_type != DHCPACK) {
        return;
    }
    
    /* Configuration acceptée ! */
    dhcp_ctx.state = DHCP_STATE_BOUND;
    dhcp_ctx.lease_time = lease_time;
    
    /* Appliquer la configuration à l'interface */
    uint32_t assigned_ip = ip_bytes_to_u32((uint8_t*)&dhcp->yiaddr);
    
    netif->ip_addr = assigned_ip;
    netif->netmask = subnet_mask;
    netif->gateway = router;
    netif->dns_server = dns;
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[DHCP] *** BOUND ***\n");
    console_puts("       IP Address:  ");
    print_ip_u32(netif->ip_addr);
    console_puts("\n       Subnet Mask: ");
    print_ip_u32(netif->netmask);
    console_puts("\n       Gateway:     ");
    print_ip_u32(netif->gateway);
    console_puts("\n       DNS Server:  ");
    print_ip_u32(netif->dns_server);
    console_puts("\n       Lease Time:  ");
    console_put_dec(lease_time);
    console_puts(" seconds\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* Mettre à jour les globales legacy pour compatibilité */
    ip_u32_to_bytes(netif->ip_addr, MY_IP);
    ip_u32_to_bytes(netif->gateway, GATEWAY_IP);
    ip_u32_to_bytes(netif->dns_server, DNS_IP);
    ip_u32_to_bytes(netif->netmask, NETMASK);
    
    /* Mettre à jour la table de routage avec la nouvelle configuration */
    route_update_from_netif(netif);
}

/**
 * Traite un paquet DHCP reçu.
 */
void dhcp_handle_packet(NetInterface* netif, uint8_t* data, int len)
{
    if (!dhcp_initialized) {
        return;
    }
    
    /* Utiliser l'interface par défaut si non spécifiée */
    if (netif == NULL) {
        netif = netif_get_default();
    }
    
    if (netif == NULL || netif != dhcp_ctx.netif) {
        return;
    }
    
    /* Vérifier la taille minimale */
    if (len < (int)(DHCP_HEADER_SIZE + 4)) {
        return;
    }
    
    dhcp_header_t* dhcp = (dhcp_header_t*)data;
    
    /* Vérifier que c'est une réponse (BOOTREPLY) */
    if (dhcp->op != DHCP_BOOTREPLY) {
        return;
    }
    
    /* Vérifier le transaction ID */
    if (ntohl(dhcp->xid) != dhcp_ctx.xid) {
        return;
    }
    
    /* Vérifier le magic cookie */
    uint8_t* cookie = data + DHCP_HEADER_SIZE;
    if (cookie[0] != 0x63 || cookie[1] != 0x82 ||
        cookie[2] != 0x53 || cookie[3] != 0x63) {
        return;
    }
    
    /* Parser les options pour identifier le type de message */
    uint8_t* options = data + DHCP_HEADER_SIZE + 4;
    int opt_len = len - DHCP_HEADER_SIZE - 4;
    
    /* Extraire le type de message pour debug */
    uint8_t msg_type = 0;
    uint32_t dummy1, dummy2, dummy3, dummy4, dummy5;
    dhcp_parse_options(options, opt_len, &msg_type, &dummy1, &dummy2, &dummy3, &dummy4, &dummy5);
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    console_puts("[DHCP] Message type: ");
    console_put_dec(msg_type);
    console_puts(" (1=DISCOVER, 2=OFFER, 3=REQUEST, 5=ACK, 6=NAK)\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* Traiter selon l'état */
    switch (dhcp_ctx.state) {
        case DHCP_STATE_SELECTING:
            dhcp_handle_offer(netif, dhcp, options, opt_len);
            break;
            
        case DHCP_STATE_REQUESTING:
        case DHCP_STATE_RENEWING:
        case DHCP_STATE_REBINDING:
            dhcp_handle_ack(netif, dhcp, options, opt_len);
            break;
            
        default:
            console_puts("[DHCP] Unexpected state, ignoring\n");
            break;
    }
}

/**
 * Libère le bail DHCP.
 */
void dhcp_release(NetInterface* netif)
{
    if (!dhcp_initialized || dhcp_ctx.netif != netif) {
        return;
    }
    
    console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
    console_puts("[DHCP] Releasing lease for ");
    print_ip_u32(netif->ip_addr);
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* Remettre l'interface à zéro */
    netif->ip_addr = 0;
    netif->netmask = 0;
    netif->gateway = 0;
    netif->dns_server = 0;
    
    dhcp_ctx.state = DHCP_STATE_INIT;
}

/**
 * Retourne l'état DHCP.
 */
dhcp_state_t dhcp_get_state(NetInterface* netif)
{
    if (!dhcp_initialized || dhcp_ctx.netif != netif) {
        return DHCP_STATE_INIT;
    }
    return dhcp_ctx.state;
}

/**
 * Vérifie si DHCP est lié.
 */
bool dhcp_is_bound(NetInterface* netif)
{
    return dhcp_get_state(netif) == DHCP_STATE_BOUND;
}
