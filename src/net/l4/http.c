/* src/net/l4/http.c - Simple HTTP Client Implementation */
#include "http.h"
#include "tcp.h"
#include "dns.h"
#include "../core/netdev.h"
#include "../../include/string.h"
#include "../../fs/vfs.h"
#include "../../kernel/klog.h"
#include "../../kernel/thread.h"
#include "../../mm/kheap.h"

/* Helper: Simple IP string parser */
static int parse_ip_string(const char* str, uint8_t* ip) {
    int octet = 0;
    int value = 0;
    int digits = 0;
    
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            value = value * 10 + (str[i] - '0');
            digits++;
            if (value > 255) return -1;
        } else if (str[i] == '.') {
            if (digits == 0 || octet >= 3) return -1;
            ip[octet++] = (uint8_t)value;
            value = 0;
            digits = 0;
        } else {
            return -1;
        }
    }
    
    if (digits == 0 || octet != 3) return -1;
    ip[octet] = (uint8_t)value;
    return 0;
}

/* Helper: Simple string append */
static int str_append(char* dest, int dest_size, int pos, const char* src) {
    int i = 0;
    while (src[i] && pos < dest_size - 1) {
        dest[pos++] = src[i++];
    }
    dest[pos] = '\0';
    return pos;
}

/* Helper: Parse URL into host, port, path */
static int parse_url(const char* url, char* host, uint16_t* port, char* path) {
    /* Simple URL parser: http://host:port/path */
    const char* ptr = url;
    
    /* Skip "http://" if present */
    if (strncmp(ptr, "http://", 7) == 0) {
        ptr += 7;
    }
    
    /* Extract host */
    int i = 0;
    while (*ptr && *ptr != ':' && *ptr != '/' && i < 63) {
        host[i++] = *ptr++;
    }
    host[i] = '\0';
    
    if (i == 0) return -1;  /* Empty host */
    
    /* Extract port if present */
    *port = HTTP_PORT;
    if (*ptr == ':') {
        ptr++;
        *port = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            *port = (*port * 10) + (*ptr - '0');
            ptr++;
        }
    }
    
    /* Extract path */
    if (*ptr == '/') {
        i = 0;
        while (*ptr && i < 255) {
            path[i++] = *ptr++;
        }
        path[i] = '\0';
    } else {
        strcpy(path, "/");
    }
    
    return 0;
}

/* Helper: Connect to TCP server with timeout */
static tcp_socket_t* tcp_connect_timeout(const uint8_t* ip, uint16_t port, int timeout_ms) {
    tcp_socket_t* sock = tcp_socket_create();
    if (!sock) return NULL;
    
    /* Bind to a random local port */
    static uint16_t local_port = 50000;
    if (tcp_bind(sock, local_port++) != 0) {
        tcp_close(sock);
        return NULL;
    }
    
    /* Set remote endpoint */
    memcpy(sock->remote_ip, ip, 4);
    sock->remote_port = port;
    
    /* Send SYN */
    sock->state = TCP_STATE_SYN_SENT;
    sock->seq = 1000;  /* Initial sequence number */
    tcp_send_packet(sock, TCP_FLAG_SYN, NULL, 0);
    
    /* Wait for connection (state becomes ESTABLISHED) */
    int elapsed = 0;
    while (sock->state != TCP_STATE_ESTABLISHED && elapsed < timeout_ms) {
        thread_sleep_ms(10);
        elapsed += 10;
        
        /* Check for connection failure */
        if (sock->state == TCP_STATE_CLOSED) {
            tcp_close(sock);
            return NULL;
        }
    }
    
    if (sock->state != TCP_STATE_ESTABLISHED) {
        tcp_close(sock);
        return NULL;
    }
    
    return sock;
}

