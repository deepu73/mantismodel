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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>

#include "utillog.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

static void del_worker_cls(gamut_opts *opts, worker_class wcls);

/*
 * Simple commands to add, modify, or delete a worker.
 */
int add_worker(gamut_opts *opts, worker_class wcls, char *attrs)
{
  int idx;
  int frc;
  int rc;
  shared_opts *shopts;

  if(!opts || !is_valid_cls(wcls) || !attrs)
    return -1;

  frc    = -1;
  shopts = NULL;

  rc = lock_stats(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_class(opts, wcls);
  if(rc < 0) {
    goto stats_out;
  }

  idx = insert_worker(opts, wcls, attrs);
  if(idx < 0) {
    goto class_out;
  }

  /*
   * Since insert_worker doesn't explicitly set the worker
   *   to pending, we do that here.
   */
  rc = lock_worker(opts, wcls, idx);
  if(rc < 0) {
    goto class_out;
  }

  shopts = get_shared_opts(opts, wcls, idx);
  if(!shopts) {
    (void)unlock_worker(opts, wcls, idx);
    goto class_out;
  }

  shopts->pending = 1;
  opts->wstats.workers_pending++;

  /*
   * Unlock the worker before going to start_queued_worker
   *   because s_q_w will try to reaquire the lock.
   */
  rc = unlock_worker(opts, wcls, idx);
  if(rc < 0) {
    goto class_out;
  }

  rc = start_queued_worker(opts, wcls, idx);
  if(rc < 0) {
    goto class_out;
    frc = 0;  /* Queued but not started */
  }
  else {
    frc = 1;  /* Queued and started */
  }

class_out:
  rc = unlock_class(opts, wcls);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

int queue_worker(gamut_opts *opts, worker_class wcls, char *attrs)
{
  int idx;
  int frc;
  int rc;

  if(!opts || !is_valid_cls(wcls) || !attrs)
    return -1;

  frc = -1;

  rc = lock_stats(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_class(opts, wcls);
  if(rc < 0) {
    goto stats_out;
  }

  idx = insert_worker(opts, wcls, attrs);
  if(idx < 0) {
    goto class_out;
  }
  else {
    frc = 0;
  }

class_out:
  rc = unlock_class(opts, wcls);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

int start_worker(gamut_opts *opts, worker_class wcls, char *wlabel)
{
  int idx;
  int frc;
  int rc;
  shared_opts *shopts;
  worker_class acls;

  if(!opts || !is_valid_cls(wcls) || !wlabel)
    return -1;

  frc    = -1;
  acls   = wcls;
  shopts = NULL;

  rc = find_worker_by_label(opts, &acls, wlabel, &idx);
  if(rc <= 0) {
    goto fail_out;
  }
  if(acls != wcls) {
    s_log(G_WARNING, "Found worker %s in class %d, not class %d.\n",
                     wlabel, acls, wcls);
    goto fail_out;
  }

  rc = lock_stats(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_class(opts, wcls);
  if(rc < 0) {
    goto stats_out;
  }

  /*
   * Since insert_worker doesn't explicitly set the worker to
   *   pending, we do that here, so start_queued_worker will work.
   */
  rc = lock_worker(opts, wcls, idx);
  if(rc < 0) {
    goto class_out;
  }

  shopts = get_shared_opts(opts, wcls, idx);
  if(!shopts) {
    (void)unlock_worker(opts, wcls, idx);
    goto class_out;
  }

  shopts->pending = 1;
  opts->wstats.workers_pending++;

  /*
   * Unlock the worker before going to start_queued_worker
   *   because s_q_w will try to reaquire the lock.
   */
  rc = unlock_worker(opts, wcls, idx);
  if(rc < 0) {
    goto class_out;
  }

  rc = start_queued_worker(opts, wcls, idx);
  if(rc < 0) {
    goto class_out;
    frc = 0;  /* Queued but not started */
  }
  else {
    frc = 1;  /* Queued and started */
  }

class_out:
  rc = unlock_class(opts, wcls);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

int mod_worker(gamut_opts *opts, worker_class wcls,
               char *wlabel, char *newattrs)
{
  int rc;
  int frc;
  int lidx;
  worker_class lcls;

  if(!opts || !is_valid_cls(wcls) || !wlabel || !newattrs) {
    s_log(G_WARNING, "Invalid options passed to mod_worker.\n");
    return -1;
  }

  frc = -1;
  rc = lock_stats(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_class(opts, wcls);
  if(rc < 0) {
    goto stats_out;
  }

  /*
   * We use a local copy here so a failed find_worker_* won't
   *   prevent us from unlocking the correct class.
   */
  lcls = wcls;
  rc = find_worker_by_label(opts, &lcls, wlabel, &lidx);
  if(!rc) {
    goto class_out;
  }

  rc = parse_worker_opts(opts, lcls, lidx, newattrs);
  if(rc < 0) {
    goto class_out;
  }
  else {
    frc = 0;
  }

class_out:
  rc = unlock_class(opts, wcls);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return -1;
}

int del_worker(gamut_opts *opts, worker_class wcls, char *wlabel)
{
  int rc;
  int frc;

  if(!opts || !is_valid_cls(wcls))
    return -1;

  frc = -1;
  rc = lock_stats(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_class(opts, wcls);
  if(rc < 0) {
    goto stats_out;
  }

  if(!wlabel || !strlen(wlabel)) {
    del_worker_cls(opts, wcls);
  }
  else {
    int lidx;
    worker_class lcls;

    lcls = wcls;
    rc = find_worker_by_label(opts, &lcls, wlabel, &lidx);
    if(!rc) {
      goto class_out;
    }

    rc = kill_worker(opts, lcls, lidx);
    if(rc < 0) {
      goto class_out;
    }
    else {
      frc = 0;
    }
  }

class_out:
  rc = unlock_class(opts, wcls);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

/*
 * Check to see if there are pending workers we can start.
 */
int chk_worker(gamut_opts *opts)
{
  int i;
  int rc;
  int frc;

  if(!opts)
    return -1;

  frc = -1;
  rc = lock_after(opts);
  if(rc < 0) {
    goto fail_out;
  }

  if(!opts->a_sync.wqueue_size) {
    frc = 0;
    goto after_out;
  }

  rc = lock_stats(opts);
  if(rc < 0) {
    goto after_out;
  }

  for(i = 0;i < opts->a_sync.wqueue_size;i++) {
    int widx;
    worker_class wcls;

    widx = opts->a_sync.wdata[i].worker_index;
    wcls = opts->a_sync.wdata[i].wcls;

    rc = lock_class(opts, wcls);
    if(rc < 0) {
      goto stats_out;
    }

    rc = start_queued_worker(opts, wcls, widx);
    if(rc < 0) {
      (void)unlock_class(opts, wcls);
    }

    rc = unlock_class(opts, wcls);
    if(rc < 0) {
      goto stats_out;
    }
  }

  frc                      = 0;
  opts->a_sync.wqueue_size = 0;

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    frc = -1;
  }

after_out:
  rc = unlock_after(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

static void del_worker_cls(gamut_opts *opts, worker_class wcls)
{
  int i;
  unsigned int max_workers;

  if(!opts)
    return;

  switch(wcls) {
    case CLS_CPU:
      max_workers = MAX_CPUS;
      break;

    case CLS_MEM:
      max_workers = MAX_MEMS;
      break;

    case CLS_DISK:
      max_workers = MAX_DIOS;
      break;

    case CLS_NET:
      max_workers = MAX_NIOS;
      break;

    default:
      return;
  }

  for(i = 0;i < max_workers;i++) {
    (void)kill_worker(opts, wcls, i);
  }
}
