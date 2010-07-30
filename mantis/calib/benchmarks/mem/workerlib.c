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

#include "calibrate.h"
#include "linklib.h"
#include "opts.h"
#include "utilio.h"
#include "utillog.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

#include "cpuworker.h"
#include "diskworker.h"
#include "memworker.h"
#include "networker.h"

static int can_start_worker(shared_opts *shopts);
static int sync_start(gamut_opts *opts);
static int find_open_slot(gamut_opts *opts, worker_class wcls);

/*
 * Insert a worker, but don't actually launch it yet.
 */
int insert_worker(gamut_opts *opts, worker_class wcls, char *attrs)
{
  int idx;
  int rc;
 
  if(!opts || !is_valid_cls(wcls) || !attrs)
    return -1;

  idx = find_open_slot(opts, wcls);
  if(idx < 0) {
    s_log(G_WARNING, "Could not find open slot for new worker.\n");
    goto clean_out;
  }
  else {
    s_log(G_DEBUG, "New slot %d for class %d.\n", idx, wcls);
  }
 
  /*
   * Now that we've found an open slot, fill in the values.
   */
  rc = parse_worker_opts(opts, wcls, idx, attrs);
  if(rc < 0) {
    s_log(G_WARNING, "Error parsing worker options.\n");
    idx = -1;
  }
  else {
    s_log(G_DEBUG, "Parsed option for new worker.\n");
  }

clean_out:
  return idx;
}

/*
 * Start a queued worker.
 * Uses the same procedure as startall_queued_worrkers,
 *   just on a smaller scale.
 */
int start_queued_worker(gamut_opts *opts, worker_class wcls, int widx)
{
  int rc;   /* Return code for individual operations */
  int frc;  /* Return code for this functions */
  shared_opts *shopts;
  gamut_worker worker_func;

  frc = -1;
  
  if(!opts || !is_valid_cls(wcls) || (widx < 0))
    return frc;

  shopts      = NULL;
  worker_func = NULL;
  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS)
        break;

      shopts      = &opts->cpu[widx].shopts;
      worker_func = cpuworker;
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS)
        break;

      shopts      = &opts->mem[widx].shopts;
      worker_func = memworker;
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS)
        break;

      shopts      = &opts->disk_io[widx].shopts;
      worker_func = diskworker;
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS)
        break;

      shopts      = &opts->net_io[widx].shopts;
      worker_func = networker;
      break;

    default:
      break;
  }

  if(!shopts || !worker_func) {
    s_log(G_WARNING, "Couldn't find parameters for worker.\n");
    goto worker_out;
  }

  rc = lock_worker(opts, wcls, widx);
  if(rc < 0) {
    goto worker_out;
  }

  /*
   * It's not an error if we can't start the 
   */
  rc = can_start_worker(shopts);
  if(rc < 0) {
    s_log(G_WARNING, "Unable to start worker %s.\n", shopts->label);
    goto worker_out;
  }
  else if(!rc) {
    s_log(G_NOTICE, "Will start worker %s later.\n", shopts->label);
    frc = 0;
    goto worker_out;
  }

  rc = lock_start(opts);
  if(rc < 0) {
    goto worker_out;
  }

  rc = pthread_create(&shopts->t_sync.tid, (pthread_attr_t *)NULL,
                      worker_func, (void*)opts);

  (void)unlock_start(opts);

  if(rc) {
    s_log(G_WARNING, "Error launching worker %s.\n",
                     shopts->label);
    frc = -1;
  }
  else {
    s_log(G_INFO, "Launched worker %s (tid %lu).\n",
                  shopts->label, shopts->t_sync.tid);
    frc = 1;
  }

  opts->wstats.workers_spawned++;

worker_out:
  rc = unlock_worker(opts, wcls, widx);
  if(rc < 0) {
    frc = -1;
  }

  return frc;
}

/*
 * Try to find a worker with the given worker ID, label or thread ID.
 *   If wcls is valid on entry, search only in that class.
 *   Otherwise, search in all classes and fill in wcls with the
 *   class of where we found it (if at all).
 */
