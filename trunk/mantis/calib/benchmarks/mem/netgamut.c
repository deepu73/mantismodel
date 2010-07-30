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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "calibrate.h"
#include "constants.h"
#include "input.h"
#include "mainctl.h"
#include "opts.h"
#include "reaper.h"
#include "utilarr.h"
#include "utillog.h"
#include "utilnet.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"

#define NETGAMUT_PORT 5623
#define NETGAMUT_FILE "/tmp/netgamut.err"

static void get_servsock(growArray *s_arr);

int main(int argc, char *argv[])
{
  int rc;
  gamut_opts opts;
  growArray *sockets;

  memset(&opts, 0, sizeof(opts));

  sockets = NULL;
  rc = init_socket_arr(&sockets);
  if(rc < 0) {
    s_log(G_EMERG, "Error initializing socket array.\n");
    exit(EXIT_FAILURE);
  }

  rc = parse_opts(argc, argv, &opts);
  if(rc < 0) {
    s_log(G_EMERG, "Error parsing options.\n");
    exit(EXIT_FAILURE);
  }
  else if(!rc) {
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  rc = daemon(/* nochdir to root => */ 0,
              /* noclose std{out,err} => */ 1);
  if(rc < 0) {
    s_log(G_EMERG, "Could not daemonize.\n");
    exit(EXIT_FAILURE);
  }

  /*
   * In this order, perform these steps (if necessary)
   * 1. Redirect to a log file
   * 2. Restore benchmark data from a file
   * 3. Run the benchmarks
   * 4. Save the new benchmark data
   * 5. Quit after benchmarks
   * 6. Run calibration (if no benchmark data)
   * 7. Run a trace file and exit
   * 8. Accept commands from the network
   */

  /*
   * 1. Redirect to a log file.  If one was specified on the command line,
   *      use that one; otherwise, use the default one for netgamut.
   */
  if(redirect_stdout) {
    redirect_output();
  }
  else {
    strncpy(log_file, NETGAMUT_FILE, BUFSIZE);
    redirect_output();
  }

  /*
   * 2. Restore benchmark data from a file
   */
  if(load_benchmarks) {
    s_log(G_NOTICE, "Loading benchmark data ... ");
    load_benchmark_data();
    s_log(G_NOTICE, "done.\n");
  }

  /*
   * 3. Run the benchmarks
   */
  if(run_benchmarks) {
    s_log(G_NOTICE, "Running %u calibration trials.\n", DEF_BMARK_TRIALS);
    benchmark_delays(DEF_BMARK_TRIALS);
  }

  /*
   * 4. Save the new benchmark data
   */
  if(save_benchmarks) {
    s_log(G_NOTICE, "Saving benchmark data ... ");
    save_benchmark_data();
    s_log(G_NOTICE, "done.\n");

    /*
     * 5. Quit after benchmarks
     */
    if(quit_benchmarks)
      exit(EXIT_SUCCESS);
  }

  /*
   * 6. Run calibration (if no benchmark data is available yet)
   */
  if(!load_benchmarks && !run_benchmarks) {
    s_log(G_NOTICE, "Calibrating node attributes ... ");
    benchmark_delays(1);
    s_log(G_NOTICE, "done.\n");
  }

  get_servsock(sockets);

  init_opts(&opts);
  start_reaper(&opts);
  start_input(&opts);

  stop_input(&opts);
  killall_workers(&opts);
  stop_reaper(&opts);

  return 0;
}

void get_servsock(growArray *s_arr)
{
  uint16_t port;
  int rc;
  int servsock;
 
  if(!s_arr || !s_arr->dat || (s_arr->currUsed != 0)) {
    s_log(G_EMERG, "Socket growArray not initialized correctly.\n");
    exit(EXIT_FAILURE);
  }

  port = NETGAMUT_PORT; 
  servsock = get_server_sock(port);
  if(servsock < 0) {
    s_log(G_EMERG, "Unable to get server socket on port %hd.\n", port);
    exit(EXIT_FAILURE);
  }
 
  rc = add_socket(s_arr, servsock, AF_INET);
  if(rc < 0) {
    s_log(G_EMERG, "Unable to add socket %d to sockets array.\n", servsock);
    exit(EXIT_FAILURE);
  }
 
  /* This socket is ready to accept connections */
  ((sockinfo *)(s_arr->dat))[0].state = XFER_MODE;
}
