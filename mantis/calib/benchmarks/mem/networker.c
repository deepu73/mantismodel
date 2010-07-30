/*
 * Copyright 2003-2005 Justin Moore, justin@cs.duke.edu
 * Copyright 2004 HP Labs, justinm@hpl.hp.com
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "calibrate.h"
#include "constants.h"
#include "linklib.h"
#include "networker.h"
#include "utillog.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

/*
 * Do the work for this epoch.
 */
static int network(gamut_opts *gopts, nio_opts *nio, int sock,
                   char *buf, int64_t *target_netio,
                   double pkts_per_epoch, double *curr_pkts);

static int get_socket(nio_opts *nio);

static int do_dataio(int sock, char *buf, nio_opts *nio);
static int send_data(int sock, char *buf, nio_opts *nio);
static int recv_data(int sock, char *buf, nio_opts *nio);

static void print_iostats(int64_t total_usec, nio_opts *nio, char *tag);

/*
 * Establish a socket to do a certain number of I/Os per second.
 */
void* networker(void *opts)
{
  char *buf;
  int rc;
  int sock;
  int nio_index;
  int32_t target_epochs;
  int64_t link_waittime;
  int64_t target_netio;
  uint64_t next_deadline;
  double curr_pkts;
  double epochs_per_link;
  double curr_epochs;
  double pkts_per_epoch;
  nio_opts *nio;
  gamut_opts *gopts;
  struct timeval start;
  struct timeval finish;
  struct timeval finish_time;

  if(!opts)
    return NULL;

  gopts = (gamut_opts *)opts;

  nio_index = worker_register(gopts, CLS_NET);
  if(nio_index < 0) {
    return NULL;
  }
  else {
    nio = &gopts->net_io[nio_index];
  }

  /*
   * See if we need to wait for another linked worker
   *   to set us off.
   */
  rc = link_start_wait(gopts, CLS_NET, nio_index);
  if(rc < 0) {
    return NULL;
  }

  (void)gettimeofday(&nio->shopts.start_time, NULL);
  nio->total_netio             = 0;
  nio->netio_bytes[C_IOREAD]   = 0;
  nio->netio_bytes[C_IOWRITE]  = 0;
  nio->io_usec[C_IOREAD]       = 0;
  nio->io_usec[C_IOWRITE]      = 0;
  nio->shopts.missed_deadlines = 0;
  nio->shopts.missed_usecs     = 0;
  nio->shopts.total_deadlines  = 0;

  buf           = NULL;
  sock          = -1;
  link_waittime = 0;
restart:
  (void)gettimeofday(&nio->shopts.mod_time, NULL);
  test_and_close(sock);
  nio->shopts.dirty = 0;

  target_netio      = 0;
  epochs_per_link   = 0.0;
  curr_epochs       = 0.0;

  rc = validate_worker_opts(opts, CLS_NET, nio_index);
  if(rc <= 0)
  {
    s_log(G_WARNING, "%s has invalid network settings.\n",
                     nio->shopts.label);
    goto clean_out;
  }

  /*
   * Calculate the first deadline and the final deadline (if necessary).
   */
  {
    struct timeval tv;

    (void)gettimeofday(&tv, NULL);

    /*
     * Is there a time limit?
     */
    if(nio->shopts.exec_time) {
      finish_time.tv_sec  = tv.tv_sec + nio->shopts.exec_time;
      finish_time.tv_usec = tv.tv_usec;
    }
    else {
      finish_time.tv_sec  = 0;
      finish_time.tv_usec = 0;
    }

    next_deadline  = tv.tv_usec;
    next_deadline += tv.tv_sec * US_SEC;
  }

  /*
   * We call 'realloc' here instead of malloc, since this
   *   handles the situation where we're coming through the
   *   first time (buf will be NULL, and realloc behaves
   *   like malloc) and when we're here because of a 'dirty'
   *   tag (buf will not be NULL, but realloc works anyway).
   */
  buf = (char *)realloc(buf, nio->pktsize);
  if(!buf) {
    s_log(G_WARNING, "%s could not allocate buffer: %s\n",
                     nio->shopts.label, strerror(errno));
    goto clean_out;
  }

  /*
   * With the packetsize and the target I/O rate we can figure out
   *   how many packets per second we need to send (or receive).
   */
  pkts_per_epoch  = (float)nio->iorate / nio->pktsize;
  pkts_per_epoch /= WORKER_EPOCHS_PER_SEC;

  s_log(G_DEBUG, "%s net I/O rate of %.4f packets/epoch.\n",
                 nio->shopts.label, pkts_per_epoch);

  /*
   * Calculate the total number of network I/O operations
   *   that this worker will perform.
   */
  if(nio->shopts.max_work)
  {
    target_netio = nio->shopts.max_work / nio->pktsize;
    if(!target_netio)
      target_netio = 1;
  }
  else
  {
    target_netio = -1;
  }

  /*
   * Make sure that if we've been asked to be part of a link,
   *   that we've got enough information to be sure that the
   *   link will actually work.
   */
  if(nio->shopts.link_work && nio->shopts.next_worker)
  {
    shared_opts *link_shopts;

    epochs_per_link = (double)nio->shopts.link_work
                      / (pkts_per_epoch * nio->pktsize);
    curr_epochs     = epochs_per_link;
    target_epochs   = (int32_t)curr_epochs;

    link_shopts      = (shared_opts *)nio->shopts.next_worker;

    s_log(G_DEBUG, "Will do %.2f epochs per link, handing off to %s.\n",
                   epochs_per_link, link_shopts->label);
  }
  else
  {
    target_epochs = -1;
  }

  /*
   * Now that we've done everything necessary on our side,
   *   get the remote end.
   */
  sock = get_socket(nio);
  if(sock < 0) {
    goto clean_out;
  }

  /*
   * We've got the socket; start doing I/O at the correct intervals.
   *   Just send data for now.  Perhaps later we'll set up a
   *   listening daemon.
   *
   * Each time through, perform these operations in this order:
   * 1. Calculate the next deadline
   * 2. Perform the I/O
   * 3. If we're linked with another worker, see if it's handoff time
   * 4. See if it's time to exit
   * 4. Sleep, if there's enough time
   */
  curr_pkts = 0.0;
  (void)gettimeofday(&start, NULL);
  while(!nio->shopts.exiting) {
    struct timeval now;

    /* Steps 1 & 2 for an unlinked worker */
    if(target_epochs < 0) {
      /* Step 1 */
      next_deadline += US_PER_WORKER_EPOCH;

      /* Step 2 */
      rc = network(gopts, nio, sock, buf, &target_netio,
                   pkts_per_epoch, &curr_pkts);
      if(rc < 0) {
        s_log(G_WARNING, "Error doing network.  Exiting.\n");
        nio->shopts.exiting = 1;
        break;
      }
      else if(!rc) {
        s_log(G_DEBUG, "Network says we need to bail.\n");
        break;
      }
    }
    else { /* (target_epochs >= 0), a linked worker */
      if(target_epochs > 0) {
        /* Step 1 */
        next_deadline += US_PER_WORKER_EPOCH;

        /* Step 2 */
        rc = network(gopts, nio, sock, buf, &target_netio,
                     pkts_per_epoch, &curr_pkts);
        if(rc < 0) {
          s_log(G_WARNING, "Error doing network.  Exiting.\n");
          nio->shopts.exiting = 1;
          break;
        }
        else if(!rc) {
          s_log(G_DEBUG, "Network says we need to bail.\n");
          break;
        }

        target_epochs--;
      }

      /*
       * If we do have to wait, make sure we don't over-work
       *   when it's our turn.  Move the next_deadline backward
       *   by as much time as we spend waiting.
       */
      if(!target_epochs) {
        int64_t timediff;
        struct timeval b_link;

        (void)gettimeofday(&b_link, NULL);
        rc = link_next_wait(gopts, CLS_NET, nio_index, epochs_per_link,
                            &curr_epochs, &target_epochs);
        if(rc < 0) {
          s_log(G_WARNING, "Error in link_next_wait.\n");
        }
        else if(!rc) {
          s_log(G_DEBUG, "We need to exit (link_wait says so).\n");
          break;
        }
        else {
          struct timeval f_link;

          (void)gettimeofday(&f_link, NULL);
          s_log(G_DEBUG, "EL %.2f  CE %.2f  TE %d\n",
                         epochs_per_link, curr_epochs, target_epochs);

          timediff = calculate_timediff(&b_link, &f_link);
          next_deadline += timediff;
          link_waittime += timediff;
          s_log(G_DEBUG, "Moved next deadline backward by %lld usec.\n",
                         timediff);
        }
      }
    }

    /* Step 4 */
    (void)gettimeofday(&now, NULL);

    if(!finish_time.tv_sec
       || (finish_time.tv_sec > now.tv_sec)
       || ((finish_time.tv_sec == now.tv_sec)
           && (finish_time.tv_usec > now.tv_usec)
          )
      )
    {
      int64_t time_diff;
      int64_t current_time;

      current_time  = now.tv_usec;
      current_time += now.tv_sec * US_SEC;

      time_diff = next_deadline - current_time;

      /* Is there enough time to sleep? */
      if(current_time < next_deadline)
      {
        if(time_diff > MIN_SLEEP_US) {
          struct timeval sleeptv;

          sleeptv.tv_sec  = time_diff / US_SEC;
          sleeptv.tv_usec = time_diff - (sleeptv.tv_sec * US_SEC);
          (void)select(0, (fd_set *)NULL, (fd_set *)NULL,
                       (fd_set *)NULL, &sleeptv);
        }
      }
      else
      {
        nio->shopts.missed_deadlines++;
        nio->shopts.missed_usecs += (current_time - next_deadline);
      }
      nio->shopts.total_deadlines++;
    }
    else
    {
      nio->shopts.exiting = 1;
      break;
    }

    /*
     * Shut everything down and start over again.
     */
    if(nio->shopts.dirty) {
      s_log(G_INFO, "%s reloading values.\n", nio->shopts.label);
      goto restart;
    }
  }

  (void)gettimeofday(&finish, NULL);

  rc = lock_stats(gopts);
  if(rc < 0) {
    goto clean_out;
  }
  gopts->wstats.workers_exiting++;
  (void)unlock_stats(gopts);

clean_out:
  if(sock >= 0)
    close(sock);
  if(buf)
    free(buf);

  if(nio->netio_bytes[C_IOREAD] || nio->netio_bytes[C_IOWRITE])
  {
    int64_t total_usec;
    uint64_t avg_miss_time;

    total_usec = calculate_timediff(&start, &finish);
    if(nio->shopts.missed_deadlines) {
      avg_miss_time = nio->shopts.missed_usecs
                      / nio->shopts.missed_deadlines;
    }
    else {
      avg_miss_time = 0;
    }

    print_iostats(total_usec, nio, "total");

    if(link_waittime) {
      print_iostats(total_usec - link_waittime, nio, "work");
    }

    s_log(G_INFO, "%s missed %llu of %llu deadlines by %llu usecs (avg).\n",
                  nio->shopts.label, nio->shopts.missed_deadlines,
                  nio->shopts.total_deadlines, avg_miss_time);
  }

  /*
   * Remove ourselves from any links.
   */
  rc = link_remove(gopts, CLS_NET, nio_index);
  if(rc < 0) {
    s_log(G_WARNING, "Error removing %s from any link.\n",
                     nio->shopts.label);
  }

  (void)worker_unregister(gopts, CLS_NET);

  return NULL;
}

