/* src/kernel/mouse.c - Implémentation du driver souris PS/2
 *
 * La souris PS/2 communique via le contrôleur 8042 (même que le clavier).
 * Elle envoie des paquets de 3 ou 4 octets selon le mode.
 */

#include "mouse.h"
#include "klog.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/idt.h"

/* Ports du contrôleur 8042 */
#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_COMMAND_PORT    0x64

/* Bits du registre de statut */
#define PS2_STATUS_OUTPUT   0x01    /* Données disponibles en lecture */
#define PS2_STATUS_INPUT    0x02    /* Buffer d'entrée plein */
#define PS2_STATUS_MOUSE    0x20    /* Données provenant de la souris */

/* Commandes du contrôleur 8042 */
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_MOUSE   0xA7
#define PS2_CMD_ENABLE_MOUSE    0xA8
#define PS2_CMD_TEST_MOUSE      0xA9
#define PS2_CMD_WRITE_MOUSE     0xD4    /* Envoie la commande suivante à la souris */

/* Commandes de la souris */
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE        0xF4
#define MOUSE_CMD_DISABLE       0xF5
#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_SET_SAMPLE    0xF3
#define MOUSE_CMD_GET_ID        0xF2
#define MOUSE_CMD_SET_RESOLUTION 0xE8

/* Réponses de la souris */
#define MOUSE_ACK               0xFA
#define MOUSE_RESEND            0xFE

/* État global */
static mouse_state_t g_mouse_state;
static mouse_callback_t g_callback = NULL;
static bool g_initialized = false;
static bool g_enabled = true;

/* Limites de l'écran (initialisées à 0, doivent être définies par mouse_set_bounds) */
static uint32_t g_screen_width = 0;
static uint32_t g_screen_height = 0;

/* Buffer pour les paquets (3 ou 4 octets) */
static uint8_t g_packet[4];
static uint8_t g_packet_index = 0;
static uint8_t g_packet_size = 3;   /* 3 pour souris standard, 4 pour souris à molette */

/* Type de souris détecté */
static uint8_t g_mouse_id = 0;

/* ============================================================================
 * FONCTIONS INTERNES - Communication avec le contrôleur 8042
 * ============================================================================ */

/* Attend que le buffer d'entrée soit vide (prêt à recevoir une commande) */
static void ps2_wait_input(void) {
    int timeout = 100000;
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT) && timeout > 0) {
        timeout--;
    }
}

/* Attend que des données soient disponibles en sortie */
static void ps2_wait_output(void) {
    int timeout = 100000;
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) && timeout > 0) {
        timeout--;
    }
}

/* Envoie une commande au contrôleur 8042 */
static void ps2_send_command(uint8_t cmd) {
    ps2_wait_input();
    outb(PS2_COMMAND_PORT, cmd);
}

/* Envoie des données au port de données */
static void ps2_send_data(uint8_t data) {
    ps2_wait_input();
    outb(PS2_DATA_PORT, data);
}

/* Lit des données du port de données */
static uint8_t ps2_read_data(void) {
    ps2_wait_output();
    return inb(PS2_DATA_PORT);
}

/* Envoie une commande à la souris (via le contrôleur) */
static uint8_t mouse_send_command(uint8_t cmd) {
    ps2_send_command(PS2_CMD_WRITE_MOUSE);
    ps2_send_data(cmd);
    return ps2_read_data();  /* Attend l'ACK */
}

/* Envoie une commande avec un argument à la souris */
static uint8_t mouse_send_command_arg(uint8_t cmd, uint8_t arg) {
    uint8_t ack = mouse_send_command(cmd);
    if (ack != MOUSE_ACK) return ack;
    
    ps2_send_command(PS2_CMD_WRITE_MOUSE);
    ps2_send_data(arg);
    return ps2_read_data();
}

/* ============================================================================
 * DÉTECTION DU TYPE DE SOURIS
 * ============================================================================ */

