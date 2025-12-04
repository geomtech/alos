/* src/net/l4/httpd.c - Simple HTTP Server Implementation */
#include "httpd.h"
#include "tcp.h"
#include "../core/net.h"
#include "../../include/string.h"
#include "../../fs/vfs.h"
#include "../../kernel/klog.h"
#include "../../kernel/thread.h"
#include "../../mm/kheap.h"

/* Helper: Find substring in string */
static char* httpd_strstr(const char* haystack, const char* needle)
{
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

/* Helper: Simple integer to string */
static void httpd_itoa(int value, char* buf)
{
    char tmp[16];
    int i = 0;
    int neg = 0;
    
    if (value < 0) {
        neg = 1;
        value = -value;
    }
    
    do {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value && i < 15);
    
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* Helper: Build HTTP 200 header */
static int build_http_header(char* buf, int buf_size, const char* content_type, int content_length)
{
    int pos = 0;
    const char* p;
    
    /* HTTP/1.0 200 OK\r\n */
    p = "HTTP/1.0 200 OK\r\n";
    while (*p && pos < buf_size - 1) buf[pos++] = *p++;
    
    /* Server: ALOS/1.0\r\n */
    p = "Server: ALOS/1.0\r\n";
    while (*p && pos < buf_size - 1) buf[pos++] = *p++;
    
    /* Content-Type: xxx\r\n */
    p = "Content-Type: ";
    while (*p && pos < buf_size - 1) buf[pos++] = *p++;
    while (*content_type && pos < buf_size - 1) buf[pos++] = *content_type++;
    p = "\r\n";
    while (*p && pos < buf_size - 1) buf[pos++] = *p++;
    
    /* Content-Length: xxx\r\n */
    p = "Content-Length: ";
    while (*p && pos < buf_size - 1) buf[pos++] = *p++;
    char len_str[16];
    httpd_itoa(content_length, len_str);
    p = len_str;
    while (*p && pos < buf_size - 1) buf[pos++] = *p++;
    p = "\r\n";
    while (*p && pos < buf_size - 1) buf[pos++] = *p++;
    
    /* Connection: close\r\n\r\n */
    p = "Connection: close\r\n\r\n";
    while (*p && pos < buf_size - 1) buf[pos++] = *p++;
    
    buf[pos] = '\0';
    return pos;
}

/* Server state */
static thread_t *g_httpd_thread = NULL;
static volatile bool g_httpd_running = false;
static volatile bool g_httpd_stop_requested = false;
static uint16_t g_httpd_port = 0;

/* HTTP response templates (static, no format strings) */

static const char HTTP_404_RESPONSE[] = 
    "HTTP/1.0 404 Not Found\r\n"
    "Server: ALOS/1.0\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 89\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><head><title>404 Not Found</title></head>"
    "<body><h1>404 Not Found</h1></body></html>";

static const char HTTP_500_RESPONSE[] = 
    "HTTP/1.0 500 Internal Server Error\r\n"
    "Server: ALOS/1.0\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 107\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><head><title>500 Error</title></head>"
    "<body><h1>500 Internal Server Error</h1></body></html>";

/* Default index page */
static const char DEFAULT_INDEX[] = 
    "<html>\r\n"
    "<head><title>Welcome to ALOS</title></head>\r\n"
    "<body>\r\n"
    "<h1>Welcome to ALOS HTTP Server</h1>\r\n"
    "<p>The server is running successfully.</p>\r\n"
    "<p>Place files in /www to serve them.</p>\r\n"
    "</body>\r\n"
    "</html>\r\n";

/* Helper: Get content type from file extension */
static const char* get_content_type(const char* path)
{
    const char* ext = path;
    const char* last_dot = NULL;
    
    /* Find last dot in path */
    while (*ext) {
        if (*ext == '.') last_dot = ext;
        ext++;
    }
    
    if (!last_dot) return "application/octet-stream";
    
    if (strcmp(last_dot, ".html") == 0 || strcmp(last_dot, ".htm") == 0)
        return "text/html";
    if (strcmp(last_dot, ".css") == 0)
        return "text/css";
    if (strcmp(last_dot, ".js") == 0)
        return "application/javascript";
    if (strcmp(last_dot, ".json") == 0)
        return "application/json";
    if (strcmp(last_dot, ".txt") == 0)
        return "text/plain";
    if (strcmp(last_dot, ".png") == 0)
        return "image/png";
    if (strcmp(last_dot, ".jpg") == 0 || strcmp(last_dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(last_dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(last_dot, ".ico") == 0)
        return "image/x-icon";
    
    return "application/octet-stream";
}

/* Helper: Parse HTTP request and extract path */
static int parse_request(const char* request, char* method, char* path, int path_size)
{
    /* Simple parser: "GET /path HTTP/1.x" */
    int i = 0;
    
    /* Extract method */
    while (request[i] && request[i] != ' ' && i < 7) {
        method[i] = request[i];
        i++;
    }
    method[i] = '\0';
    
    if (request[i] != ' ') return -1;
    i++;
    
    /* Extract path */
    int j = 0;
    while (request[i] && request[i] != ' ' && request[i] != '?' && j < path_size - 1) {
        path[j++] = request[i++];
    }
    path[j] = '\0';
    
    return 0;
}

/* Helper: Send response to client */
static void send_response(tcp_socket_t* client, const char* data, int len)
{
    int sent = 0;
    while (sent < len && client->state == TCP_STATE_ESTABLISHED) {
        int chunk = len - sent;
        if (chunk > 1024) chunk = 1024;  /* Send in chunks */
        
        int result = tcp_send(client, (const uint8_t*)(data + sent), chunk);
        if (result <= 0) break;
        sent += result;
        
        /* Yield to allow network processing */
        thread_yield();
    }
}

/* Handle a single client connection */
static void handle_client(tcp_socket_t* client)
{
    char* request = (char*)kmalloc(HTTPD_MAX_REQUEST_SIZE);
    char* response = (char*)kmalloc(HTTPD_MAX_RESPONSE_SIZE);
    
    if (!request || !response) {
        if (request) kfree(request);
        if (response) kfree(response);
        send_response(client, HTTP_500_RESPONSE, strlen(HTTP_500_RESPONSE));
        return;
    }
    
    /* Read request with timeout */
    int total_read = 0;
    int timeout = 50;  /* 500ms timeout */
    
    while (total_read < HTTPD_MAX_REQUEST_SIZE - 1 && timeout > 0) {
        int available = tcp_available(client);
        if (available > 0) {
            int to_read = available;
            if (to_read > HTTPD_MAX_REQUEST_SIZE - 1 - total_read)
                to_read = HTTPD_MAX_REQUEST_SIZE - 1 - total_read;
            
            int read = tcp_recv(client, (uint8_t*)(request + total_read), to_read);
            if (read > 0) {
                total_read += read;
                /* Check if we have complete headers */
                request[total_read] = '\0';
                if (httpd_strstr(request, "\r\n\r\n")) break;
            }
        }
        
        if (client->state != TCP_STATE_ESTABLISHED) break;
        
        thread_yield();
        timeout--;
    }
    
    if (total_read == 0) {
        kfree(request);
        kfree(response);
        return;
    }
    
    request[total_read] = '\0';
    
    /* Parse request */
    char method[8];
    char path[256];
    
    if (parse_request(request, method, path, sizeof(path)) != 0) {
        send_response(client, HTTP_500_RESPONSE, strlen(HTTP_500_RESPONSE));
        kfree(request);
        kfree(response);
        return;
    }
    
    KLOG_INFO("HTTPD", "Request received:");
    KLOG_INFO("HTTPD", method);
    KLOG_INFO("HTTPD", path);
    
    /* Only support GET */
    if (strcmp(method, "GET") != 0) {
        send_response(client, HTTP_500_RESPONSE, strlen(HTTP_500_RESPONSE));
        kfree(request);
        kfree(response);
        return;
    }
    
    /* Handle root path */
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }
    
    /* Build full path */
    char full_path[512];
    strcpy(full_path, HTTPD_DOCUMENT_ROOT);
    strcat(full_path, path);
    
    /* Try to open file */
    vfs_node_t* file = vfs_open(full_path, VFS_O_RDONLY);
    
    if (!file) {
        /* Check if it's the root and serve default page */
        if (strcmp(path, "/index.html") == 0) {
            /* Serve default index */
            int header_len = build_http_header(response, HTTPD_MAX_RESPONSE_SIZE,
                "text/html", (int)strlen(DEFAULT_INDEX));
            send_response(client, response, header_len);
            send_response(client, DEFAULT_INDEX, strlen(DEFAULT_INDEX));
        } else {
            send_response(client, HTTP_404_RESPONSE, strlen(HTTP_404_RESPONSE));
        }
        kfree(request);
        kfree(response);
        return;
    }
    
    /* Get file size from vfs_node */
    uint32_t file_size = file->size;
    
    /* Send header */
    const char* content_type = get_content_type(path);
    int header_len = build_http_header(response, HTTPD_MAX_RESPONSE_SIZE,
        content_type, (int)file_size);
    send_response(client, response, header_len);
    
    /* Send file content in chunks */
    uint32_t offset = 0;
    while (offset < file_size && client->state == TCP_STATE_ESTABLISHED) {
        uint32_t chunk_size = file_size - offset;
        if (chunk_size > 1024) chunk_size = 1024;
        
        int read = vfs_read(file, offset, chunk_size, (uint8_t*)response);
        if (read <= 0) break;
        
        send_response(client, response, read);
        offset += read;
    }
    
    vfs_close(file);
    kfree(request);
    kfree(response);
}

/* Main server thread function */
static void httpd_thread_main(void* arg)
{
    uint16_t port = (uint16_t)(uintptr_t)arg;
    
    KLOG_INFO("HTTPD", "HTTP server thread starting...");
    KLOG_INFO_DEC("HTTPD", "Listening on port ", port);
    
    /* Create listen socket */
    net_lock();
    tcp_socket_t* listen_sock = tcp_socket_create();
    net_unlock();
    
    if (!listen_sock) {
        KLOG_ERROR("HTTPD", "Failed to create socket");
        g_httpd_running = false;
        return;
    }
    
    /* Bind to port */
    net_lock();
    if (tcp_bind(listen_sock, port) != 0) {
        KLOG_ERROR("HTTPD", "Failed to bind socket");
        tcp_close(listen_sock);
        net_unlock();
        g_httpd_running = false;
        return;
    }
    net_unlock();
    
    /* Put socket in listen state */
    listen_sock->state = TCP_STATE_LISTEN;
    
    g_httpd_running = true;
    g_httpd_port = port;
    
    KLOG_INFO("HTTPD", "HTTP server started successfully");
    
    /* Main accept loop */
    while (!g_httpd_stop_requested) {
        /* Check for incoming connections - no lock needed for read-only operation */
        tcp_socket_t* client = tcp_find_ready_client(port);
        
        if (client != NULL && client->state == TCP_STATE_ESTABLISHED) {
            KLOG_INFO("HTTPD", "New client connection found!");
            
            /* Handle client request */
            handle_client(client);
            
            /* Close client connection */
            net_lock();
            tcp_close(client);
            net_unlock();
            
            KLOG_INFO("HTTPD", "Client connection closed");
        }
        
        /* Yield to other threads */
        thread_yield();
    }
    
    /* Cleanup */
    net_lock();
    tcp_close(listen_sock);
    net_unlock();
    
    g_httpd_running = false;
    g_httpd_port = 0;
    
    KLOG_INFO("HTTPD", "HTTP server stopped");
}

int httpd_start(uint16_t port)
{
    if (g_httpd_running) {
        KLOG_WARN("HTTPD", "Server already running");
        return -1;
    }
    
    if (port == 0) port = HTTPD_DEFAULT_PORT;
    
    g_httpd_stop_requested = false;
    
    /* Create server thread */
    g_httpd_thread = thread_create("httpd", httpd_thread_main, 
                                   (void*)(uintptr_t)port,
                                   THREAD_DEFAULT_STACK_SIZE,
                                   THREAD_PRIORITY_NORMAL);
    
    if (!g_httpd_thread) {
        KLOG_ERROR("HTTPD", "Failed to create server thread");
        return -1;
    }
    
    KLOG_INFO("HTTPD", "HTTP server thread created");
    
    /* Wait a bit for the thread to start */
    thread_sleep_ms(100);
    
    return 0;
}

void httpd_stop(void)
{
    if (!g_httpd_running && !g_httpd_thread) {
        return;
    }
    
    KLOG_INFO("HTTPD", "Stopping HTTP server...");
    
    g_httpd_stop_requested = true;
    
    /* Wait for thread to finish (with timeout) */
    if (g_httpd_thread) {
        int timeout = 50;  /* 5 seconds */
        while (g_httpd_running && timeout > 0) {
            thread_sleep_ms(100);
            timeout--;
        }
        
        if (g_httpd_running) {
            KLOG_WARN("HTTPD", "Server thread did not stop gracefully");
            thread_kill(g_httpd_thread, 0);
        }
        
        g_httpd_thread = NULL;
    }
    
    g_httpd_running = false;
    g_httpd_port = 0;
}

bool httpd_is_running(void)
{
    return g_httpd_running;
}

uint16_t httpd_get_port(void)
{
    return g_httpd_port;
}