int http_get(const char* host, const char* path, uint16_t port, 
             uint8_t* buffer, uint32_t buf_size) {
    /* Resolve hostname */
    uint8_t server_ip[4];
    
    /* Check if host is already an IP */
    int dots = 0;
    for (const char* p = host; *p; p++) {
        if (*p == '.') dots++;
    }
    
    if (dots == 3) {
        /* Probably an IP address, try to parse it */
        if (parse_ip_string(host, server_ip) != 0) {
            return -1;
        }
    } else {
        /* Resolve hostname via DNS */
        KLOG_INFO("HTTP", "Resolving hostname...");
        
        dns_send_query(host);
        
        /* Wait for DNS response */
        int timeout = 500;  /* 5 seconds */
        while (dns_is_pending() && timeout-- > 0) {
            thread_sleep_ms(10);
        }
        
        if (!dns_get_result(server_ip)) {
            KLOG_ERROR("HTTP", "DNS resolution failed");
            return -1;
        }
        
        KLOG_INFO("HTTP", "DNS resolved");
    }
    
    /* Connect to server */
    KLOG_INFO("HTTP", "Connecting to server...");
    tcp_socket_t* sock = tcp_connect_timeout(server_ip, port, 5000);
    if (!sock) {
        KLOG_ERROR("HTTP", "Connection failed");
        return -1;
    }
    
    KLOG_INFO("HTTP", "Connected!");
    
    /* Build HTTP request */
    char request[512];
    int len = 0;
    len = str_append(request, sizeof(request), len, "GET ");
    len = str_append(request, sizeof(request), len, path);
    len = str_append(request, sizeof(request), len, " HTTP/1.0\r\nHost: ");
    len = str_append(request, sizeof(request), len, host);
    len = str_append(request, sizeof(request), len, "\r\nUser-Agent: ALOS/1.0\r\n");
    len = str_append(request, sizeof(request), len, "Connection: close\r\n\r\n");
    
    /* Send request */
    KLOG_INFO("HTTP", "Sending HTTP request...");
    if (tcp_send(sock, (uint8_t*)request, len) < 0) {
        KLOG_ERROR("HTTP", "Failed to send request");
        tcp_close(sock);
        return -1;
    }
    
    /* Receive response */
    KLOG_INFO("HTTP", "Waiting for response...");
    uint32_t total_received = 0;
    int timeout = 1000;  /* 10 seconds */
    
    while (total_received < buf_size && timeout-- > 0) {
        int available = tcp_available(sock);
        if (available > 0) {
            int to_read = available;
            if (to_read > (int)(buf_size - total_received)) {
                to_read = buf_size - total_received;
            }
            
            int received = tcp_recv(sock, buffer + total_received, to_read);
            if (received > 0) {
                total_received += received;
                timeout = 1000;  /* Reset timeout on data */
            }
        }
        
        /* Check if connection closed */
        if (sock->state == TCP_STATE_CLOSED) {
            break;
        }
        
        thread_sleep_ms(10);
    }
    
    KLOG_INFO_DEC("HTTP", "Received bytes: ", total_received);
    
    tcp_close(sock);
    return total_received;
}

int http_download_file(const char* url, const char* dest_path) {
    char host[64];
    uint16_t port;
    char path[256];
    
    /* Parse URL */
    if (parse_url(url, host, &port, path) != 0) {
        KLOG_ERROR("HTTP", "Invalid URL format");
        return -1;
    }
    
    KLOG_INFO("HTTP", "Downloading file...");
    KLOG_INFO_DEC("HTTP", "Port: ", port);
    
    /* Allocate buffer for HTTP response */
    uint8_t* buffer = (uint8_t*)kmalloc(65536);  /* 64KB buffer */
    if (!buffer) {
        KLOG_ERROR("HTTP", "Out of memory");
        return -1;
    }
    
    /* Download file */
    int received = http_get(host, path, port, buffer, 65536);
    if (received <= 0) {
        KLOG_ERROR("HTTP", "Download failed");
        kfree(buffer);
        return -1;
    }
    
    /* Parse HTTP response - skip headers */
    uint8_t* body = buffer;
    int body_len = received;
    
    /* Find end of headers (\r\n\r\n) */
    for (int i = 0; i < received - 3; i++) {
        if (buffer[i] == '\r' && buffer[i+1] == '\n' && 
            buffer[i+2] == '\r' && buffer[i+3] == '\n') {
            body = buffer + i + 4;
            body_len = received - (i + 4);
            break;
        }
    }
    
    /* Check HTTP status code */
    if (received > 12) {
        char status[4] = {0};
        status[0] = buffer[9];
        status[1] = buffer[10];
        status[2] = buffer[11];
        int status_code = atoi(status);
        
        KLOG_INFO_DEC("HTTP", "HTTP Status: ", status_code);
        
        if (status_code != HTTP_OK) {
            KLOG_ERROR("HTTP", "HTTP error");
            kfree(buffer);
            return -1;
        }
    }
    
    /* Save to file */
    KLOG_INFO("HTTP", "Saving to file...");
    
    vfs_node_t* file = vfs_open(dest_path, VFS_O_WRONLY | VFS_O_CREAT);
    if (!file) {
        KLOG_ERROR("HTTP", "Failed to create file");
        kfree(buffer);
        return -1;
    }
    
    int written = vfs_write(file, 0, body_len, body);
    vfs_close(file);
    
    if (written != body_len) {
        KLOG_ERROR("HTTP", "Failed to write all data");
        kfree(buffer);
        return -1;
    }
    
    KLOG_INFO_DEC("HTTP", "Downloaded bytes: ", body_len);
    
    kfree(buffer);
    return 0;
}
