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
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>

#include "input.h"
#include "linkctl.h"
#include "mainctl.h"
#include "opts.h"
#include "utillog.h"
#include "workerctl.h"
#include "workerinfo.h"
#include "workeropts.h"
#include "workerwait.h"
#include "workersync.h"

#define create_handler(h) \
static int (h)(gamut_opts *gopts, char *cmdstr)

create_handler(do_link);
create_handler(do_wctl);

static cmd_handler c_handlers[] = {
  { "link", do_link },
  { "wctl", do_wctl }
};
static int num_handlers = sizeof(c_handlers) / sizeof(c_handlers[0]);

static int run_input_cmd(gamut_opts *gopts);
static gamut_handler get_handler_by_msg(const char *cmd);

static link_cmd get_lcmd(const char *cmdstr);
static worker_cmd get_wcmd(const char *cmdstr);
static worker_class get_wcls(const char *clsstr);

/*
 * The Real Deal.
 *
 * Wait on the lock and accept commands from other threads
 *   that poke at us.
 */
void execute_gamut(gamut_opts *opts)
{
  int rc;
  int exiting;

  if(!opts) {
    s_log(G_WARNING, "No options passed to main loop.\n");
    return;
  }

  rc = lock_master(opts);
  if(rc < 0) {
    goto fail_out;
  }

  exiting = 0;
  while(!exiting) {
    rc = signal_master(opts);
    if(rc < 0) {
      goto master_out;
    }

    rc = wait_master(opts);
    if(rc < 0) {
      goto master_out;
    }

    switch(opts->mctl.mcmd) {
      case MCMD_FREE:    /* Why were we woken up? */
        s_log(G_WARNING, "Master woken for no good reason.\n");
        break;

      case MCMD_INPUT:   /* Input from stdin */
        rc = run_input_cmd(opts);
        if(rc < 0) {
          s_log(G_WARNING, "Master error running input command.\n");
        }
        else {
          s_log(G_DEBUG, "Master ran command successfully.\n");
        }
        break;

      case MCMD_AFTER:  /* We need to poke another worker to start */
        rc = chk_worker(opts);
        if(rc < 0) {
          s_log(G_WARNING, "Error starting 'after' worker.\n");
        }
        else {
          s_log(G_DEBUG, "Master successfully ran 'after' worker.\n");
        }
        break;

      case MCMD_EXIT:  /* Bail out */
        exiting = 1;
        break;

      default:
        s_log(G_WARNING, "Unknown command delivered to master: %d.\n",
                         opts->mctl.mcmd);
        break;
    }

    /*
     * Now that we've executed the command, clean everything out
     *   and prepare for the next command.
     */
    opts->mctl.mcmd = MCMD_FREE;
    memset(opts->mctl.mbuf, 0, sizeof(opts->mctl.mbuf));
  }

  return;

  /*
   * We'll end up here only if there's an error.
   */
master_out:
  (void)unlock_master(opts);

fail_out:
  return;
}

/*
 * This is how we notify the master thread that we need it
 *   to do something.
 *
 * NOTE: We MUST have the master lock upon entering this function.
 */
int send_master_cmd(gamut_opts *gopts, worker_cmd wcmd,
                    char *cmdstr)
{
  int rc;
  int frc;
  int msg_sent;

  if(!gopts || !is_valid_mcmd(wcmd))
    return -1;

  frc      = -1;
  msg_sent = 0;

  s_log(G_DEBUG, "Sending %d command to master (%s).\n",
                 (int)wcmd, (cmdstr ? cmdstr : "null"));

  while(!msg_sent) {
    if(gopts->mctl.mcmd == MCMD_FREE) {
      gopts->mctl.mcmd = wcmd;
      memset(gopts->mctl.mbuf, 0, BUFSIZE);

      if(cmdstr) {
        strncpy(gopts->mctl.mbuf, cmdstr, BUFSIZE);
      }

      rc = signal_master(gopts);
      if(rc < 0) {
        goto fail_out;
      }

      frc      = 0;
      msg_sent = 1;
      s_log(G_DEBUG, "Message sent.\n");
    }
    else {
      /*
       * If we got the lock but the command isn't free,
       *   that means someone else sent a command and we woke
       *   up before the master did.
       * Broadcast on the master, hoping that we wake the master up.
       *   Then wait and hope we get it this time.
       */
      rc = broadcast_master(gopts);
      if(rc < 0) {
        goto fail_out;
      }
      else {
        s_log(G_DEBUG, "MASTER!  Wake up!!\n");
      }

      rc = wait_master(gopts);
      if(rc < 0) {
        goto fail_out;
      }
    }
  }

fail_out:
  return frc;
}

static int run_input_cmd(gamut_opts *gopts)
{
  char *cbuf;
  char *args[2];
  int frc;
  int nargs;
  gamut_handler func;

  if(!gopts)
    return -1;

  frc  = -1;
  cbuf = gopts->mctl.mbuf;

  s_log(G_NOTICE, "MASTER %s\n", cbuf);

  nargs = split(NULL, cbuf, args, 2, ws_is_delim);
  if(nargs < 1) {
    s_log(G_WARNING, "Invalid command string: \"%s\"\n", cbuf);
    goto clean_out;
  }

  /*
   * See if we can find the function we're supposed to run.
   */
  func = get_handler_by_msg(args[0]);
  if(!func) {
    s_log(G_WARNING, "Invalid command: \"%s\".\n", args[0]);
    goto clean_out;
  }

  frc = func(gopts, args[1]);
  if(frc < 0) {
    s_log(G_WARNING, "MASTER: Error executing command.\n");
  }
  else {
    s_log(G_DEBUG, "MASTER: Executed command successfully.\n");
  }

clean_out:
  return frc;
}

