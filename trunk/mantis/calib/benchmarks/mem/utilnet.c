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

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
   
#include "utilarr.h"
#include "utilio.h"
#include "utilnet.h"

int host_lookup(const char *hn, uint32_t *addr)
{
  struct hostent *he;

  if(!hn || !addr || !(*hn))
    return -1;

  he = gethostbyname(hn);
  if(!he) {
    return -1;
  }
  else {
    memcpy((char *)addr, (char *)he->h_addr, he->h_length);
    return 0;
  }
}

int get_ip_from_socket(int fd, uint32_t *addr)
{
  int rc;
  socklen_t namelen;
  struct sockaddr_in saddr;

  if((fd < 0) || !addr)
    return -1;

  namelen = sizeof(saddr);

  rc = getpeername(fd, (struct sockaddr *)&saddr, &namelen);
  if(rc < 0) {
    return -1;
  }
  else {
    memcpy(addr, &saddr.sin_addr.s_addr, sizeof(uint32_t));
    return 0;
  }
}

void hex2hwaddr(uint8_t *hexmac, uint32_t macLen, uint8_t *dst, uint32_t dstLen)
{
  int i;
  int n;

  if(!hexmac || !macLen || !dst || !dstLen)
    return;

  n = snprintf((char *)dst, dstLen, "%02x", hexmac[0]);
  for(i = 1;(i < macLen) && (n < dstLen);i++) {
    n += snprintf((char *)(dst + n), (dstLen - n), ":%02x", hexmac[i]);
  }
  if(n < dstLen)
    dst[n] = '\0';
}

int init_socket_arr(growArray **s_arr)
{
  int rc;
  int do_sort;

  if(!s_arr || (*s_arr))
    return -1;

  do_sort = 1;

  rc = InitGrowArray(s_arr, 1, sizeof(sockinfo), !do_sort);
  if(rc < 0)
    return -1;

  return 0;
}

void del_socket_arr(growArray **s_arr)
{
  if(!s_arr || !(*s_arr))
    return;

  DelGrowArray(s_arr);
}

int add_socket(growArray *s_arr, int sock, int domain)
{
  int i;
  int rc;
  sockinfo *arr;

  if(!s_arr || !s_arr->dat || (sock < 0) || (domain < 0))
    return -1;

  rc = TestAndGrowArray(s_arr, 1);
  if(rc < 0)
    return -1;

  arr = (sockinfo *)s_arr->dat;
  for(i = 0;i < s_arr->currUsed;i++) {
    if(sock == arr[i].sock)
      return 0;
  }

  arr[i].sock  = sock;
  arr[i].state = NEW_SOCK;
  arr[i].error = 0;
  memset(&arr[i].addr, 0, sizeof(struct sockaddr_storage));
  arr[i].addr.ss_family = domain;
  s_arr->currUsed++;

  return 0;
}

int find_socket(growArray *s_arr, int sock)
{
  int i;
  sockinfo *arr;

  if(!s_arr || !s_arr->dat || !s_arr->currUsed)
    return -1;

  arr = (sockinfo *)s_arr->dat;
  for(i = 0;i < s_arr->currUsed;i++) {
    if(arr[i].sock == sock)
      return i;
  }

  return -1;
}

int activate_socket(growArray *s_arr, int sock)
{
  int i;
  sockinfo *arr;
 
  if(!s_arr || !s_arr->dat || !s_arr->currUsed)
    return -1;
 
  arr = (sockinfo *)s_arr->dat;
  for(i = 0;i < s_arr->currUsed;i++) {
    if(arr[i].sock == sock) {
      arr[i].state = XFER_MODE;
      return 1;
    }
  }

  return 0;
}

int del_socket(growArray *s_arr, int sock)
{
  int i;
  int to_move;
  sockinfo *arr;

  if(!s_arr || !s_arr->dat || (sock < 0))
    return -1;
  
  arr = (sockinfo *)s_arr->dat;
  for(i = 0;i < s_arr->currUsed;i++) {
    if(sock == arr[i].sock)
      break;
  }

  if(i == s_arr->currUsed)
    return -1;

  to_move = s_arr->currUsed - i - 1;
  if(to_move)
    memmove(&arr[i], &arr[i+1], (to_move * sizeof(sockinfo)));
  s_arr->currUsed--;

  return 0;
}

