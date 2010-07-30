/*
 * Copyright 2005 Justin Moore, justin@cs.duke.edu
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>

#include "constants.h"
#include "input.h"
#include "mainctl.h"
#include "opts.h"
#include "utillog.h"
#include "workerinfo.h"
#include "workeropts.h"
#include "workersync.h"
#include "workerwait.h"

/*
 * The thread we will spawn.
 */
static void* input(void *opts);

/*
 * Will each command come with a timestamp, or will we execute
 *   them 'live', as they arrive?
 */
static void parse_input_timed(gamut_opts *gopts, FILE *infp);
static void parse_input_live(gamut_opts *gopts, FILE *infp);

/*
 * Main functions that actually do stuff
  */
#define create_handler(h) \
static int (h)(gamut_opts *gopts, char *cmdstr)

create_handler(do_helo);
create_handler(do_info);
create_handler(do_load);
create_handler(do_opts);
create_handler(do_wait);

/*
 * We keep track of all the commands that will execute
 *   on the front-end as well as the back-end.  We keep tabs
 *   on which to execute locally (within the input thread)
 *   and which to execute remotely (on the master).
 */
static cmd_handler c_handlers[] = {
  { "wctl", NULL    },
  { "helo", do_helo },
  { "info", do_info },
  { "link", NULL    },
  { "load", do_load },
  { "opts", do_opts },
  { "wait", do_wait }
};
static int num_handlers = sizeof(c_handlers) / sizeof(c_handlers[0]);

/*
 * Given a command string, which function should we execute?
 */
static cmd_handler* get_handler_by_msg(const char *cmd)
{
  int i;

  if(!cmd || !(*cmd))
    return (cmd_handler *)NULL;

  for(i = 0;i < num_handlers;i++) {
    if(!strcasecmp(c_handlers[i].cmd, cmd)) {
      return &c_handlers[i];
    }
  }

  return (cmd_handler *)NULL;
}

/*
 * Fire up the input thread.
 */
void start_input(gamut_opts *gopts)
{
  int rc;

  if(!gopts) {
    exit(EXIT_FAILURE);
  }

  rc = lock_start(gopts);
  if(rc < 0) {
    exit(EXIT_FAILURE);
  }

  rc = pthread_create(&gopts->i_sync.t_sync.tid, (pthread_attr_t *)NULL,
                      input, (void *)gopts);

  (void)unlock_start(gopts);

  if(rc < 0) {
    s_log(G_WARNING, "Error starting input thread.\n");
    exit(EXIT_FAILURE);
  }
  else {
    s_log(G_DEBUG, "Started input thread (tid %lu).\n",
                   gopts->i_sync.t_sync.tid);
    return;
  }
}

/*
 * High-level API to input parsing code.
 */
void parse_input(gamut_opts *gopts, char *infname, uint8_t timed)
{
  FILE *infp;

  if(!gopts || !infname)
    return;

  infp = NULL;
  if(!strlen(infname) || !strcmp(infname, "-")) {
    infp = stdin;
  }
  else {
    infp = fopen(infname, "r");
    if(!infp) {
      s_log(G_WARNING, "Could not open input file %s: %s\n",
                       infname, strerror(errno));
      goto fail_out;
    }
  }

  if(!infp) {
    s_log(G_WARNING, "Error finding input.\n");
    goto fail_out;
  }

  if(timed) {
    parse_input_timed(gopts, infp);
  }
  else {
    parse_input_live(gopts, infp);
  }

fail_out:
  if(infp)
    fclose(infp);

  return;
}

/*
 * Shut down the input thread.
 */
