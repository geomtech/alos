/* <resolv.h> : configuration du resolveur, sous-ensemble compatible glibc.
 * Il n'y a pas de /etc/resolv.conf sous ALOS : res_ninit() lit le serveur DNS
 * configure par DHCP dans le noyau (SYS_NET_IFACE_INFO). Seuls les champs
 * lus par du code client (nscount, nsaddr_list, options, ndots, retrans, retry,
 * dnsrch, defdname) sont renseignes ; il n'y a pas de res_query/res_send. */
#ifndef _RESOLV_H
#define _RESOLV_H

#include <netinet/in.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAXNS 3
#define MAXDNSRCH 6
#define MAXRESOLVSORT 10
#define RES_TIMEOUT 5
#define RES_DFLRETRY 2

#define RES_INIT      0x00000001
#define RES_DEBUG     0x00000002
#define RES_USEVC     0x00000008
#define RES_IGNTC     0x00000020
#define RES_RECURSE   0x00000040
#define RES_DEFNAMES  0x00000080
#define RES_STAYOPEN  0x00000100
#define RES_DNSRCH    0x00000200
#define RES_USE_INET6 0x00002000
#define RES_ROTATE    0x00004000
#define RES_USE_EDNS0 0x40000000
#define RES_SNGLKUP   0x00200000
#define RES_SNGLKUPREOP 0x00400000
#define RES_DEFAULT (RES_RECURSE | RES_DEFNAMES | RES_DNSRCH)

struct __res_state {
  int retrans;
  int retry;
  unsigned long options;
  int nscount;
  struct sockaddr_in nsaddr_list[MAXNS];
#define nsaddr nsaddr_list[0]
  unsigned short id;
  char *dnsrch[MAXDNSRCH + 1];
  char defdname[256];
  unsigned long pfcode;
  unsigned ndots : 4;
  unsigned nsort : 4;
  unsigned ipv6_unavail : 1;
  unsigned unused : 23;
  struct {
    struct in_addr addr;
    uint32_t mask;
  } sort_list[MAXRESOLVSORT];
  void *__glibc_unused_qhook;
  void *__glibc_unused_rhook;
  int res_h_errno;
  int _vcsock;
  unsigned int _flags;
  union {
    char pad[52];
    struct {
      uint16_t nscount;
      uint16_t nsmap[MAXNS];
      int nssocks[MAXNS];
      uint16_t nscount6;
      uint16_t nsinit;
      struct sockaddr_in6 *nsaddrs[MAXNS];
    } _ext;
  } _u;
};

typedef struct __res_state *res_state;

/* Etat du thread (equivalent de _res de glibc). */
struct __res_state *__res_state(void);
#define _res (*__res_state())

int res_ninit(res_state state);
int res_init(void);
void res_nclose(res_state state);
void res_ndestroy(res_state state);

#ifdef __cplusplus
}
#endif

#endif