int accept_connection(growArray *s_arr, uint32_t usec_timeout)
{
  int i;
  int rc;
  int max_fd;
  int num_new;
  fd_set fds;
  sockinfo *arr;
  struct timeval tv;
  struct timeval *tvp;

  if(!s_arr || !s_arr->dat || !s_arr->currUsed)
    return -1;

  if(!usec_timeout)
    tvp = NULL;
  else {
    tv.tv_sec  = usec_timeout / 1000000;
    tv.tv_usec = usec_timeout - (tv.tv_sec * 1000000);
    tvp = &tv;
  }

  FD_ZERO(&fds);
  max_fd = -1;
  arr = (sockinfo *)s_arr->dat;
  for(i = 0;i < s_arr->currUsed;i++) {
    if(arr[i].state != XFER_MODE)
      continue;

    FD_SET(arr[i].sock, &fds);
    if(arr[i].sock > max_fd)
      max_fd = arr[i].sock;
  }

  /* Nothing to accept */
  if(max_fd == -1)
    return -1;

  rc = select(max_fd + 1, &fds, (fd_set *)NULL, (fd_set *)NULL, tvp);
  if(rc < 0) {
    if(errno == EINTR) {  /* Some signal came along an interrupted us */
      return 0;
    }
    else
      return -1;
  }
  else if(!rc) {
    return 0;
  }
  else
    num_new = rc;

  /*
   * There are actually connections to accept here.
   */
  for(i = 0;i < s_arr->currUsed;i++) {
    arr = (sockinfo *)s_arr->dat;

    if(arr[i].state != XFER_MODE)
      continue;

    if(FD_ISSET(arr[i].sock, &fds)) {
      socklen_t len;

      len = sizeof(struct sockaddr_storage);

      do {
        rc = accept(arr[i].sock, (struct sockaddr *)&arr[i].addr, &len);
      } while((rc < 0) && (errno == EINTR));
      if(rc < 0)
        arr[i].error = errno;
      else {
        int newsock;

        arr[i].error = 0;
        newsock = rc;
        (void)add_socket(s_arr, newsock, arr[i].addr.ss_family);
      }
    }
  }

  return num_new;
}

int get_client_sock(const char *node, uint16_t port)
{
  int rc;
  int sock;
  struct hostent *he;
  struct sockaddr_in saddr;

  if(!node || !(*node) || !port)
    return -1;

  memset(&saddr, 0, sizeof(saddr));

  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);

  he = gethostbyname(node);
  if(!he)
    return -1;

  memcpy(&saddr.sin_addr, he->h_addr, sizeof(saddr.sin_addr));

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(sock < 0)
    return -1;

  rc = connect(sock, (struct sockaddr *)&saddr, (socklen_t)sizeof(saddr));
  if(rc < 0)
    return -1;

  return sock;
}

int get_server_sock(uint16_t port)
{
  int rc;
  int optval;
  int sock;
  struct sockaddr_in saddr;

  if(!port)
    return -1;

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(sock < 0)
    return -1;

  optval = 1;
  rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                  (void *)&optval, sizeof(optval));
  if(rc < 0)
    return -1;

  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);

  rc = bind(sock, (struct sockaddr *)&saddr, sizeof(saddr));
  if(rc < 0) {
    close(sock);
    return -1;
  }

  rc = listen(sock, 32);
  if(rc < 0) {
    close(sock);
    return -1;
  }
  else
    return sock;
}

/*
 * Builds the list of network interfaces on a given node.
 * Returns -1 on error,
 *          0 on success.
 */