/*
 * Do the work for this epoch.
 */
static int network(gamut_opts *gopts, nio_opts *nio, int sock,
                   char *buf, int64_t *target_netio,
                   double pkts_per_epoch, double *curr_pkts)
{
  int      rc;
  int64_t  l_target_netio;
  uint64_t target_pkts;
  double   l_curr_pkts;

  if(!gopts || !nio || (sock < 0) || !buf || !target_netio
     || (pkts_per_epoch < 0) || !curr_pkts
    )
  {
    return -1;
  }

  l_curr_pkts    = *curr_pkts;
  l_target_netio = *target_netio;

  l_curr_pkts += pkts_per_epoch;
  target_pkts  = (uint64_t)l_curr_pkts;

  while(target_pkts) {
    rc = do_dataio(sock, buf, nio);
    if(rc < 0) {  /* Error */
      s_log(G_WARNING, "%s: Error in network I/O.\n",
                       nio->shopts.label);
      break;
    }
    else if(!rc) { /* Remote end closed (TCP only) */
      s_log(G_WARNING, "%s: TCP connection closed.\n",
                       nio->shopts.label);
      break;
    }
    else {        /* Some I/O actually performed */
      target_pkts--;
      nio->total_netio += nio->pktsize;

      if(l_target_netio > 0) {
        target_netio--;
        if(!target_netio) {
          nio->shopts.exiting = 1;
          break;
        }
      }
    }  /* END, Full packet */
  }

  l_curr_pkts -= (uint64_t)l_curr_pkts;

  /*
   * Copy all local variables back.
   */
  *curr_pkts    = l_curr_pkts;
  *target_netio = l_target_netio;

  if(nio->shopts.exiting) {
    return 0;
  }
  else if(target_pkts) {
    /* We bailed out early.  There's a problem. */
    return -1;
  }
  else {
    return 1;
  }
}



