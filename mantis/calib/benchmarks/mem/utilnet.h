/* 
 * Copyright 2001-2004 Justin Moore, justin@cs.duke.edu
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _UTIL_NET_H
#define _UTIL_NET_H

#include <netdb.h>
#include <net/if.h>  /* for IFNAMSIZ */

#include "utilarr.h"

extern int host_lookup(const char *hn, uint32_t *addr);
extern int get_ip_from_socket(int fd, uint32_t *addr);
extern void hex2hwaddr(uint8_t *hexmac, uint32_t macLen, uint8_t *dst,
                       uint32_t dstLen);

/*
 * Basic network library to allow connection multiplexing.
 */
typedef enum {
  NEW_SOCK,   /* Just created/accepted */
  XFER_MODE   /* Transferring/accepting data */
} conn_state;

typedef struct {
  int sock;
  struct sockaddr_storage addr;
  conn_state state;
  int error;
} sockinfo;

/*
 * Information about network interfaces.
 */
typedef struct {
  char name[IF_NAMESIZE+1];
  uint32_t addr;
  uint32_t netmask;
} interface;

extern int init_socket_arr(growArray **s_arr);
extern void del_socket_arr(growArray **s_arr);

extern int add_socket(growArray *s_arr, int sock, int protocol);
extern int find_socket(growArray *s_arr, int sock);
extern int activate_socket(growArray *s_arr, int sock);
extern int del_socket(growArray *s_arr, int sock);

extern int accept_connection(growArray *s_arr, uint32_t usec_timeout);

extern int get_client_sock(const char *node, uint16_t port);
extern int get_server_sock(uint16_t port);

/*
 * Build (cache) the list of network interfaces.
 */
extern int32_t build_nic_table(interface **nics);

/*
 * Get a network address by
 * a) the interface name, or
 * b) the address of a client that communicated with it.
 */
extern uint32_t get_iface_by_ifname(interface *nics, int32_t num_nics,
                                    const char *iface);
extern uint32_t get_iface_by_addr(interface *nics, int32_t num_nics,
                                  struct sockaddr_in *client, char *iface);

#endif /* _UTIL_NET_H */