/* Tente d'activer le mode souris à molette (ID = 3) */
static bool mouse_enable_scroll_wheel(void) {
    /* Séquence magique pour activer la molette :
     * Set sample rate 200, puis 100, puis 80 */
    mouse_send_command_arg(MOUSE_CMD_SET_SAMPLE, 200);
    mouse_send_command_arg(MOUSE_CMD_SET_SAMPLE, 100);
    mouse_send_command_arg(MOUSE_CMD_SET_SAMPLE, 80);
    
    /* Demande l'ID de la souris */
    mouse_send_command(MOUSE_CMD_GET_ID);
    uint8_t id = ps2_read_data();
    
    if (id == 3) {
        g_mouse_id = 3;
        g_packet_size = 4;
        return true;
    }
    
    return false;
}

/* ============================================================================
 * TRAITEMENT DES PAQUETS
 * ============================================================================ */

static void mouse_process_packet(void) {
    uint8_t flags = g_packet[0];
    
    /* Vérifie le bit "always 1" (bit 3) - si absent, paquet invalide */
    if (!(flags & 0x08)) {
        g_packet_index = 0;
        return;
    }
    
    /* Récupère les boutons */
    uint8_t old_buttons = g_mouse_state.buttons;
    g_mouse_state.buttons = flags & 0x07;  /* Bits 0-2 : L, R, M */
    g_mouse_state.buttons_changed = old_buttons ^ g_mouse_state.buttons;
    
    /* Récupère le déplacement X */
    int32_t dx = g_packet[1];
    if (flags & 0x10) {  /* Bit de signe X */
        dx |= 0xFFFFFF00;  /* Extension de signe */
    }
    
    /* Récupère le déplacement Y (inversé car Y augmente vers le bas à l'écran) */
    int32_t dy = g_packet[2];
    if (flags & 0x20) {  /* Bit de signe Y */
        dy |= 0xFFFFFF00;
    }
    dy = -dy;  /* Inverse Y pour que le haut soit positif */
    
    /* Défilement (si souris à molette) */
    g_mouse_state.scroll = 0;
    if (g_packet_size == 4) {
        int8_t scroll = (int8_t)g_packet[3];
        /* Limite à -1, 0, +1 */
        if (scroll > 0) g_mouse_state.scroll = 1;
        else if (scroll < 0) g_mouse_state.scroll = -1;
    }
    
    /* Vérifie les bits d'overflow */
    if (flags & 0x40) dx = 0;  /* Overflow X */
    if (flags & 0x80) dy = 0;  /* Overflow Y */
    
    /* Met à jour le déplacement relatif */
    g_mouse_state.dx = dx;
    g_mouse_state.dy = dy;
    
    /* Met à jour la position absolue avec clipping */
    g_mouse_state.x += dx;
    g_mouse_state.y += dy;
    
    /* Clipping (seulement si les limites sont définies) */
    if (g_mouse_state.x < 0) g_mouse_state.x = 0;
    if (g_mouse_state.y < 0) g_mouse_state.y = 0;
    if (g_screen_width > 0 && g_mouse_state.x >= (int32_t)g_screen_width) 
        g_mouse_state.x = (int32_t)g_screen_width - 1;
    if (g_screen_height > 0 && g_mouse_state.y >= (int32_t)g_screen_height) 
        g_mouse_state.y = (int32_t)g_screen_height - 1;
    
    /* Appelle le callback si défini */
    if (g_callback && g_enabled) {
        g_callback(&g_mouse_state);
    }
}

/* ============================================================================
 * HANDLER D'INTERRUPTION
 * ============================================================================ */

void mouse_irq_handler(void) {
    uint8_t status = inb(PS2_STATUS_PORT);
    
    /* Vérifie que les données viennent de la souris */
    if (!(status & PS2_STATUS_OUTPUT)) return;
    if (!(status & PS2_STATUS_MOUSE)) return;
    
    uint8_t data = inb(PS2_DATA_PORT);
    
    if (!g_initialized || !g_enabled) return;
    
    /* Premier octet : doit avoir le bit 3 à 1 */
    if (g_packet_index == 0 && !(data & 0x08)) {
        /* Paquet désynchronisé, ignore */
        return;
    }
    
    g_packet[g_packet_index++] = data;
    
    if (g_packet_index >= g_packet_size) {
        mouse_process_packet();
        g_packet_index = 0;
    }
}

/* ============================================================================
 * FONCTIONS PUBLIQUES
 * ============================================================================ */

