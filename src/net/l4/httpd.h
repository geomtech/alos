/* src/net/l4/httpd.h - Simple HTTP Server (runs in background thread) */
#ifndef NET_HTTPD_H
#define NET_HTTPD_H

#include <stdint.h>
#include <stdbool.h>

/* Default HTTP server port */
#define HTTPD_DEFAULT_PORT 80

/* Maximum request size */
#define HTTPD_MAX_REQUEST_SIZE 4096

/* Maximum response size */
#define HTTPD_MAX_RESPONSE_SIZE 8192

/* Document root for serving files */
#define HTTPD_DOCUMENT_ROOT "/www"

/**
 * Start the HTTP server in a background thread.
 * The server will listen on the specified port and serve files from HTTPD_DOCUMENT_ROOT.
 * 
 * @param port Port to listen on (0 for default 80)
 * @return 0 on success, -1 on error
 */
int httpd_start(uint16_t port);

/**
 * Stop the HTTP server.
 * Signals the server thread to stop and waits for it to terminate.
 */
void httpd_stop(void);

/**
 * Check if the HTTP server is running.
 * 
 * @return true if running, false otherwise
 */
bool httpd_is_running(void);

/**
 * Get the port the HTTP server is listening on.
 * 
 * @return Port number, or 0 if not running
 */
uint16_t httpd_get_port(void);

#endif /* NET_HTTPD_H */