static int get_socket(nio_opts *nio)
{
  int sock;
  int socktype;

  if(!nio) {
    return -1;
  }

  sock = -1;

  switch(nio->protocol) {
    case IPPROTO_TCP:
      socktype = SOCK_STREAM;
      break;

    case IPPROTO_UDP:
      socktype = SOCK_DGRAM;
      break;

    default:
      goto fail_out;
  }

  if((nio->mode != O_RDONLY) && (nio->mode != O_WRONLY)) {
    goto fail_out;
  }

  /*
   * Use the correct kind of protocol.
   */
  sock = socket(AF_INET, socktype, nio->protocol);
  if(sock < 0) {
    s_log(G_WARNING, "%s error getting socket.\n", nio->shopts.label);
    goto fail_out;
  }

  /*
   * If the socket is write-only, then try to connect to the other end
   *   if it's a TCP connection.
   * Otherwise, the socket is read-only, and we need to wait for data.
   *   If it's a TCP socket, listen and wait for data a connection.
   */
  if(nio->mode == O_WRONLY) {
    int rc;
    int flags;

    /*
     * If this is a TCP socket, try to connect to the remote end.
     */
    if(socktype == SOCK_STREAM) {
      int ecode;  /* potential error code for getsockopt */
      fd_set conn;
      socklen_t rlen;
      socklen_t optlen;
      struct timeval tv;
      struct sockaddr_in remote;

      /*
       * We will set the socket to be non-blocking, then try to connect
       *   to the remote end with a CONN_WAIT-second delay.
       */
      flags = fcntl(sock, F_GETFL);
      if(flags < 0) {
        goto fail_out;
      }

      flags |= O_NONBLOCK;
      rc     = fcntl(sock, F_SETFL, &flags);
      if(rc < 0) {
        goto fail_out;
      }

      memset(&remote, 0, sizeof(remote));

      remote.sin_addr.s_addr = nio->addr;
      remote.sin_port        = htons(nio->port);
      remote.sin_family      = AF_INET;
      rlen                   = sizeof(remote);

      rc = connect(sock, (struct sockaddr *)&remote, rlen);
      if(rc < 0) {
        if(errno != EINPROGRESS) {
          goto fail_out;
        }
      }
      else /* Success */
        goto clean_out;

      /*
       * At this point we're waiting for the connection.  We need to
       *   test the socket for writability, then check the error code.
       */
      FD_ZERO(&conn);
      FD_SET(sock, &conn);

      tv.tv_sec  = CONN_WAIT;
      tv.tv_usec = 0;
      rc = select(sock+1, (fd_set *)NULL, &conn, (fd_set *)NULL, &tv);
      if(rc <= 0) {
        goto fail_out;
      }

      /* Writability on one socket.  But is it success? */
      optlen = sizeof(ecode);
      rc = getsockopt(sock, SOL_SOCKET, SO_ERROR, &ecode, &optlen);
      if((rc < 0) || (ecode != 0)) {
        goto fail_out;
      }
    }

    /*
     * Regardless of what we did, clear any O_NONBLOCK flags.
     */
    flags = fcntl(sock, F_GETFL);
    if(flags < 0) {
      goto fail_out;
    }

    flags &= ~O_NONBLOCK;
    rc     = fcntl(sock, F_SETFL, flags);
    if(rc < 0) {
      goto fail_out;
    }
  }  /* End O_WRONLY */
  else { /* O_RDONLY */
    int rc;
    int flags;
    struct sockaddr_in my_addr;

    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family      = AF_INET;
    my_addr.sin_port        = htons(nio->port);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    rc = bind(sock, (struct sockaddr *)&my_addr, sizeof(my_addr));
    if(rc < 0) {
      goto fail_out;
    }

    if(socktype == SOCK_STREAM) {
      int newsock;
      time_t timeout;
      time_t currtime;

      newsock = -1;

      rc = listen(sock, LISTEN_BACKLOG);
      if(rc < 0) {
        goto fail_out;
      }

      /*
       * Set an absolute deadline for getting the correct connection.
       *   This is to make sure we don't get a "bad" connection and
       *   have to redo how long we're waiting.
       */
      currtime = time(NULL);
      timeout  = currtime + CONN_WAIT + 1;
      while(timeout > (currtime = time(NULL))) {
        socklen_t slen;
        struct sockaddr_in saddr;

        slen = sizeof(saddr);
        memset(&saddr, 0, sizeof(saddr));

        newsock = accept(sock, (struct sockaddr *)&saddr, &slen);
        if(newsock < 0) {
          goto fail_out;
        }

        /*
         * Did we hear from the right people?
         */
        if((nio->addr != INADDR_ANY)
           && (saddr.sin_addr.s_addr != nio->addr)
          )
        {
            /* Wrong person.  Try again */
            continue;
        }

        /*
         * Yep.  Now close the old socket and hand back the new socket.
         */
        (void)close(sock);
        sock = newsock;
      }
    }

    flags = fcntl(sock, F_GETFL);
    if(flags < 0) {
      goto fail_out;
    }

    flags |= O_NONBLOCK;
    s_log(G_DEBUG, "Setting mode on %d to %x.\n", sock, flags);
    rc = fcntl(sock, F_SETFL, flags);
    if(rc < 0) {
      goto fail_out;
    }
  } /* End O_RDONLY */

  {
    int flags;

    flags = fcntl(sock, F_GETFL);
    s_log(G_DEBUG, "fcntl(%d, F_GETFL) = %x (%x).\n",
                   sock, flags, (flags & O_NONBLOCK));
  }

clean_out:

  return sock;

fail_out:
  if(sock >= 0)
    (void)close(sock);

  return -1;
}

