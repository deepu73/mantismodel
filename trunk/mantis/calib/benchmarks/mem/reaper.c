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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "reaper.h"
#include "utillog.h"
#include "workeropts.h"
#include "workersync.h"

static void* reaper(void *opts);
static int can_reap_worker(shared_opts *shopts);
static int reap_workers(gamut_opts *gopts);

/*
 * Fire up the reaper.
 */
void start_reaper(gamut_opts *gopts)
{
  int rc;

  if(!gopts) {
    exit(EXIT_FAILURE);
  }

  rc = lock_start(gopts);
  if(rc < 0) {
    exit(EXIT_FAILURE);
  }

  rc = pthread_create(&gopts->r_sync.t_sync.tid, (pthread_attr_t *)NULL,
                      reaper, (void *)gopts);

  (void)unlock_start(gopts);

  if(rc < 0) {
    s_log(G_WARNING, "Error starting reaper.\n");
    exit(EXIT_FAILURE);
  }
  else {
    s_log(G_DEBUG, "Started reaper (tid %lu).\n",
                   gopts->r_sync.t_sync.tid);
    return;
  }
}

/*
 * Shut down the reaper.
 *   All other workers must be killed off for this to be OK.
 *   NOTE: We don't enforce that right now.
 */
void stop_reaper(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return;

  /*
   * To kill off the reaper, tell it it's time to exit,
   *   and then signal it.  Lock the reaper lock so it
   *   doesn't miss our signal.
   */
  rc = lock_reaper(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  gopts->r_sync.exiting = 1;

  rc = signal_reaper(gopts);
  if(rc < 0) {
    goto reaper_out;
  }
  else {
    s_log(G_DEBUG, "Signalled reaper for exit.\n");
  }

  rc = unlock_reaper(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = pthread_join(gopts->r_sync.t_sync.tid, (void **)NULL);
  if(rc < 0) {
    s_log(G_WARNING, "Error collecting reaper.\n");
  }
  else {
    s_log(G_DEBUG, "Collected reaper.\n");
  }

reaper_out:
  (void)unlock_reaper(gopts);

fail_out:
  return;
}

static void* reaper(void *opts)
{
  int rc;
  gamut_opts *gopts;

  if(!opts) {
    s_log(G_ERR, "Reaper was given a NULL option struct.\n");
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

  rc = lock_reaper(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  do {
    s_log(G_DEBUG, "Preparing to wait for signal.\n");
    rc = wait_reaper(gopts);
    if(rc < 0) {
      goto fail_out;
    }

    rc = reap_workers(gopts);
    if(rc < 0) {
      goto fail_out;
    }
    else {
      s_log(G_INFO, "Reaped %d workers.\n", rc);
    }
  } while(!gopts->r_sync.exiting);

fail_out:
  (void)unlock_reaper(gopts);

  return NULL;
}

/*
 * In order to be reap-able, the worker has to come from a used slot,
 *   be exiting, and have a thread ID.
 */
static int can_reap_worker(shared_opts *shopts)
{
  if(!shopts)
    return 0;

  if(!shopts->used || !shopts->exiting || !shopts->t_sync.tid)
    return 0;
  else
    return 1;
}

static int reap_workers(gamut_opts *gopts)
{
  int i;
  int rc;
  int num_exit;
  int num_reaped;

  num_exit   = 0;
  num_reaped = 0;
  for(i = 0;i < gopts->r_sync.wqueue_size;i++) {
    int widx;
    shared_opts *shopts;
    worker_class wcls;

    widx = gopts->r_sync.wdata[i].worker_index;
    wcls = gopts->r_sync.wdata[i].wcls;

    if(!is_valid_cls(wcls)) {
      s_log(G_WARNING, "Asked to reap invalid class %d.\n", wcls);
      continue;
    }

    shopts = get_shared_opts(gopts, wcls, widx);
    if(!shopts) {
       s_log(G_WARNING, "Could not find exiting worker (%d, %d).\n",
                       (int)wcls, widx);
    }

    rc = can_reap_worker(shopts);
    if(!rc) {
      s_log(G_WARNING, "Cannot reap worker %u (%s).\n",
                       (uint32_t)shopts->wid, shopts->label);
      continue;
    }

    rc = pthread_join(shopts->t_sync.tid, (void **)NULL);
    if(rc < 0) {
      s_log(G_WARNING, "Error reaping worker %u (%s).\n",
                       (uint32_t)shopts->wid, shopts->label);
      continue;
    }
    else {
      s_log(G_DEBUG, "Reaped worker %u (%s).\n",
                     (uint32_t)shopts->wid, shopts->label);
    }

    if(shopts->exiting) {
      num_exit++;
    }
    num_reaped++;

    clean_worker_opts(gopts, wcls, widx, WC_NOKEEPID);
  }

  /*
   * We lock the stats down here so we don't create a deadlock
   *   when the dying worker pokes the master to start
   *   an 'after' worker (which tries to get the stats lock).
   */
  rc = lock_stats(gopts);
  if(rc < 0) {
    return -1;
  }

  gopts->wstats.workers_exiting -= num_exit;
  gopts->wstats.workers_reaped  += num_reaped;
  gopts->r_sync.wqueue_size      = 0;

  rc = unlock_stats(gopts);
  if(rc < 0) {
    num_reaped = -1;
  }

  return num_reaped;
}