int find_worker_by_wid(gamut_opts *opts, worker_class *wcls,
                       workerID wid, int *widx)
{
  int i;
  int idx;
  worker_class tcls;

  if(!opts || !wcls || !widx)
    return -1;

  idx  = -1;
  tcls = CLS_NONE;
  if(is_valid_cls(*wcls)) {
    switch(*wcls) {
      case CLS_CPU:
        for(i = 0;i < MAX_CPUS;i++) {
          if(opts->cpu[i].shopts.wid == wid) {
            idx  = i;
            tcls = CLS_CPU;
            break;
          }
        }
        break;

      case CLS_MEM:
        for(i = 0;i < MAX_MEMS;i++) {
          if(opts->mem[i].shopts.wid == wid) {
            idx  = i;
            tcls = CLS_MEM;
            break;
          }
        }
        break;

      case CLS_DISK:
        for(i = 0;i < MAX_DIOS;i++) {
          if(opts->disk_io[i].shopts.wid == wid) {
            idx  = i;
            tcls = CLS_DISK;
            break;
          }
        }
        break;

      case CLS_NET:
        for(i = 0;i < MAX_NIOS;i++) {
          if(opts->net_io[i].shopts.wid == wid) {
            idx  = i;
            tcls = CLS_NET;
            break;
          }
        }
        break;

      default:
        break;
    }
  }
  else if((*wcls) == CLS_ALL) {
    /*
     * Search through all the classes.
     */
    for(i = 0;i < MAX_CPUS;i++) {
      if(opts->cpu[i].shopts.wid == wid) {
        idx  = i;
        tcls = CLS_CPU;
        break;
      }
    }
    if(i != MAX_CPUS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_MEMS;i++) {
      if(opts->mem[i].shopts.wid == wid) {
        idx  = i;
        tcls = CLS_MEM;
        break;
      }
    }
    if(i != MAX_MEMS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_DIOS;i++) {
      if(opts->disk_io[i].shopts.wid == wid) {
        idx  = i;
        tcls = CLS_DISK;
        break;
      }
    }
    if(i != MAX_DIOS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_NIOS;i++) {
      if(opts->net_io[i].shopts.wid == wid) {
        idx  = i;
        tcls = CLS_NET;
        break;
      }
    }
    if(i != MAX_NIOS) {
      goto clean_out;
    }
  }

clean_out:
  if((tcls == CLS_NONE) || (idx < 0)) {
    *wcls = CLG_ERROR;
    *widx = -1;
    return 0;
  }
  else {
    *wcls = tcls;
    *widx = idx;
    return 1;
  }
}

int find_worker_by_label(gamut_opts *opts, worker_class *wcls,
                         char *wlabel, int *widx)
{
  int i;
  int idx;
  worker_class tcls;

  if(!opts || !wcls || !widx || !wlabel || !strlen(wlabel))
    return -1;

  idx  = -1;
  tcls = CLS_NONE;
  if(is_valid_cls(*wcls)) {
    switch(*wcls) {
      case CLS_CPU:
        for(i = 0;i < MAX_CPUS;i++) {
          if(!opts->cpu[i].shopts.used)
            continue;
          if(!strcmp(opts->cpu[i].shopts.label, wlabel)) {
            idx  = i;
            tcls = CLS_CPU;
            break;
          }
        }
        break;

      case CLS_MEM:
        for(i = 0;i < MAX_MEMS;i++) {
          if(!opts->mem[i].shopts.used)
            continue;
          if(!strcmp(opts->mem[i].shopts.label, wlabel)) {
            idx  = i;
            tcls = CLS_MEM;
            break;
          }
        }
        break;

      case CLS_DISK:
        for(i = 0;i < MAX_DIOS;i++) {
          if(!opts->disk_io[i].shopts.used)
            continue;
          if(!strcmp(opts->disk_io[i].shopts.label, wlabel)) {
            idx  = i;
            tcls = CLS_DISK;
            break;
          }
        }
        break;

      case CLS_NET:
        for(i = 0;i < MAX_NIOS;i++) {
          if(!opts->net_io[i].shopts.used)
            continue;
          if(!strcmp(opts->net_io[i].shopts.label, wlabel)) {
            idx  = i;
            tcls = CLS_NET;
            break;
          }
        }
        break;

      default:
        break;
    }
  }
  else if((*wcls) == CLS_ALL) {
    /*
     * Search through all the classes.
     */
    for(i = 0;i < MAX_CPUS;i++) {
      if(!opts->cpu[i].shopts.used)
        continue;
      if(!strcmp(opts->cpu[i].shopts.label, wlabel)) {
        idx  = i;
        tcls = CLS_CPU;
        break;
      }
    }
    if(i != MAX_CPUS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_MEMS;i++) {
      if(!opts->mem[i].shopts.used)
        continue;
      if(!strcmp(opts->mem[i].shopts.label, wlabel)) {
        idx  = i;
        tcls = CLS_MEM;
        break;
      }
    }
    if(i != MAX_MEMS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_DIOS;i++) {
      if(!opts->disk_io[i].shopts.used)
        continue;
      if(!strcmp(opts->disk_io[i].shopts.label, wlabel)) {
        idx  = i;
        tcls = CLS_DISK;
        break;
      }
    }
    if(i != MAX_DIOS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_NIOS;i++) {
      if(!opts->net_io[i].shopts.used)
        continue;
      if(!strcmp(opts->net_io[i].shopts.label, wlabel)) {
        idx  = i;
        tcls = CLS_NET;
        break;
      }
    }
    if(i != MAX_NIOS) {
      goto clean_out;
    }
  }

clean_out:
  if((tcls == CLS_NONE) || (idx < 0)) {
    *wcls = CLG_ERROR;
    *widx = -1;
    return 0;
  }
  else {
    *wcls = tcls;
    *widx = idx;
    return 1;
  }
}

