/* Configuration du resolveur : res_ninit sur le DNS DHCP du noyau. */
#include <errno.h>
#include <netinet/in.h>
#include <resolv.h>
#include <string.h>
#include <sys/syscall.h>
#include "../../../../include/net_abi.h"

/* Etat par thread : _Thread_local est supporte (TLS ELF statique). */
static _Thread_local struct __res_state thread_state;

struct __res_state *__res_state(void) { return &thread_state; }

int res_ninit(res_state state) {
  memset(state, 0, sizeof(*state));
  state->retrans = RES_TIMEOUT;
  state->retry = RES_DFLRETRY;
  state->options = RES_INIT | RES_DEFAULT;
  state->ndots = 1;
  /* Premier serveur DNS non nul parmi les interfaces (jusqu'a MAXNS). */
  alos_netif_info_t info;
  for (unsigned i = 0; i < 16 && state->nscount < MAXNS; i++) {
    long result = syscall2(SYS_NET_IFACE_INFO, (long)i, (long)&info);
    if (result < 0) break;
    if (info.dns == 0) continue;
    struct sockaddr_in *ns = &state->nsaddr_list[state->nscount++];
    ns->sin_family = AF_INET;
    ns->sin_port = (in_port_t)((53 >> 8) | ((53 & 0xff) << 8)); /* htons(53) */
    ns->sin_addr.s_addr = info.dns;
  }
  return 0;
}

int res_init(void) { return res_ninit(&thread_state); }

void res_nclose(res_state state) { (void)state; /* pas de socket gardee ouverte */ }

void res_ndestroy(res_state state) { res_nclose(state); }
