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

#include <ctype.h>
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
#include "utilio.h"
#include "utillog.h"
#include "utilnet.h"
#include "version.h"
#include "workerctl.h"
#include "workeropts.h"

unsigned int redirect_stdout = 0;  /* Redirect stdout to a new file   */
unsigned int use_timestamps  = 0;  /* Input will be timestamped       */
unsigned int run_benchmarks  = 0;  /* Run the benchmarks              */
unsigned int load_benchmarks = 0;  /* Restore calibration from a file */
unsigned int save_benchmarks = 0;  /* Save new calibration to a file  */
unsigned int quit_benchmarks = 0;  /* Exit after running benchmarks   */
unsigned int debug_sync      = 0;  /* Debug synchronization order     */

/*
 * The input file and log file names are global since they're needed 
*    outside this file.
 */
char log_file[BUFSIZE];
char input_file[BUFSIZE];

static char benchmark_infile[BUFSIZE];
static char benchmark_outfile[BUFSIZE];

static int print_version = 0;

/*
 * Print out a pretty usage function.
 */
void usage(char *progname)
{
  char *p;

  if(!progname)
    progname = "gamut";

  p = strrchr(progname, '/');
  if(p)
    progname = p + 1;

  fprintf(stderr, "\n%s version %s, %s\n",
                  progname, GAMUT_VERSION, GAMUT_UPDATED);

  if(print_version) {
    fprintf(stderr, "\n");
    return;
  }

  fprintf(stderr, "\n"
                  "Usage: %s [-l logfile] [-r restore_bmark_file] [-s save_bmark_file]\n"
                  "            [-t tracefile] [-d debug_level] [-T <y|yes|n|no>]\n"
                  "            [-S] [-b] [-q] [-h] [-V]\n\n"
                  "-l logfile:             Log output to the given logfile (default: stdout).\n"
                  "-r restore_bmark_file:  Restore benchmark data from the given file.\n"
                  "-s save_bmark_file:     Save benchmark data to the given file.\n"
                  "-t tracefile:           Execute a series of timestamped commands from a file.\n"
                  "                        gamut will exit at the end of the file,\n"
                  "                        and not read any commands from stdin.\n"

                  "-d debug_level:         Set the logging detail to debug_level\n"
                  "                        (0 <= debug_level <= %d, default: %d)\n"
                  "-T <y|yes|n|no>:        Will input have timestamps?\n"
                  "                        Tracefiles have timestamps by default.\n"
                  "-b:                     Run the benchmark cycle 10 times.\n"
                  "-S:                     Debug synchronization operations (adds overhead).\n"
                  "-q:                     Quit after saving benchmark data to a file.\n"
                  "-h:                     Print this help screen and exit.\n"
                  "-V:                     Print version information and exit.\n"
                  "\n" , progname, G_MAX_DEBUG - 1, (int)get_log_level());

  return;
}


/*
 * Parse command-line options and fill in the gamut_opts struct.
 * This function returns -1 on error (invalid usage), or
 *                        the number of options parsed otherwise.
 */
int parse_opts(int argc, char *argv[], gamut_opts *opts)
{
  char *q;
  int opt;
  int count;
  s_log_level debug_level = G_NOTICE;

  if(!argc || !argv || !opts)
    return -1;

  count   = 0;
  opterr  = 0;  /* silent error reporting */
  memset(benchmark_infile,  0, BUFSIZE);
  memset(benchmark_outfile, 0, BUFSIZE);
  memset(log_file,          0, BUFSIZE);
  memset(input_file,        0, BUFSIZE);
  while((opt = getopt(argc, argv, "l:r:s:t:d:T:SVbqh")) != EOF) {
    if(((opt == 'l') || (opt == 'r') || (opt == 's')
        || (opt == 't') || (opt == 'd') || (opt == 'T')
       )
       && !optarg
      )
    {
      return -1;
    }

    switch(opt) {
      case 'b':  /* Run the benchmarks */
        run_benchmarks = 1;
        break;

      case 'l':  /* Where do we log output (stdout if none specified) */
        redirect_stdout = 1;
        strncpy(log_file, optarg, BUFSIZE);
        break;

      case 'q':  /* Quit after running benchmarks */
        quit_benchmarks = 1;
        break;

      case 'r':  /* Restore benchmark data from a file */
        load_benchmarks = 1;
        strncpy(benchmark_infile, optarg, BUFSIZE);
        break;

      case 's':  /* Save benchmark data to a file */
        save_benchmarks = 1;
        strncpy(benchmark_outfile, optarg, BUFSIZE);
        break;

      case 't':  /* Tracefile with commands */
        use_timestamps = 1;
        strncpy(input_file, optarg, BUFSIZE);
        break;

      case 'd':  /* Set debugging level */
        errno = 0;
        debug_level = (unsigned int)strtoul(optarg, &q, 10);
        if(errno || (optarg == q)) {
          s_log(G_ERR, "Invalid debug level: %s.\n", optarg);
          return -1;
        }
        break;

      case 'T': /* Input will contain timestamps */
        if(!strcasecmp(optarg, "y") || !strcasecmp(optarg, "yes")) {
          use_timestamps = 1;
        }
        else if(!strcasecmp(optarg, "n") || !strcasecmp(optarg, "no")) {
          use_timestamps = 0;
        }
        else {
          s_log(G_ERR, "Invalid timestamp preference: %s.\n", optarg);
          return -1;
        }
        break;

      case 'S': /* Enable synchronization debugging */
        debug_sync = 1;
        break;

      case 'h': /* Print the usage screen */
        return 0;

      case 'V': /* Print version info and exit */
        print_version = 1;
        return 0;

      default:
        return -1;
    }
    count++;
  }

  /*
   * We can't quit after running benchmarks if we're not running them.
   * We can't quit after running benchmarks if we don't save them.
   * We can't save and load benchmark data if we don't run benchmarks.
   * We can't run a tracefile if we quit after running benchmarks.
   */
  if(quit_benchmarks && (!run_benchmarks || !save_benchmarks))
    return -1;
  if(load_benchmarks && save_benchmarks && !run_benchmarks)
    return -1;
  if(quit_benchmarks && strlen(input_file))
    return -1;

  /* Set the debugging level */
  set_log_level(debug_level);

  return count;
}