int find_worker_by_tid(gamut_opts *opts, worker_class *wcls,
                       pthread_t tid, int *widx)
{
  int i;
  int idx;
  worker_class tcls;

  if(!opts || !wcls || !widx)
    return -1;

  idx  = -1;
  tcls = CLS_NONE;
  if(is_valid_cls(*wcls)) {
    switch(*wcls) {
      case CLS_CPU:
        for(i = 0;i < MAX_CPUS;i++) {
          if(opts->cpu[i].shopts.t_sync.tid == tid) {
            idx  = i;
            tcls = CLS_CPU;
            break;
          }
        }
        break;

      case CLS_MEM:
        for(i = 0;i < MAX_MEMS;i++) {
          if(opts->mem[i].shopts.t_sync.tid == tid) {
            idx  = i;
            tcls = CLS_MEM;
            break;
          }
        }
        break;

      case CLS_DISK:
        for(i = 0;i < MAX_DIOS;i++) {
          if(opts->disk_io[i].shopts.t_sync.tid == tid) {
            idx  = i;
            tcls = CLS_DISK;
            break;
          }
        }
        break;

      case CLS_NET:
        for(i = 0;i < MAX_NIOS;i++) {
          if(opts->net_io[i].shopts.t_sync.tid == tid) {
            idx  = i;
            tcls = CLS_NET;
            break;
          }
        }
        break;

      default:
        break;
    }
  }
  else if((*wcls) == CLS_ALL) {
    /*
     * Search through all the classes.
     */
    for(i = 0;i < MAX_CPUS;i++) {
      if(opts->cpu[i].shopts.t_sync.tid == tid) {
        idx  = i;
        tcls = CLS_CPU;
        break;
      }
    }
    if(i != MAX_CPUS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_MEMS;i++) {
      if(opts->mem[i].shopts.t_sync.tid == tid) {
        idx  = i;
        tcls = CLS_MEM;
        break;
      }
    }
    if(i != MAX_MEMS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_DIOS;i++) {
      if(opts->disk_io[i].shopts.t_sync.tid == tid) {
        idx  = i;
        tcls = CLS_DISK;
        break;
      }
    }
    if(i != MAX_DIOS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_NIOS;i++) {
      if(opts->net_io[i].shopts.t_sync.tid == tid) {
        idx  = i;
        tcls = CLS_NET;
        break;
      }
    }
    if(i != MAX_NIOS) {
      goto clean_out;
    }
  }

clean_out:
  if((tcls == CLS_NONE) || (idx < 0)) {
    *wcls = CLG_ERROR;
    *widx = -1;
    return 0;
  }
  else {
    *wcls = tcls;
    *widx = idx;
    return 1;
  }
}

/*
 * Try to find an 'after' worker with the given worker ID or label.
 *   If wcls is valid on entry, search only in that class.
 *   Otherwise, search in all classes and fill in wcls with the
 *   class of where we found it (if at all).
 */
