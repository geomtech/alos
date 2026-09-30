#ifndef _SYS_UN_H
#define _SYS_UN_H
#include <sys/socket.h>
/* Format declare pour les clients ; AF_UNIX reste refuse par le noyau. */
struct sockaddr_un { sa_family_t sun_family; char sun_path[108]; };
#endif