static int do_dataio(int sock, char *buf, nio_opts *nio)
{
  int rc;
  int frc;

  if((sock < 0) || !buf || !nio)
    return -1;

  frc = -1;
  if(nio->mode == O_WRONLY) {
    rc = send_data(sock, buf, nio);
    s_log(G_DLOOP, "send_data(%d, %p, %p) = %d\n",
                   sock, buf, (void *)nio, rc);
    if(rc < 0) {
      s_log(G_WARNING, "%s error sending data.\n", nio->shopts.label);
      nio->shopts.exiting = 1;
    }
    else if((nio->protocol == IPPROTO_TCP) && !rc) {
      s_log(G_WARNING, "%s remote end closed.\n", nio->shopts.label);
      nio->shopts.exiting = 1;
    }
    else { /* Successful send */
      frc = 1;
    }
  }
  else {  /* mode == O_RDONLY */
    rc = recv_data(sock, buf, nio);
    s_log(G_DLOOP, "recv_data(%d, %p, %p) = %d\n",
                   sock, buf, (void *)nio, rc);
    if(rc < 0) {
      if(rc != -EAGAIN) {
        s_log(G_WARNING, "%s error getting data.\n", nio->shopts.label);
        nio->shopts.exiting = 1;
      }
    }
    else if((nio->protocol == IPPROTO_TCP) && !rc) {
      s_log(G_WARNING, "%s remote end closed.\n", nio->shopts.label);
      nio->shopts.exiting = 1;
    }
    else {
      /*
       * We'll allow UDP with no data to be a success (somehow?)
       *   because we've done the bookkeeping correctly in the
       *   lower-level functions.
       */
      frc = 1;
    }
  }

  return frc;
}