int find_after_by_label(gamut_opts *opts, worker_class *wcls,
                        char *alabel, int *widx)
{
  int i;
  int j;
  int idx;
  int start_idx;
  worker_class tcls;

  if(!opts || !wcls || !widx || !alabel || !strlen(alabel))
    return -1;

  idx  = -1;
  tcls = CLS_NONE;
  if(is_valid_cls(*wcls)) {
    /*
     * If we were given a non-zero worker index, start at that index.
     */
    if(*widx > 0) {
      start_idx = *widx;
    }
    else {
      start_idx = 0;
    }

    switch(*wcls) {
      case CLS_CPU:
        for(i = start_idx;i < MAX_CPUS;i++) {
          if(!opts->cpu[i].shopts.used) {
            continue;
          }
          for(j = 0;j < opts->cpu[i].shopts.num_afters;j++) {
            if(!strcmp(opts->cpu[i].shopts.after[j], alabel)) {
              idx  = i;
              tcls = CLS_CPU;
              break;
            }
          }
          if(j != opts->cpu[i].shopts.num_afters) {
            break;
          }
        }
        break;

      case CLS_MEM:
        for(i = start_idx;i < MAX_MEMS;i++) {
          if(!opts->mem[i].shopts.used) {
            continue;
          }
          for(j = 0;j < opts->mem[i].shopts.num_afters;j++) {
            if(!strcmp(opts->mem[i].shopts.after[j], alabel)) {
              idx  = i;
              tcls = CLS_MEM;
              break;
            }
          }
          if(j != opts->mem[i].shopts.num_afters) {
            break;
          }
        }
        break;

      case CLS_DISK:
        for(i = start_idx;i < MAX_DIOS;i++) {
          if(!opts->disk_io[i].shopts.used) {
            continue;
          }
          for(j = 0;j < opts->disk_io[i].shopts.num_afters;j++) {
            if(!strcmp(opts->disk_io[i].shopts.after[j], alabel)) {
              idx  = i;
              tcls = CLS_DISK;
              break;
            }
          }
          if(j != opts->disk_io[i].shopts.num_afters) {
            break;
          }
        }
        break;

      case CLS_NET:
        for(i = start_idx;i < MAX_NIOS;i++) {
          if(!opts->net_io[i].shopts.used) {
            continue;
          }
          for(j = 0;j < opts->net_io[i].shopts.num_afters;j++) {
            if(!strcmp(opts->net_io[i].shopts.after[j], alabel)) {
              idx  = i;
              tcls = CLS_NET;
              break;
            }
          }
          if(j != opts->net_io[i].shopts.num_afters) {
            break;
          }
        }
        break;

      default:
        break;
    }
  }
  else if((*wcls) == CLS_ALL) {
    /*
     * Search through all the classes.
     */
    for(i = 0;i < MAX_CPUS;i++) {
      if(!opts->cpu[i].shopts.used) {
        continue;
      }
      for(j = 0;j < opts->cpu[i].shopts.num_afters;j++) {
        if(!strcmp(opts->cpu[i].shopts.after[j], alabel)) {
          idx  = i;
          tcls = CLS_CPU;
          break;
        }
      }
      if(j != opts->cpu[i].shopts.num_afters) {
        break;
      }
    }
    if(i != MAX_CPUS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_MEMS;i++) {
      if(!opts->mem[i].shopts.used) {
        continue;
      }
      for(j = 0;j < opts->mem[i].shopts.num_afters;j++) {
        if(!strcmp(opts->mem[i].shopts.after[j], alabel)) {
          idx  = i;
          tcls = CLS_MEM;
          break;
        }
      }
      if(j != opts->mem[i].shopts.num_afters) {
        break;
      }
    }
    if(i != MAX_MEMS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_DIOS;i++) {
      if(!opts->disk_io[i].shopts.used) {
        continue;
      }
      for(j = 0;j < opts->disk_io[i].shopts.num_afters;j++) {
        if(!strcmp(opts->disk_io[i].shopts.after[j], alabel)) {
          idx  = i;
          tcls = CLS_DISK;
          break;
        }
      }
      if(j != opts->disk_io[i].shopts.num_afters) {
        break;
      }
    }
    if(i != MAX_DIOS) {
      goto clean_out;
    }

    for(i = 0;i < MAX_NIOS;i++) {
      if(!opts->net_io[i].shopts.used) {
        continue;
      }
      for(j = 0;j < opts->net_io[i].shopts.num_afters;j++) {
        if(!strcmp(opts->net_io[i].shopts.after[j], alabel)) {
          idx  = i;
          tcls = CLS_NET;
          break;
        }
      }
      if(j != opts->net_io[i].shopts.num_afters) {
        break;
      }
    }
    if(i != MAX_NIOS) {
      goto clean_out;
    }
  }

clean_out:
  if((tcls == CLS_NONE) || (idx < 0)) {
    *wcls = CLG_ERROR;
    *widx = -1;
    return 0;
  }
  else {
    *wcls = tcls;
    *widx = idx;
    return 1;
  }
}