/*
 * Given a command string, which function should we execute?
 */
static gamut_handler get_handler_by_msg(const char *cmd)
{
  int i;

  if(!cmd || !(*cmd))
    return (gamut_handler)NULL;

  for(i = 0;i < num_handlers;i++) {
    if(!strcasecmp(c_handlers[i].cmd, cmd))
      return c_handlers[i].func;
  }

  return (gamut_handler)NULL;
}

static int do_link(gamut_opts *gopts, char *cmdstr)
{
  char *args[3];
  int rc;
  int nargs;
  link_cmd lcmd;

  if(!gopts || !cmdstr)
    return -1;

  nargs = split(NULL, cmdstr, args, 3, ws_is_delim);
  if(nargs < 2) {
    return -1;
  }

  lcmd = get_lcmd(args[0]);
  if(lcmd == LINK_ERROR) {
    return -1;
  }

  switch(lcmd) {
    case LINK_QUEUE:
      rc = queue_link(gopts, args[1], args[2]);
      if(rc < 0) {
        s_log(G_WARNING, "Error queueing link.\n");
      }
      break;

    case LINK_START:
      rc = start_link(gopts, args[1]);
      if(rc < 0) {
        s_log(G_WARNING, "Error starting linked workers.\n");
      }
      break;

    case LINK_DEL:
      rc = del_link(gopts, args[1]);
      if(rc < 0) {
        s_log(G_WARNING, "Error removing linked workers.\n");
      }
      break;

    default:
      break;
  }

  return 0;
}

static int do_wctl(gamut_opts *gopts, char *cmdstr)
{
  char *args[4];
  int rc;
  int nargs;
  worker_cmd wcmd;
  worker_class wcls;

  if(!gopts || !cmdstr)
    return -1;

  nargs = split(NULL, cmdstr, args, 4, ws_is_delim);
  if(nargs < 2) {
    return -1;
  }

  wcmd = get_wcmd(args[0]);
  wcls = get_wcls(args[1]);
  if((wcmd == WCTL_ERROR) || (wcls == CLG_ERROR)) {
    return -1;
  }

  switch(wcmd) {
    case WCTL_ADD:
      rc = add_worker(gopts, wcls, args[2]);
      if(rc < 0) {
        s_log(G_WARNING, "Error adding worker.\n");
      }
      break;

    case WCTL_QUEUE:
      rc = queue_worker(gopts, wcls, args[2]);
      if(rc < 0) {
        s_log(G_WARNING, "Error queueing up worker.\n");
      }
      break;

    case WCTL_START:
      rc = start_worker(gopts, wcls, args[2]);
      if(rc < 0) {
        s_log(G_WARNING, "Error starting worker.\n");
      }
      break;

    case WCTL_MOD:
      rc = mod_worker(gopts, wcls, args[2], args[3]);
      if(rc < 0) {
        s_log(G_WARNING, "Error modifying existing worker.\n");
      }
      break;

    case WCTL_DEL:
      rc = del_worker(gopts, wcls, args[2]);
      if(rc < 0) {
        s_log(G_WARNING, "Error deleting existing worker.\n");
      }
      break;

    default:
      break;
  }

  return 0;
}

static link_cmd get_lcmd(const char *cmdstr)
{
  if(!cmdstr || !(*cmdstr))
    return LINK_ERROR;

  if(!strcasecmp(cmdstr, "queue")) {
    return LINK_QUEUE;
  }
  else if(!strcasecmp(cmdstr, "start")) {
    return LINK_START;
  }
  else if(!strcasecmp(cmdstr, "del")) {
    return LINK_DEL;
  }
  else {
    return LINK_ERROR;
  }
}

static worker_cmd get_wcmd(const char *cmdstr)
{
  if(!cmdstr || !(*cmdstr))
    return WCTL_ERROR;

  if(!strcasecmp(cmdstr, "add")) {
    return WCTL_ADD;
  }
  else if(!strcasecmp(cmdstr, "queue")) {
    return WCTL_QUEUE;
  }
  else if(!strcasecmp(cmdstr, "start")) {
    return WCTL_START;
  }
  else if(!strcasecmp(cmdstr, "mod")) {
    return WCTL_MOD;
  }
  else if(!strcasecmp(cmdstr, "del")) {
    return WCTL_DEL;
  }
  else {
    return WCTL_ERROR;
  }
}

static worker_class get_wcls(const char *clsstr)
{
  if(!clsstr || !(*clsstr))
    return CLG_ERROR;

  if(!strcasecmp(clsstr, "cpu")) {
    return CLS_CPU;
  }
  else if(!strcasecmp(clsstr, "mem")) {
    return CLS_MEM;
  }
  else if(!strcasecmp(clsstr, "disk")) {
    return CLS_DISK;
  }
  else if(!strcasecmp(clsstr, "net")) {
    return CLS_NET;
  }
  else {
    return CLG_ERROR;
  }
}
