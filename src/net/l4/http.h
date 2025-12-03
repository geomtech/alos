/* src/net/l4/http.h - Simple HTTP Client */
#ifndef NET_HTTP_H
#define NET_HTTP_H

#include <stdint.h>

/* HTTP default port */
#define HTTP_PORT 80

/* Maximum URL length */
#define HTTP_MAX_URL_LENGTH 512

/* HTTP Response codes */
#define HTTP_OK                 200
#define HTTP_MOVED_PERMANENTLY  301
#define HTTP_FOUND              302
#define HTTP_NOT_FOUND          404
#define HTTP_SERVER_ERROR       500

/**
 * Download a file via HTTP and save it to the filesystem
 * 
 * @param url       Full URL (e.g., "http://example.com/file.txt")
 * @param dest_path Destination file path (e.g., "/tmp/file.txt")
 * @return 0 on success, -1 on error
 */
int http_download_file(const char* url, const char* dest_path);

/**
 * Simple HTTP GET request
 * 
 * @param host      Hostname or IP address
 * @param path      Path on the server (e.g., "/index.html")
 * @param port      Port number (typically 80)
 * @param buffer    Buffer to store the response body
 * @param buf_size  Size of the buffer
 * @return Number of bytes received, or -1 on error
 */
int http_get(const char* host, const char* path, uint16_t port, 
             uint8_t* buffer, uint32_t buf_size);

#endif /* NET_HTTP_H */