int mouse_init(void) {
    KLOG_INFO("MOUSE", "Initializing PS/2 mouse driver...");
    
    /* Réinitialise l'état (position sera définie par mouse_set_position) */
    g_mouse_state.x = 0;
    g_mouse_state.y = 0;
    g_mouse_state.dx = 0;
    g_mouse_state.dy = 0;
    g_mouse_state.scroll = 0;
    g_mouse_state.buttons = 0;
    g_mouse_state.buttons_changed = 0;
    g_packet_index = 0;
    
    /* Active le port auxiliaire (souris) */
    ps2_send_command(PS2_CMD_ENABLE_MOUSE);
    
    /* Lit la configuration actuelle */
    ps2_send_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read_data();
    
    /* Active l'IRQ12 (bit 1) et désactive le clock disable (bit 5) */
    config |= 0x02;   /* Enable IRQ12 */
    config &= ~0x20;  /* Enable mouse clock */
    
    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(config);
    
    /* Reset la souris */
    uint8_t ack = mouse_send_command(MOUSE_CMD_RESET);
    if (ack == MOUSE_ACK) {
        /* Attend le self-test (0xAA) et l'ID (0x00) */
        ps2_read_data();  /* 0xAA */
        ps2_read_data();  /* 0x00 */
    }
    
    /* Configure les paramètres par défaut */
    mouse_send_command(MOUSE_CMD_SET_DEFAULTS);
    
    /* Tente d'activer la molette */
    if (mouse_enable_scroll_wheel()) {
        KLOG_INFO("MOUSE", "Scroll wheel detected (ID=3)");
    } else {
        KLOG_INFO("MOUSE", "Standard mouse (ID=0)");
        g_mouse_id = 0;
        g_packet_size = 3;
    }
    
    /* Configure le sample rate (100 samples/sec) */
    mouse_send_command_arg(MOUSE_CMD_SET_SAMPLE, 100);
    
    /* Configure la résolution (8 counts/mm) */
    mouse_send_command_arg(MOUSE_CMD_SET_RESOLUTION, 3);
    
    /* Active la souris */
    ack = mouse_send_command(MOUSE_CMD_ENABLE);
    if (ack != MOUSE_ACK) {
        KLOG_ERROR("MOUSE", "Failed to enable mouse");
        return -1;
    }
    
    g_initialized = true;
    g_enabled = true;
    
    KLOG_INFO("MOUSE", "PS/2 mouse initialized successfully");
    
    return 0;
}

void mouse_set_bounds(uint32_t width, uint32_t height) {
    g_screen_width = width;
    g_screen_height = height;
    
    /* Recentre la souris si elle est hors limites */
    if (g_mouse_state.x >= (int32_t)width) 
        g_mouse_state.x = (int32_t)width - 1;
    if (g_mouse_state.y >= (int32_t)height) 
        g_mouse_state.y = (int32_t)height - 1;
}

void mouse_set_position(int32_t x, int32_t y) {
    g_mouse_state.x = x;
    g_mouse_state.y = y;
    
    /* Clipping */
    if (g_mouse_state.x < 0) g_mouse_state.x = 0;
    if (g_mouse_state.y < 0) g_mouse_state.y = 0;
    if (g_mouse_state.x >= (int32_t)g_screen_width) 
        g_mouse_state.x = (int32_t)g_screen_width - 1;
    if (g_mouse_state.y >= (int32_t)g_screen_height) 
        g_mouse_state.y = (int32_t)g_screen_height - 1;
}

const mouse_state_t* mouse_get_state(void) {
    return &g_mouse_state;
}

void mouse_set_callback(mouse_callback_t callback) {
    g_callback = callback;
}

bool mouse_button_pressed(uint8_t button) {
    return (g_mouse_state.buttons & button) != 0;
}

bool mouse_button_just_pressed(uint8_t button) {
    return (g_mouse_state.buttons & button) && 
           (g_mouse_state.buttons_changed & button);
}

bool mouse_button_just_released(uint8_t button) {
    return !(g_mouse_state.buttons & button) && 
           (g_mouse_state.buttons_changed & button);
}

void mouse_enable(bool enabled) {
    g_enabled = enabled;
}

bool mouse_is_available(void) {
    return g_initialized && g_enabled;
}