/*
 * Kill off an existing worker.
 */
int kill_worker(gamut_opts *opts, worker_class wcls, int widx)
{
  int rc;  /* Return code for individual operations */
  int frc; /* Return code for the entire function   */
  shared_opts *shopts;

  if(!opts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  frc    = -1;

  rc = lock_reaper(opts);
  if(rc < 0) {
    goto fail_out;
  }

  shopts = get_shared_opts(opts, wcls, widx);
  if(!shopts) {
    s_log(G_WARNING, "Error getting worker data.\n");
    goto reaper_out;
  }

  /*
   * Trying to kill a worker that doesn't exist is an error.
   *   That's why we unlock_out here, as opposed to a more
   *   benign way of exiting this function.
   */
  if(!shopts->used) {
    goto reaper_out;
  }

  /*
   * Figure out exactly what we need to do.
   *   If the worker hasn't started yet (i.e., isn't running)
   *     then we can just clean it out.
   *   If it is running, then we'll need to tell it to exit
   *     and then wait for it to actually exit.
   */
  rc = lock_worker(opts, wcls, widx);
  if(rc < 0) {
    goto reaper_out;
  }

  /*
   * Tell it to exit.  We don't wait for the actual exit.
   *   That's the job of the reaper.
   *
   * Note: Don't increment the exiting count here, since we'll
   *       leave that up to the individual workers to notice.
   */
  if(shopts->running) {
    shopts->exiting = 1;

    /*
     * Signal the worker, just in case it's waiting on a link.
     */
    rc = signal_worker(opts, wcls, widx);
    if(rc < 0) {
      goto worker_out;
    }
  }
  else {
    /*
     * Essentially deleting a worker's slot.
     */
    clean_worker_opts(opts, wcls, widx, WC_NOKEEPID);
  }

  frc = 0;

worker_out:
  rc = unlock_worker(opts, wcls, widx);
  if(rc < 0) {
    frc = -1;
  }

reaper_out:
  rc = unlock_reaper(opts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

/*
 * Kill all existing workers.
 */
void killall_workers(gamut_opts *opts)
{
  int i;

  if(!opts)
    return;

  for(i = 0;i < MAX_CPUS;i++) {
    (void)kill_worker(opts, CLS_CPU, i);
  }

  for(i = 0;i < MAX_MEMS;i++) {
    (void)kill_worker(opts, CLS_MEM, i);
  }

  for(i = 0;i < MAX_DIOS;i++) {
    (void)kill_worker(opts, CLS_DISK, i);
  }

  for(i = 0;i < MAX_NIOS;i++) {
    (void)kill_worker(opts, CLS_NET, i);
  }
}

/*
 * When each worker starts up, it registers itself with the master.
 *   Once all workers have started up, the master sets all
 *   workers off and running.
 */
int worker_register(gamut_opts *opts, worker_class wcls)
{
  int rc;
  int widx;
  pthread_t me;
  shared_opts *shopts;

  if(!opts || !is_valid_cls(wcls))
    return -1;

  widx = -1;

  /*
   * Lock and unlock the start lock so we know
   *   that our thread ID has been filled in.
   */
  rc = sync_start(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_stats(opts);
  if(rc < 0) {
    return -1;
  }

  rc = lock_class(opts, wcls);
  if(rc < 0) {
    goto stats_out;
  }

  /*
   * Find ourselves, and make sure we can be off and running.
   */
  me = pthread_self();
  {
    worker_class acls;

    acls = wcls;
    rc   = find_worker_by_tid(opts, &acls, me, &widx);
    if((rc <= 0) || (acls != wcls)) {
      goto class_out;
    }
  }

  shopts = get_shared_opts(opts, wcls, widx);
  if(!shopts) {
    goto class_out;
  }

  rc = lock_worker(opts, wcls, widx);
  if(rc < 0) {
    widx = -1;
    goto class_out;
  }

  /*
   * Update our status and statistics.
   */
  if(shopts->pending) {
    opts->wstats.workers_pending--;
    shopts->pending = 0;
  }

  opts->wstats.workers_running++;
  shopts->running = 1;

  rc = unlock_worker(opts, wcls, widx);
  if(rc < 0) {
    widx = -1;
    goto class_out;
  }

class_out:
  rc = unlock_class(opts, wcls);
  if(rc < 0) {
    widx = -1;
    goto stats_out;
  }

stats_out:
  rc = unlock_stats(opts);
  if(rc < 0) {
    widx = -1;
  }
  else {
    s_log(G_DEBUG, "Worker registered with master.\n");
  }

fail_out:
  return widx;
}

/*
 * Un-register a worker so we the master can prepare the slot
 *   for its next occupant.
 */
void worker_unregister(gamut_opts *opts, worker_class wcls)
{
  int i;
  int rc;
  int aidx[MAX_AFTERS];  /* Index of workers coming after us */
  int widx;  /* Our index */
  int num_after;
  pthread_t     me;
  shared_opts  *shopts;  /* our shared opts */
  worker_class  acls[MAX_AFTERS]; /* class of worker coming after us */
  worker_order  worder;  /* list of workers we'll need to lock */

  if(!opts || !is_valid_cls(wcls))
    return;

  memset(aidx, 0, sizeof(aidx));
  memset(acls, 0, sizeof(acls));
  num_after  = 0;
  shopts     = NULL;

  rc = init_worker_order(&worder);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_master(opts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_after(opts);
  if(rc < 0) {
    goto master_out;
  }

  rc = lock_reaper(opts);
  if(rc < 0) {
    goto after_out;
  }

  rc = lock_waiting(opts);
  if(rc < 0) {
    goto reaper_out;
  }

  rc = lock_stats(opts);
  if(rc < 0) {
    goto waiting_out;
  }

  rc = lock_link(opts);
  if(rc < 0) {
    goto stats_out;
  }

  rc = lock_class(opts, wcls);
  if(rc < 0) {
    goto link_out;
  }

  /*
   * Find ourselves, and make sure we can head off into the sunset.
   */
  {
    worker_class tcls;

    me   = pthread_self();
    tcls = wcls;
    rc   = find_worker_by_tid(opts, &tcls, me, &widx);
    if((rc <= 0) || (tcls != wcls) || (widx < 0)) {
      s_log(G_WARNING, "Could not find ourselves to unregister.\n");
      goto class_out;
    }
  }

  shopts = get_shared_opts(opts, wcls, widx);
  if(!shopts) {
    s_log(G_WARNING, "Could not find ourselves to unregister.\n");
    goto class_out;
  }

  /*
   * Do one pass through the workers and find everyone we'll need
   *   to lock up in order to succeed.  Append them to the list of
   *   workers, then lock the whole lot of them.
   */
  rc = append_worker(&worder, wcls, widx);
  if(rc < 0) {
    goto class_out;
  }

  /*
   * Were we leading any workers?
   */
  if(shopts->leading) {
    worker_class lcls; /* Leading class */

    for(lcls = 0;lcls < CLS_LAST;lcls++) {
      int tidx;
      worker_class tcls;  /* Temporary class */

      tidx = 0;
      tcls = lcls;
      while(1) {
        rc = find_after_by_label(opts, &tcls, shopts->label, &tidx);
        if(rc < 0) {
          s_log(G_WARNING, "Error finding worker by 'after' label.\n");
          break;
        }
        else if(!rc) {
          s_log(G_DEBUG, "End of 'after' workers for class %d.\n", lcls);
          break;
        }
        else if((tcls != lcls) || (tidx < 0)) {
          s_log(G_WARNING, "Didn't find anything, but returned OK?\n");
          break;
        }
        else {
          shared_opts *ashopts;

          /*
           * We found someone.
           */
          s_log(G_DEBUG, "RC %d CLS %d IDX %d\n", rc, tcls, tidx);

          ashopts = get_shared_opts(opts, tcls, tidx);
          if(!ashopts) {
            s_log(G_WARNING, "Could not get shared opts of follower.\n");
            continue;
          }
          else {
            /*
             * We found the a following after us.  Throw them
             *   on the list of workers to lock.
             */
            rc = append_worker(&worder, tcls, tidx);
            if(rc < 0) {
              goto class_out;
            }

            acls[num_after] = tcls;
            aidx[num_after] = tidx;
            num_after++;

            /*
             * Increment tidx so we don't find the same worker
             *   multiple times.
             */
            tidx++;

            s_log(G_DEBUG, "Worker %s is ready to free %s.\n",
                           shopts->label, ashopts->label);
          }
        }
      } /* End of while(1) */
    } /* End of for() */
  }

  /*
   * OK, lock everyone up according to the locking order.
   */
  rc = lock_worker_order(opts, &worder);
  if(rc < 0) {
    goto class_out;
  }

  /*
   * We have various things we need to do before we exit.
   *   We need to update the number of workers that are running.
   *   If the master is waiting on us, decrement that counter.
   *   See if any workers are waiting for us to finish.
   *   Were we linked to anyone else?
   *   Finally, notify the reaper that we're ready to exit stage left.
   */
  if(shopts->running) {
    opts->wstats.workers_running--;
    shopts->running = 0;
  }
  else {
    s_log(G_WARNING, "A unregistering worker wasn't running?\n");
  }

  /*
   * Were we leading any workers?
   */
  if(shopts->leading && num_after) {
    int num_released;
    int waiting_found;
    worker_sync *a_sync;

    a_sync = &opts->a_sync;

    num_released  = 0;
    waiting_found = 0;
    for(i = 0;i < num_after;i++) {
      int j;
      int mslots;
      shared_opts *ashopts;

      ashopts = get_shared_opts(opts, acls[i], aidx[i]);
      if(!ashopts) {
        s_log(G_WARNING, "Could not re-find worker (%d, %d).\n",
                         acls[i], aidx[i]);
        continue;
      }

      /*
       * We are leading this worker, but we might not be the
       *   only one.  Remove ourselves from the list.
       */
      for(j = 0;j < ashopts->num_afters;j++) {
        if(!strcmp(ashopts->after[j], shopts->label)) {
          break;
        }
      }
      if(j == ashopts->num_afters) {
        s_log(G_WARNING, "Found worker, but we are no longer on "
                         "its 'after' list (?).\n");
        continue;
      }

      /*
       * Remove ourselves from the 'after' list.
       */
      mslots = ashopts->num_afters - 1 - j;
      if(mslots) {
        memmove(&ashopts->after[j], &ashopts->after[j+1],
                mslots * SMBUFSIZE);
      }

      /*
       * If we were the last one this worker was waiting on,
       *   prepare to poke the reaper.
       */
      ashopts->num_afters--;
      if(!ashopts->num_afters) {
        ashopts->waiting = 0;
        opts->wstats.workers_waiting--;

        a_sync->wdata[opts->a_sync.wqueue_size].wcls         = acls[i];
        a_sync->wdata[opts->a_sync.wqueue_size].worker_index = aidx[i];
        a_sync->wqueue_size++;
        num_released++;
      }
      waiting_found++;
    }

    if(waiting_found != num_after) {
      s_log(G_WARNING, "Found %d following us earlier, only %d now.\n",
                       num_after, waiting_found);
    }
    num_after = num_released;
    opts->wstats.workers_leading--;
  }

  /*
   * Were we linked to anyone else?
   */
  if(shopts->linked) {
    int lidx;

    rc = find_link_by_worker(opts, shopts->wcls, shopts->widx, &lidx);
    if(rc <= 0) {
      s_log(G_WARNING, "We were linked but couldn't find ourselves.\n");
    }
    else {
      int j;
      int num_linked;
      worker_link *wlink;

      wlink      = &opts->wlinks.wlink[lidx];
      num_linked = wlink->num_linked;
      for(j = 0;j < num_linked;j++) {
        if((wlink->wdata[j].worker_index == shopts->widx)
           &&
           (wlink->wdata[j].wcls == shopts->wcls)
          )
        {
          /*
           * We found ourselves.  Remove ourselves.
           *   mslots are the number of slots we'll need to move
           *   over to fill our spot.
           */
          int mslots;

          mslots = wlink->num_linked - 1 - j;
          if(mslots) {
            memmove(&wlink->wdata[j], &wlink->wdata[j+1],
                    mslots * sizeof(worker_data));
          }
          wlink->num_linked--;

          /*
           * If we were the last link here, destroy the link itself.
           */
          if(!wlink->num_linked) {
            memset(wlink, 0, sizeof(worker_link));
          }

          break;
        } /* end if(us) */
      }
      if(j != num_linked) {
        s_log(G_DEBUG, "Removed ourselves from our link.\n");
        opts->wstats.workers_linked--;
      }
    }
  }

  if(shopts->mwait) {
    opts->wcounter.count--;

    /*
     * We're the last worker on which the master was waiting.
     *   Signal that it's done.
     */
    if(opts->wcounter.count == 0) {
      (void)signal_waiting(opts);
    }
  }

  /*
   * Poke the reaper to let it know we're ready to go.
   */
  {
    int ridx;

    ridx = opts->r_sync.wqueue_size;

    opts->r_sync.wdata[ridx].wcls         = wcls;
    opts->r_sync.wdata[ridx].worker_index = widx;
    opts->r_sync.wqueue_size++;
  }

  rc = signal_reaper(opts);
  if(rc < 0) {
    s_log(G_WARNING, "Unregister could not signal reaper.\n");
  }
  else {
    s_log(G_DEBUG, "Unregister worker %u (%s) signalled reaper.\n",
                   shopts->wid, shopts->label);
  }

  (void)unlock_worker_order(opts, &worder);

class_out:
  (void)unlock_class(opts, wcls);

link_out:
  (void)unlock_link(opts);

stats_out:
  (void)unlock_stats(opts);

waiting_out:
  (void)unlock_waiting(opts);

reaper_out:
  (void)unlock_reaper(opts);

after_out:
  (void)unlock_after(opts);

  /*
   * We send the 'after' command here to prevent any deadlocks.
   */
  if(num_after) {
    rc = send_master_cmd(opts, MCMD_AFTER, NULL);
    if(rc < 0) {
      s_log(G_WARNING, "Error sending master an 'after' command.\n");
    }
  }

master_out:
  (void)unlock_master(opts);

fail_out:
  return;
}


/*
 * Conditions for starting a worker:
 * 1. Slot must be in use (i.e., have parameters)
 * 2. Worker must be pending, but not waiting for anything.
 * 3. Worker must not be running already.
 */
static int can_start_worker(shared_opts *shopts)
{
  if(!shopts)
    return -1;

  if(!shopts->used)
    return 0;

  if(!shopts->pending || shopts->waiting)
    return 0;

  if(shopts->running)
    return 0;

  return 1;
}

static int sync_start(gamut_opts *opts)
{
  int frc;
  int rc;

  frc = -1;

  /*
   * Lock and unlock the start lock so we know
   *   that our thread ID has been filled in.
   */
  rc = lock_start(opts);
  if(rc < 0) {
    goto fail_out;
  }
  rc = unlock_start(opts);
  if(rc < 0) {
    goto fail_out;
  }

  frc = 0;

fail_out:
  return frc;
}

static int find_open_slot(gamut_opts *opts, worker_class wcls)
{
  int i;
  int idx;

  if(!opts || !is_valid_cls(wcls))
    return -1;

  idx = -1;
  switch(wcls) {
    case CLS_CPU:
      for(i = 0;i < MAX_CPUS;i++) {
        if(!opts->cpu[i].shopts.used)
          break;
      }
      if(i != MAX_CPUS) {
        idx = i;
      }
      break;

    case CLS_MEM:
      for(i = 0;i < MAX_MEMS;i++) {
        if(!opts->mem[i].shopts.used)
          break;
      }
      if(i != MAX_MEMS) {
        idx = i;
      }
      break;

    case CLS_DISK:
      for(i = 0;i < MAX_DIOS;i++) {
        if(!opts->disk_io[i].shopts.used)
          break;
      }
      if(i != MAX_DIOS) {
        idx = i;
      }
      break;

    case CLS_NET:
      for(i = 0;i < MAX_NIOS;i++) {
        if(!opts->net_io[i].shopts.used)
          break;
      }
      if(i != MAX_NIOS) {
        idx = i;
      }
      break;

    default:
      s_log(G_WARNING, "Unknown worker class: %d.\n", wcls);
      break;
  }

  /*
   * If we found a valid slot, this will be it.
   *   Otherwise it will still be -1.
   */
  return idx;
}
