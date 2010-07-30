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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "calibrate.h"
#include "constants.h"
#include "input.h"
#include "mainctl.h"
#include "opts.h"
#include "reaper.h"
#include "utillog.h"
#include "utilnet.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"

int main(int argc, char *argv[])
{
  int rc;
  gamut_opts opts;

  signal(SIGPIPE, SIG_IGN);

  memset(&opts, 0, sizeof(opts));

  /* Use line buffering for stdout */
  setvbuf(stdout, (char *)NULL, _IOLBF, BUFSIZ);

  rc = parse_opts(argc, argv, &opts);
  if(rc < 0) {
    s_log(G_EMERG, "Error parsing options.\n");
    exit(EXIT_FAILURE);
  }
  else if(!rc) {
    usage(argv[0]);
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
   * 8. Accept commands from stdin
   */

  /*
   * 1. Redirect to a log file
   */
  if(redirect_stdout) {
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

  init_opts(&opts);
  start_reaper(&opts);
  start_input(&opts);

  execute_gamut(&opts);

  stop_input(&opts);
  killall_workers(&opts);
  stop_reaper(&opts);

  return 0;
}