/*
 * Wrapper for sending data.
 * This function returns <  0 on error,
 *                       >= 0 on success (the number of bytes sent).
 */
static int send_data(int sock, char *buf, nio_opts *nio)
{
  int rc;
  int err;
  int frc;
  struct sockaddr_in cdata;  /* connection data */
  socklen_t slen;
  struct timeval bt;
  struct timeval ft;

  if((sock < 0) || !buf || !nio) {
    return -1;
  }

  /*
   * If the socket is a TCP stream, just use send().
   * If the socket is a UDP stream, use sendto.
   */
  rc  = -1;
  err = 0;
  frc = -1;
  memset(&cdata, 0, sizeof(cdata));
  switch(nio->protocol) {
    case IPPROTO_TCP:
      (void)gettimeofday(&bt, NULL);
      errno = 0;
      rc    = send(sock, buf, (size_t)nio->pktsize, 0);
      err   = errno;
      (void)gettimeofday(&ft, NULL);
      s_log(G_DLOOP, "send(%d, %p, %u, %u) = %d (%d).\n",
                     sock, buf, nio->pktsize, 0, rc, err);
      break;

    case IPPROTO_UDP:
      cdata.sin_addr.s_addr = nio->addr;
      cdata.sin_port        = htons(nio->port);
      cdata.sin_family      = AF_INET;
      slen                  = sizeof(cdata);

      (void)gettimeofday(&bt, NULL);
      errno = 0;
      rc    = sendto(sock, buf, (size_t)nio->pktsize, 0,
                     (struct sockaddr *)&cdata, slen);
      err   = errno;
      s_log(G_DLOOP, "sendto(%d, %p, %d, %d, %p, %p) = %d (%d).\n",
                     sock, buf, nio->pktsize, 0, (void *)&cdata,
                     (void *)&slen, rc, err);
      break;

    default:
      rc  = -1;
      err = 1;
      break;
  }

  if(rc < 0) {
    frc = -err;
  }
  else if(rc != nio->pktsize) {
    frc = 0;
  }
  else { /* rc == nio->pktsize */
    int64_t timediff;

    timediff = calculate_timediff(&bt, &ft);
    nio->netio_bytes[C_IOWRITE] += 1;
    nio->io_usec[C_IOWRITE]     += timediff;

    frc = 1;
  }

  return frc;
}