int32_t build_nic_table(interface **nics)
{
  char buf[BUFSIZE];
  int i;
  int rc;
  int sock;
  int32_t num_nics;
  interface *tmpNics;
  interface newNics[16];  /* This should be a reasonable max */
  struct ifreq *ifarr;
  struct ifreq *ifend;
  struct ifconf ifconf;

  if(!nics)
    return -1;

  tmpNics = NULL;
  memset(buf, 0, BUFSIZE);
  memset(newNics, 0, sizeof(newNics));

  ifconf.ifc_len = BUFSIZE;
  ifconf.ifc_buf = (caddr_t)buf;

  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(sock < 0)
    return -1;

  rc = ioctl(sock, SIOCGIFCONF, &ifconf);
  if(rc < 0)
    goto clean_out;

  /*
   * loop over the list of interfaces until we hit the end
   */
  i = 0;
  ifarr = ifconf.ifc_req;
  ifend = (struct ifreq *)(ifconf.ifc_buf + ifconf.ifc_len);
  for(ifarr = ifconf.ifc_req;ifarr < ifend;ifarr++) {
    struct in_addr ifaddr;
    struct sockaddr_in *iftmp;

    if(ifarr->ifr_addr.sa_family != AF_INET)
      continue;

    strncpy(newNics[i].name, ifarr->ifr_name, IFNAMSIZ);
    newNics[i].name[IFNAMSIZ] = '\0';

    iftmp = (struct sockaddr_in *)&ifarr->ifr_addr;
    memcpy(&ifaddr, &iftmp->sin_addr, sizeof(struct in_addr));
    memcpy(&newNics[i].addr, &ifaddr.s_addr, sizeof(uint32_t));

    /*
     * Now that we've got the interface's address, get the netmask.
     */
    rc = ioctl(sock, SIOCGIFNETMASK, ifarr);
    if(rc < 0) {
      continue;
    }
    newNics[i].netmask = iftmp->sin_addr.s_addr;

    i++;
  }
  if((ifarr > ifend) || !i)
    goto clean_out;
  else
    num_nics = i;

  close(sock);

  /*
   * Now that we've got the NIC table, copy it over
   *   to the main data structure.
   */
  tmpNics = calloc(i, sizeof(interface));
  if(!tmpNics) {
    return -1;
  }

  memcpy(tmpNics, newNics, (i * sizeof(interface)));

  /*
   * Clean out any previous interfaces and re-build the table.
   * This could be necessary if a new interface is brought up
   *   after the program starts.
   */
  if(*nics) {
    free(*nics);
  }

  *nics = tmpNics;

  return num_nics;

clean_out:
  if(tmpNics)
    free(tmpNics);
  close(sock);

  return -1;
}

/*
 * Get an interface address based on the contents of the 'ifname' string.
 */
uint32_t get_iface_by_ifname(interface *nics, int32_t num_nics,
                             const char *iface)
{
  int i;

  if(!nics || !iface || !(*iface))
    return 0;

  for(i = 0;i < num_nics;i++) {
    if(!strncmp(iface, nics[i].name, IFNAMSIZ))
      break;
  }
  if(i == num_nics)
    return 0;
  else
    return nics[i].addr;
}

/*
 * Get the address of the interface associated with that client.
 * If the client doesn't have an addr, see if we have a default
 *   interface.  If not, return an error.
 */
uint32_t get_iface_by_addr(interface *nics, int32_t num_nics,
                           struct sockaddr_in *client, char *ifname)
{
  int i;
  uint32_t cliaddr;

  if(!nics || !client)
    return 0;

  if(!client->sin_addr.s_addr) {
    if(!ifname || !ifname[0]) { /* No client address and no fallback */
      return 0;
    }
    else {
      uint32_t ret_addr;

      ret_addr = get_iface_by_ifname(nics, num_nics, ifname);
      return ret_addr;
    }
  }

  cliaddr = client->sin_addr.s_addr;

  for(i = 0;i < num_nics;i++) {
    uint32_t clinet;
    uint32_t ifnet;

    ifnet = nics[i].addr & nics[i].netmask;
    clinet = cliaddr & nics[i].netmask;
    if(ifnet == clinet)
      break;
  }
  if(i == num_nics)
    return 0;
  else
    return nics[i].addr;
}
