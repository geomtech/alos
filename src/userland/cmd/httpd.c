/* src/userland/cmd/httpd.c - HTTP server control */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: httpd <start|stop|status> [port]\n");
    printf("Commands:\n");
    printf("  httpd start [port]  - Start HTTP server (default port 80)\n");
    printf("  httpd stop          - Stop HTTP server\n");
    printf("  httpd status        - Show server status\n");
    printf("Note: Files are served from /www directory.\n");
    return 1;
  }

  const char *cmd = argv[1];

  if (strcmp(cmd, "start") == 0) {
    int port = 80;
    if (argc >= 3) {
      port = atoi(argv[2]);
      if (port <= 0) port = 80;
    }
    long ret = syscall2(SYS_HTTPD, 1, port);
    if (ret == 1) {
      printf("HTTP server is already running.\n");
    } else if (ret == 0) {
      printf("HTTP server started on port %d (serving /www)\n", port);
    } else {
      printf("Failed to start HTTP server on port %d\n", port);
      return 1;
    }
  } else if (strcmp(cmd, "stop") == 0) {
    syscall2(SYS_HTTPD, 2, 0);
    printf("HTTP server stopped.\n");
  } else if (strcmp(cmd, "status") == 0) {
    long port = syscall2(SYS_HTTPD, 3, 0);
    if (port > 0) {
      printf("HTTP server is RUNNING on port %ld (root /www)\n", port);
    } else {
      printf("HTTP server is STOPPED\n");
    }
  } else {
    printf("Unknown command: %s\n", cmd);
    return 1;
  }

  return 0;
}