/*
 * Wrapper for receiving data.
 * This function returns <  0 on error,
 *                       == 0 if the remote end closed (TCP only),
 *                       >  0 on success (the number of bytes read).
 */
static int recv_data(int sock, char *buf, nio_opts *nio)
{
  int rc;
  int err;
  int frc;
  int32_t num_tries;
  struct sockaddr_in cdata;  /* connection data */
  socklen_t slen;
  struct timeval bt;
  struct timeval ft;

  if((sock < 0) || !buf || !nio) {
    return -1;
  }

  /*
   * If the socket is a TCP stream, just use recv().
   * If the socket is a UDP stream, use recvfrom.
   */
  err       = 0;
  frc       = -1;
  num_tries = 0;
  memset(&cdata, 0, sizeof(cdata));
  switch(nio->protocol) {
    case IPPROTO_TCP:
      (void)gettimeofday(&bt, NULL);
      errno = 0;
      rc    = recv(sock, buf, nio->pktsize, 0);
      err   = errno;
      (void)gettimeofday(&ft, NULL);
      s_log(G_DLOOP, "recv(%d, %p, %u, %u) = %d (%d).\n",
                     sock, buf, nio->pktsize, 0, rc, err);
      break;

    case IPPROTO_UDP:
      {
        int flags;

        flags = fcntl(sock, F_GETFL);
        s_log(G_DLOOP, "fcntl(%d, F_GETFL) = %x (%x).\n",
                       sock, flags, (flags & O_NONBLOCK));
      }

      /*
       * For UDP, we read data in a loop because we need to make sure
       *   we're getting our packet from the correct remote end.
       *   It's possible for an invalid host to send something, so
       *   make sure we ignore that data.
       * Cap the number of tries, so we don't get stuck in this loop.
       */
      do {
        memset(&cdata, 0, sizeof(cdata));
        slen = sizeof(cdata);
        (void)gettimeofday(&bt, NULL);
        errno = 0;
        rc    = recvfrom(sock, buf, nio->pktsize, 0,
                         (struct sockaddr *)&cdata, &slen);
        err   = errno;
        (void)gettimeofday(&ft, NULL);
        s_log(G_DLOOP, "recvfrom(%d, %p, %d, %d, %p, %p) = %d (%d).\n",
                       sock, buf, nio->pktsize, 0, (void *)&cdata,
                       (void *)&slen, rc, err);
        if(rc > 0)
        { /* We got some data */
          if((nio->addr == INADDR_ANY)
             || (nio->addr == cdata.sin_addr.s_addr))
          {    /* We got data from the right people */
            break;
          }
          else {
            num_tries++;
          }
        }
        else
        { /* Error of some sort (rc <= 0) */
          break;
        }
      } while(num_tries < MAX_RECV_TRIES);
      break;

    default:
      rc  = -1;
      err = 1;
      break;
  }

  if(rc < 0) {
    frc = -err;
  }
  else if(rc != nio->pktsize) { /* Incomplete data */
    frc = 0;
  }
  else { /* rc == nio->pktsize */
    int64_t timediff;

    timediff = calculate_timediff(&bt, &ft);
    nio->netio_bytes[C_IOREAD] += 1;
    nio->io_usec[C_IOREAD]     += timediff;

    frc = 1;
  }

  return frc;
}