/*
 * Redirect stdout to the log file specified on the command line.
 *   Upon completion, stdout will point to this new file.
 */
void redirect_output(void)
{
  FILE *fp;

  if(!log_file || !strlen(log_file))
    exit(EXIT_FAILURE);

  fp = freopen(log_file, "a", stdout);
  if(!fp) {
    s_log(G_EMERG, "Error redirecting stdout to \"%s\".\n", log_file);
    exit(EXIT_FAILURE);
  }
  else if(fp != stdout) {
    s_log(G_EMERG, "Error redirecting stdout: fp(%p) != stdout(%p).\n",
                   fp, stdout);
    exit(EXIT_FAILURE);
  }

  /* Use line buffering for stdout */
  setvbuf(stdout, (char *)NULL, _IOLBF, BUFSIZ);

  set_log_stream(stdout);
}


/*
 * Load benchmark data from a file.
 *   This function returns -1 on error, or
 *                          0 on success.
 */
int load_benchmark_data(void)
{
  char buf[BUFSIZE+1];
  int rc;
  FILE *fp;

  fp = fopen(benchmark_infile, "r");
  if(!fp) {
    s_log(G_WARNING, "Error opening benchmark data file %s: %s.\n",
                     benchmark_infile, strerror(errno));
    return 0;
  }

  second_count        = 0;
  prng_count          = 0;

  while((rc = get_line(buf, BUFSIZE, fp, (uint64_t)0)) > 0) {
    char *q;
    char *args[2];
    int nargs;

    chomp(buf);
    if((buf[0] == '\n') || (buf[0] == '#'))
      continue;

    nargs = split("=", buf, args, 2, ws_is_delim);
    if(nargs != 2) {
      s_log(G_WARNING, "Error parsing benchmark data file %s.\n",
                       benchmark_infile);
      rc = 0;
      goto close_out;
    }

    if(!strcasecmp("second_count", args[0])) {
      second_count = (unsigned long long)strtoull(args[1], &q, 10);
      if(errno || (args[1] == q)) {
        s_log(G_WARNING, "Invalid second_count value: %s\n", args[1]);
        rc = 0;
        goto close_out;
      }
    }
    else if(!strcasecmp("prng_count", args[0])) {
      prng_count = (unsigned long long)strtoull(args[1], &q, 10);
      if(errno || (args[1] == q)) {
        s_log(G_WARNING, "Invalid prng_count value: %s\n", args[1]);
        rc = 0;
        goto close_out;
      }
    }
    else {
      s_log(G_WARNING, "Unknown benchmark option in %s: %s\n",
                       benchmark_infile, args[0]);
      goto close_out;
    }
  }
  fclose(fp);
  fp = NULL;

  if(!second_count || !prng_count)
    rc = 0;
  else
    rc = 1;

close_out:
  if(fp)
    fclose(fp);

  return rc;
}

/*
 * Save benchmark data to a file.
 *   This function returns -1 on error, or
 *                          0 on success.
 */
int save_benchmark_data(void)
{
  FILE *fp;

  fp = fopen(benchmark_outfile, "w");
  if(!fp) {
    s_log(G_WARNING, "Error opening benchmark data file %s: %s.\n",
                     benchmark_outfile, strerror(errno));
    return 0;
  }

  fprintf(fp, "second_count = %llu\n", second_count);
  fprintf(fp, "prng_count = %llu\n", prng_count);

  fclose(fp);

  return 0;
}