void stop_input(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return;

  /*
   * To kill off the input, tell it it's time to exit,
   *   and then signal it.  Lock the input lock so it
   *   doesn't miss our signal.
   */
  rc = lock_input(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  gopts->i_sync.exiting = 1;

  rc = unlock_input(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = pthread_join(gopts->i_sync.t_sync.tid, (void **)NULL);
  if(rc < 0) {
    s_log(G_WARNING, "Error collecting input thread.\n");
  }
  else {
    s_log(G_DEBUG, "Collected input.\n");
  }

fail_out:
  return;
}

/*
 * The thread we will spawn.
 */
static void* input(void *opts)
{
  int rc;
  gamut_opts *gopts;

  if(!opts) {
    s_log(G_ERR, "Input thread was given a NULL option struct.\n");
    return NULL;
  }

  gopts = (gamut_opts *)opts;

  /*
   * Lock and unlock the start lock so we know
   *   that our thread ID has been filled in.
   */
  rc = lock_start(gopts);
  if(rc < 0) {
    goto fail_out;
  }
  rc = unlock_start(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  parse_input(gopts, input_file, use_timestamps);

  rc = lock_master(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = send_master_cmd(gopts, MCMD_EXIT, NULL);
  if(rc < 0) {
    s_log(G_WARNING, "Error commanding the master to quit.\n");
  }

  (void)unlock_master(gopts);

fail_out:
  return NULL;
}

static void parse_input_timed(gamut_opts *gopts, FILE *infp)
{
  int rc;
  int linenum;
  struct timeval start_timetv;

  if(!gopts || !infp)
    return;

  /*
   * We loop over and over getting commands from the input file.
   *
   * Get the initial time and base our decisions on when to execute
   *   commands on that time.
   */
  linenum = 0;
  (void)gettimeofday(&start_timetv, NULL);
  while(!gopts->i_sync.exiting) {
    char buf[BUFSIZE+1];
    char *args[2];
    int nargs;

    rc = get_line(buf, BUFSIZE, infp, (uint64_t)0);
    if(rc < 0) {
      s_log(G_WARNING, "Error getting command.\n");
      break;
    } else if(!rc) {
      break;  /* input file */
    }

    chomp(buf);
    linenum++;

    nargs = split(NULL, buf, args, 2, ws_is_delim);
    if(nargs != 2) {
      s_log(G_WARNING, "Invalid command in input file line %u: %s\n",
                       linenum, buf);
      continue;
    }

    /*
     * Are we going to need to sleep before the next command?
     */
    {
      char *q;
      int64_t ptime_usec; /* Psuedo-time in usecs */
      int64_t rtime_usec; /* Real time in usec */
      int64_t wait_usec;  /* Psuedo-time minus real time */
      double next_time;
      struct timeval now_timetv;

      errno = 0;
      next_time = (double)strtod(args[0], &q);
      if(errno || (args[0] == q)) {
        s_log(G_WARNING, "Invalid time on line %u: \"%s\"\n",
                         linenum, args[0]);
        continue;
      }

      ptime_usec = (int64_t)(next_time * US_SEC);
      (void)gettimeofday(&now_timetv, NULL);
      rtime_usec = calculate_timediff(&start_timetv, &now_timetv);
      wait_usec = ptime_usec - rtime_usec;

      s_log(G_DEBUG, "PST %.2f PTU %lld RTU %llu WUS %lld\n",
                      next_time, (long long)ptime_usec,
                      (long long)rtime_usec, (long long)wait_usec);

      /*
       * Do we have time to sleep?
       */
      if(wait_usec > MIN_SLEEP_US) {
        struct timeval sleeptv;

        sleeptv.tv_sec  = wait_usec / US_SEC;
        sleeptv.tv_usec = wait_usec % US_SEC;
        (void)select(0, (fd_set *)NULL, (fd_set *)NULL,
                     (fd_set *)NULL, &sleeptv);
      }
    }

    /*
     * Now that we've slept (if necessary) it's time to do stuff.
     */
    {
      char *cbuf;
      char *cargs[2];
      int ncargs;
      cmd_handler *c_handle;

      s_log(G_NOTICE, "INPUT %s\n", args[1]);

      cbuf = args[1];
      ncargs = split(NULL, cbuf, cargs, 2, ws_is_delim);
      if(nargs < 1) {
        s_log(G_WARNING, "Invalid command string: \"%s\".\n", cbuf);
        continue;
      }

      if(!strcmp(cargs[0], "quit")) {
        goto clean_out;
      }

      /*
       * Either we're going to execute this command locally,
       *   or we're going to ship it off to the master thread.
       */
      c_handle = get_handler_by_msg(cargs[0]);
      if(!c_handle) {
        s_log(G_WARNING, "Invalid command: \"%s\".\n", cargs[0]);
        continue;
      }

      /*
       * If the function is defined, execute it here.
       *   Otherwise, send it to the master.
       */
      if(c_handle->func) {
        rc = c_handle->func(gopts, cargs[1]);
        if(rc < 0) {
          s_log(G_WARNING, "Error executing \"%s\".\n", cargs[0]);
        }
      }
      else { /* Send to the master */
        char mbuf[BUFSIZE];

        (void)snprintf(mbuf, BUFSIZE, "%s %s", cargs[0], cargs[1]);

        rc = lock_master(gopts);
        if(rc < 0) {
          s_log(G_WARNING, "Input couldn't get master.  Bailing.\n");
          goto clean_out;
        }

        rc = send_master_cmd(gopts, MCMD_INPUT, mbuf);
        if(rc < 0) {
          s_log(G_WARNING, "Error sending command to master.\n");
        }

        rc = unlock_master(gopts);
        if(rc < 0) {
          s_log(G_WARNING, "Input couldn't release master.  Bailing.\n");
        }
      }
    }
  }

clean_out:
  return;
}

static void parse_input_live(gamut_opts *gopts, FILE *infp)
{
  int rc;

  if(!infp || !gopts)
    return;

  /*
   * We loop over and over getting commands from input.
   *
   * We terminate the loop on a "quit".
   */
  while(!gopts->i_sync.exiting) {
    char buf[BUFSIZE+1];
    char *args[2];
    int nargs;
    cmd_handler *c_handle;

    rc = get_line(buf, BUFSIZE, infp, (uint64_t)0);
    if(rc < 0) {
      s_log(G_WARNING, "Error getting command.\n");
      break;
    } else if(!rc) {
      break;  /* remote end closed */
    }

    chomp(buf);

    s_log(G_NOTICE, "INPUT %s\n", buf);

    nargs = split(NULL, buf, args, 2, ws_is_delim);
    if(nargs < 1) {
      s_log(G_WARNING, "Invalid command string: \"%s\".\n", buf);
      continue;
    }

    if(!strcmp(args[0], "quit")) {
      goto clean_out;
    }

    /*
     * Either we're going to execute this command locally,
     *   or we're going to ship it off to the master thread.
     */
    c_handle = get_handler_by_msg(args[0]);
    if(!c_handle) {
      s_log(G_WARNING, "Invalid command: \"%s\".\n", args[0]);
      continue;
    }

    /*
     * If the function is defined, execute it here.
     *   Otherwise, send it to the master.
     */
    if(c_handle->func) {
      rc = c_handle->func(gopts, args[1]);
      if(rc < 0) {
        s_log(G_WARNING, "Error executing \"%s\".\n", args[0]);
      }
    }
    else { /* Send to the master */
      char mbuf[BUFSIZE];

      (void)snprintf(mbuf, BUFSIZE, "%s %s", args[0], args[1]);

      rc = lock_master(gopts);
      if(rc < 0) {
        s_log(G_WARNING, "Input couldn't get master.  Bailing.\n");
        goto clean_out;
      }

      rc = send_master_cmd(gopts, MCMD_INPUT, mbuf);
      if(rc < 0) {
        s_log(G_WARNING, "Error sending command to master.\n");
      }

      rc = unlock_master(gopts);
      if(rc < 0) {
        s_log(G_WARNING, "Input couldn't release master.  Bailing.\n");
      }
    }
  }

clean_out:
  return;
}

/********************** Begin worker functions ************************/
static int do_helo(gamut_opts *gopts, char *cmdstr)
{
  if(!gopts)
    return -1;

  s_log(G_NOTICE, "helo\n");
  return 0;
}

static int do_info(gamut_opts *gopts, char *cmdstr)
{
  int detail;
  int widx;
  worker_class wcls;

  if(!gopts)
    return -1;

  detail = 0;
  widx   = -1;
  wcls   = CLS_ALL;
  if(cmdstr && strlen(cmdstr)) {
    char *args[3];
    int i;
    int nargs;

    nargs = split(",", cmdstr, args, 3, ws_is_delim);
    for(i = 0;i < nargs;i++) {
      char *sargs[2];
      char *q;
      int nsargs;

      nsargs = split("=", args[i], sargs, 2, ws_is_delim);
      if(nsargs != 2) {
        s_log(G_WARNING, "Invalid info options: \"%s\"\n", args[i]);
        goto fail_out;
      }

      if(!strcmp("class", sargs[0])) {
        s_log(G_DEBUG, "Finding class \"%s\"\n", sargs[1]);
        if(!strcmp("cpu", sargs[1]))
          wcls = CLS_CPU;
        else if(!strcmp("mem", sargs[1]))
          wcls = CLS_MEM;
        else if(!strcmp("disk", sargs[1]))
          wcls = CLS_DISK;
        else if(!strcmp("net", sargs[1]))
          wcls = CLS_NET;
        else {
          s_log(G_WARNING, "Invalid class in info: \"%s\"\n", sargs[1]);
          goto fail_out;
        }
      }
      else if(!strcmp("worker", sargs[0])) {
        s_log(G_DEBUG, "Looking for worker \"%s\"\n", sargs[1]);
        errno = 0;
        widx = (int)strtoul(sargs[1], &q, 10);
        if(errno || (sargs[1] == q)) {
          s_log(G_WARNING, "Invalid worker ID: \"%s\"\n", sargs[1]);
          goto fail_out;
        }
      }
      else if(!strcmp("detail", sargs[0])) {
        s_log(G_DEBUG, "Setting detail level to \"%s\"\n", sargs[1]);
        errno = 0;
        detail = (int)strtoul(sargs[1], &q, 10);
        if(errno || (sargs[1] == q)) {
          s_log(G_WARNING, "Invalid detail level: \"%s\"\n", sargs[1]);
          goto fail_out;
        }
      }
      else {
        s_log(G_WARNING, "Invalid info tag: \"%s\"\n", sargs[0]);
        goto fail_out;
      }
    }
  }

  if(wcls == CLS_ALL) {
    widx = -1;
    print_stats_info(gopts, detail);
    print_worker_info(gopts, CLS_CPU, widx, detail);
    print_worker_info(gopts, CLS_MEM, widx, detail);
    print_worker_info(gopts, CLS_DISK, widx, detail);
    print_worker_info(gopts, CLS_NET, widx, detail);
  }
  else {
    print_worker_info(gopts, wcls, widx, detail);
  }

  return 0;

fail_out:
  return -1;
}

static int do_load(gamut_opts *gopts, char *cmdstr)
{
  if(!gopts)
    return -1;

  s_log(G_WARNING, "The \"load\" command is not implemented yet.\n");
  return -1;
}

static int do_opts(gamut_opts *gopts, char *cmdstr)
{
  if(!gopts)
    return -1;

  s_log(G_WARNING, "The \"opts\" command is not implemented yet.\n");
  return -1;
}

static int do_wait(gamut_opts *gopts, char *cmdstr)
{
  int rc;
  int frc;
  int num_waiting;
  uint64_t wait_usec;
  worker_class wcls;

  if(!gopts) {
    return -1;
  }

  frc         = -1;
  wcls        = CLS_ALL;
  wait_usec   = 0;
  num_waiting = 0;
  if(cmdstr && strlen(cmdstr)) {
    char *args[2];
    int i;
    int nargs;

    nargs = split(NULL, cmdstr, args, 2, ws_is_delim);
    if(nargs < 1) {
      s_log(G_WARNING, "Invalid options passed to wait: \"%s\"\n",
                       cmdstr);
      goto fail_out;
    }
    for(i = 0;i < nargs;i++) {
      char *sargs[2];
      int nsargs;

      nsargs = split("=", args[i], sargs, 2, ws_is_delim);
      if(nsargs != 2) {
        s_log(G_WARNING, "Invalid wait options: \"%s\"\n", args[i]);
        goto fail_out;
      }

      if(!strcmp("class", sargs[0])) {
        s_log(G_DEBUG, "Finding class \"%s\"\n", sargs[1]);
        if(!strcmp("cpu", sargs[1]))
          wcls = CLS_CPU;
        else if(!strcmp("mem", sargs[1]))
          wcls = CLS_MEM;
        else if(!strcmp("disk", sargs[1]))
          wcls = CLS_DISK;
        else if(!strcmp("net", sargs[1]))
          wcls = CLS_NET;
        else {
          s_log(G_WARNING, "Invalid class in wait: \"%s\"\n", sargs[1]);
          goto fail_out;
        }
      }
      else if(!strcmp("time", sargs[0])) {
        char *q;
        double dval;

        errno = 0;
        dval = strtod(sargs[1], &q);
        if(errno || (sargs[1] == q) || (dval < 0)) {
          s_log(G_WARNING, "Invalid time passed to wait: \"%s\"\n",
                           sargs[1]);
          goto fail_out;
        }

        wait_usec = (uint64_t)(dval * US_SEC);
      }
      else {
        s_log(G_WARNING, "Invalid wait tag: \"%s\"\n", sargs[0]);
        goto fail_out;
      }
    }
  }

  rc = lock_waiting(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  if(wcls == CLS_ALL) {
    rc = tag_worker_mwait(gopts, CLS_CPU);
    if(rc < 0) {
      goto waiting_out;
    }
    num_waiting += rc;

    rc = tag_worker_mwait(gopts, CLS_MEM);
    if(rc < 0) {
      goto waiting_out;
    }
    num_waiting += rc;

    rc = tag_worker_mwait(gopts, CLS_DISK);
    if(rc < 0) {
      goto waiting_out;
    }
    num_waiting += rc;

    rc = tag_worker_mwait(gopts, CLS_NET);
    if(rc < 0) {
      goto waiting_out;
    }
    num_waiting += rc;
  }
  else {
    rc = tag_worker_mwait(gopts, wcls);
    if(rc < 0) {
      goto waiting_out;
    }
    num_waiting += rc;
  }

  if(num_waiting) {
    gopts->wcounter.count = num_waiting;
    s_log(G_DEBUG, "Planning to wait %llu usecs for %u workers.\n",
                   wait_usec, gopts->wcounter.count);

    rc = wait_waiting(gopts, wait_usec);
    if(rc == ETIMEDOUT) {
      s_log(G_DEBUG, "Collected %u workers before we timed out.\n",
                     num_waiting - gopts->wcounter.count);
      frc = num_waiting - gopts->wcounter.count;
    }
    else if(rc < 0) {
      frc = -1;
    }
    else {
      s_log(G_DEBUG, "Should have collected everyone: %u left.\n",
                     gopts->wcounter.count);
      frc = num_waiting - gopts->wcounter.count;
    }
  }
  else {
    s_log(G_NOTICE, "No workers on which we can wait.\n");
    frc = 0;
  }

waiting_out:
  rc = unlock_waiting(gopts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}
/*********************** End worker functions *************************/