static void print_iostats(int64_t total_usec, nio_opts *nio, char *tag)
{
  char iorate[SMBUFSIZE];
  int64_t total_io;
  double iotime;

  if(!nio || !tag)
    return;

  if(!total_usec)
    return;

  if(!strlen(tag)) {
    tag = "total";
  }

  /*
   * Print a number of statistics about the I/O rates
   * 1. Overall I/O and I/O rates
   * 2. Read I/O and I/O rates
   * 3. Write I/O and I/O rates
   */
  total_io   = nio->netio_bytes[C_IOREAD] + nio->netio_bytes[C_IOWRITE];
  total_io  *= nio->pktsize;
  iotime     = (double)total_usec / US_SEC;

  /* Number 1 */
  print_scaled_number(iorate, SMBUFSIZE,
                      (uint64_t)(total_io / iotime), 1);
  s_log(G_NOTICE, "%s did %llu net I/O in %.4f sec "
                  "at %sps (%s).\n", nio->shopts.label,
                  total_io, iotime, iorate, tag);

  /* Number 2 */
  if(nio->netio_bytes[C_IOREAD] && nio->io_usec[C_IOREAD])
  {
    int64_t read_io;
    double readtime;

    read_io   = nio->netio_bytes[C_IOREAD] * nio->pktsize;
    readtime  = (double)nio->io_usec[C_IOREAD] / US_SEC;
    print_scaled_number(iorate, SMBUFSIZE,
                        (uint64_t)(read_io / readtime), 1);
    s_log(G_NOTICE, "%s did %llu bytes read in %.4f sec "
                    "at %sps (%s).\n", nio->shopts.label, read_io,
                    readtime, iorate, tag);
  }

  /* Number 3 */
  if(nio->netio_bytes[C_IOWRITE] && nio->io_usec[C_IOWRITE])
  {
    int64_t write_io;
    double writetime;

    write_io  = nio->netio_bytes[C_IOWRITE] * nio->pktsize;
    writetime = (double)nio->io_usec[C_IOWRITE] / US_SEC;
    print_scaled_number(iorate, SMBUFSIZE,
                        (uint64_t)(write_io / writetime), 1);
    s_log(G_NOTICE, "%s did %llu bytes send in %.4f sec "
                    "at %sps (%s).\n", nio->shopts.label, write_io,
                    writetime, iorate, tag);
  }
}
