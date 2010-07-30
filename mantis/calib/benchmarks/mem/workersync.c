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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "opts.h"
#include "utilio.h"
#include "utillog.h"
#include "utilnet.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

/*
 * Perform the bookkeeping for this synchronization operation. 
 */
static void bookkeep_sync(gamut_opts *gopts, uint32_t lockID,
                          uint8_t op);

/*
 * Compare two workers by their locking order.
 */
static int worker_compare(const void *w_a, const void *w_b);

/*
 * Lock/unlock the master lock, wait for a signal,
 *   or signal the master.
 */
int lock_master(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, MASTER_LOCK_IDX, L_ADD);

  rc = pthread_mutex_lock(&gopts->mctl.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not get master lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Got master lock.\n");
    return 0;
  }
}

int unlock_master(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, MASTER_LOCK_IDX, L_DEL);

  rc = pthread_mutex_unlock(&gopts->mctl.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not release master lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Released master lock.\n");
    return 0;
  }
}

int wait_master(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  s_log(G_DSYNC, "Waiting on master cond (releasing master lock).\n");
  bookkeep_sync(gopts, MASTER_LOCK_IDX, L_DEL);
  rc = pthread_cond_wait(&gopts->mctl.t_sync.cond,
                         &gopts->mctl.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not wait on master cond.\n");
    return -1;
  }
  else {
    bookkeep_sync(gopts, MASTER_LOCK_IDX, L_ADD);
    s_log(G_DSYNC, "Finished waiting on master cond (got master lock).\n");
    return 0;
  }
}

int signal_master(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_cond_signal(&gopts->mctl.t_sync.cond);
  if(rc < 0) {
    s_log(G_WARNING, "Could not signal master cond.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Signalled master cond.\n");
    return 0;
  }
}

int broadcast_master(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_cond_broadcast(&gopts->mctl.t_sync.cond);
  if(rc < 0) {
    s_log(G_WARNING, "Could not broadcast on master cond.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Broadcast on master cond.\n");
    return 0;
  }
}

/*
 * Lock/unlock the 'after' lock, wait for a signal,
 *   or signal the 'after' worker.
 */
int lock_after(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, AFTER_LOCK_IDX, L_ADD);

  rc = pthread_mutex_lock(&gopts->a_sync.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not get 'after' lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Got 'after' lock.\n");
    return 0;
  }
}

int unlock_after(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, AFTER_LOCK_IDX, L_DEL);

  rc = pthread_mutex_unlock(&gopts->a_sync.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not release 'after' lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Released 'after' lock.\n");
    return 0;
  }
}

int wait_after(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  s_log(G_DSYNC, "Waiting on 'after' cond (releasing 'after' lock).\n");
  bookkeep_sync(gopts, AFTER_LOCK_IDX, L_DEL);
  rc = pthread_cond_wait(&gopts->a_sync.t_sync.cond,
                         &gopts->a_sync.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not wait on 'after' cond.\n");
    return -1;
  }
  else {
    bookkeep_sync(gopts, AFTER_LOCK_IDX, L_ADD);
    s_log(G_DSYNC, "Finished waiting on 'after' cond (got 'after' lock).\n");
    return 0;
  }
}

int signal_after(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_cond_signal(&gopts->a_sync.t_sync.cond);
  if(rc < 0) {
    s_log(G_WARNING, "Could not signal 'after' cond.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Signalled 'after' cond.\n");
    return 0;
  }
}

/*
 * Lock/unlock the reaper lock, wait for a signal,
 *   or signal the reaper.
 */
int lock_reaper(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, REAPER_LOCK_IDX, L_ADD);

  rc = pthread_mutex_lock(&gopts->r_sync.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not get reaper lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Got reaper lock.\n");
    return 0;
  }
}

int unlock_reaper(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, REAPER_LOCK_IDX, L_DEL);

  rc = pthread_mutex_unlock(&gopts->r_sync.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not release reaper lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Released reaper lock.\n");
    return 0;
  }
}

int wait_reaper(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  s_log(G_DSYNC, "Waiting on reaper cond (releasing reaper lock).\n");
  bookkeep_sync(gopts, REAPER_LOCK_IDX, L_DEL);
  rc = pthread_cond_wait(&gopts->r_sync.t_sync.cond,
                         &gopts->r_sync.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not wait on reaper cond.\n");
    return -1;
  }
  else {
    bookkeep_sync(gopts, REAPER_LOCK_IDX, L_ADD);
    s_log(G_DSYNC, "Finished waiting on reaper cond (got reaper lock).\n");
    return 0;
  }
}

int signal_reaper(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_cond_signal(&gopts->r_sync.t_sync.cond);
  if(rc < 0) {
    s_log(G_WARNING, "Could not signal reaper cond.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Signalled reaper cond.\n");
    return 0;
  }
}

/*
 * Lock/unlock the waiting lock, wait for a signal,
 *   or signal the waiting worker.
 */
int lock_waiting(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, WAITING_LOCK_IDX, L_ADD);
  rc = pthread_mutex_lock(&gopts->wcounter.c_lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not get waiting lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Got waiting lock.\n");
    return 0;
  }
}

int unlock_waiting(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_mutex_unlock(&gopts->wcounter.c_lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not release waiting lock.\n");
    return -1;
  }
  else {
    bookkeep_sync(gopts, WAITING_LOCK_IDX, L_DEL);
    s_log(G_DSYNC, "Released waiting lock.\n");
    return 0;
  }
}

int wait_waiting(gamut_opts *gopts, uint64_t wait_usec)
{
  int rc;

  if(!gopts)
    return -1;

  s_log(G_DSYNC, "Wait on 'wait' cond (releasing waiting lock).\n");

  bookkeep_sync(gopts, WAITING_LOCK_IDX, L_DEL);

  rc = 0;
  if(wait_usec) {
    uint64_t deadline;
    struct timeval now;
    struct timespec timeout;

    (void)gettimeofday(&now, NULL);
    deadline  = now.tv_sec * US_SEC;
    deadline += now.tv_usec;
    deadline += wait_usec;

    timeout.tv_sec  = deadline / US_SEC;
    timeout.tv_nsec = (deadline % US_SEC) * 1000;

    rc = pthread_cond_timedwait(&gopts->wcounter.c_cond,
                                &gopts->wcounter.c_lock, &timeout);
  }
  else {
    rc = pthread_cond_wait(&gopts->wcounter.c_cond,
                           &gopts->wcounter.c_lock);
  }
  if(rc == ETIMEDOUT) {
    bookkeep_sync(gopts, WAITING_LOCK_IDX, L_ADD);
    s_log(G_DSYNC, "Wait on waiting timed out after %llu usecs.\n",
                   wait_usec);
    return ETIMEDOUT;
  }
  else if(rc < 0) {
    s_log(G_WARNING, "Could not wait on waiting cond.\n");
    return -1;
  }
  else {
    bookkeep_sync(gopts, WAITING_LOCK_IDX, L_ADD);
    s_log(G_DSYNC, "Finished waiting on waiting cond (got waiting lock).\n");
    return 0;
  }
}

int signal_waiting(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_cond_signal(&gopts->wcounter.c_cond);
  if(rc < 0) {
    s_log(G_WARNING, "Could not signal waiting cond.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Signalled waiting cond.\n");
    return 0;
  }
}

/*
 * Lock/unlock the statistics for modifications.
 */
int lock_stats(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, STATS_LOCK_IDX, L_ADD);
  rc = pthread_mutex_lock(&gopts->wstats.stats_lock);
  if(rc < 0) {
    s_log(G_WARNING, "Error getting statistics lock.\n");
    rc = -1;
  }
  else {
    s_log(G_DSYNC, "Got statistics lock.\n");
    rc = 0;
  }

  return rc;
}

int unlock_stats(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_mutex_unlock(&gopts->wstats.stats_lock);
  if(rc < 0) {
    s_log(G_WARNING, "Error releasing statistics lock.\n");
    rc = -1;
  }
  else {
    bookkeep_sync(gopts, STATS_LOCK_IDX, L_DEL);
    s_log(G_DSYNC, "Released statistics lock.\n");
    rc = 0;
  }

  return rc;
}

/*
 * Lock/unlock the input lock.
 */
int lock_input(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, INPUT_LOCK_IDX, L_ADD);
  rc = pthread_mutex_lock(&gopts->i_sync.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not get input lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Got input lock.\n");
    return 0;
  }
}

int unlock_input(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_mutex_unlock(&gopts->i_sync.t_sync.lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not release input lock.\n");
    return -1;
  }
  else {
    bookkeep_sync(gopts, INPUT_LOCK_IDX, L_DEL);
    s_log(G_DSYNC, "Released input lock.\n");
    return 0;
  }
}

/*
 * Lock/unlock the worker link lock.
 */
int lock_link(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  bookkeep_sync(gopts, LINK_LOCK_IDX, L_ADD);
  rc = pthread_mutex_lock(&gopts->wlinks.link_lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not get link lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Got link lock.\n");
    return 0;
  }
}

int unlock_link(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_mutex_unlock(&gopts->wlinks.link_lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not release link lock.\n");
    return -1;
  }
  else {
    bookkeep_sync(gopts, LINK_LOCK_IDX, L_DEL);
    s_log(G_DSYNC, "Released link lock.\n");
    return 0;
  }
}

/*
 * Lock/unlock the worker start lock.
 */
int lock_start(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_mutex_lock(&gopts->mctl.start_lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not get master start lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Got master start lock.\n");
    return 0;
  }
}

int unlock_start(gamut_opts *gopts)
{
  int rc;

  if(!gopts)
    return -1;

  rc = pthread_mutex_unlock(&gopts->mctl.start_lock);
  if(rc < 0) {
    s_log(G_WARNING, "Could not release master start lock.\n");
    return -1;
  }
  else {
    s_log(G_DSYNC, "Released master start lock.\n");
    return 0;
  }
}

/*
 * Lock/unlock a worker class in preparation for modifications.
 */
int lock_class(gamut_opts *gopts, worker_class wcls)
{
  int rc;
  
  if(!gopts || !is_valid_cls(wcls))
    return -1;

  switch(wcls) {
    case CLS_CPU:
      bookkeep_sync(gopts, CPU_CLASS_LOCK_IDX, L_ADD);
      rc = pthread_mutex_lock(&gopts->cpu_lock);
      if(rc < 0) {
        s_log(G_WARNING, "Error getting CPU class lock.\n");
        rc = -1;
      }
      else {
        s_log(G_DSYNC, "Got CPU class lock.\n");
        rc = 0;
      }
      break;

    case CLS_MEM:
      bookkeep_sync(gopts, MEM_CLASS_LOCK_IDX, L_ADD);
      rc = pthread_mutex_lock(&gopts->mem_lock);
      if(rc < 0) {
        s_log(G_WARNING, "Error getting memory class lock.\n");
        rc = -1;
      }
      else {
        s_log(G_DSYNC, "Got memory class lock.\n");
        rc = 0;
      }
      break;

    case CLS_DISK:
      bookkeep_sync(gopts, DIO_CLASS_LOCK_IDX, L_ADD);
      rc = pthread_mutex_lock(&gopts->dio_lock);
      if(rc < 0) {
        s_log(G_WARNING, "Error getting disk class lock.\n");
        rc = -1;
      }
      else {
        s_log(G_DSYNC, "Got disk class lock.\n");
        rc = 0;
      }
      break;

    case CLS_NET:
      bookkeep_sync(gopts, NIO_CLASS_LOCK_IDX, L_ADD);
      rc = pthread_mutex_lock(&gopts->nio_lock);
      if(rc < 0) {
        s_log(G_WARNING, "Error getting network class lock.\n");
        rc = -1;
      }
      else {
        s_log(G_DSYNC, "Got network class lock.\n");
        rc = 0;
      }
      break;

    default:
      s_log(G_WARNING, "Unknown class for class lock: %d.\n", wcls);
      rc = -1;
  }

  return rc;
}

int unlock_class(gamut_opts *gopts, worker_class wcls)
{
  int rc;
  
  if(!gopts || !is_valid_cls(wcls))
    return -1;

  switch(wcls) {
    case CLS_CPU:
      bookkeep_sync(gopts, CPU_CLASS_LOCK_IDX, L_DEL);
      rc = pthread_mutex_unlock(&gopts->cpu_lock);
      if(rc < 0) {
        s_log(G_WARNING, "Error releasing CPU class lock.\n");
        rc = -1;
      }
      else {
        s_log(G_DSYNC, "Released CPU class lock.\n");
        rc = 0;
      }
      break;

    case CLS_MEM:
      bookkeep_sync(gopts, MEM_CLASS_LOCK_IDX, L_DEL);
      rc = pthread_mutex_unlock(&gopts->mem_lock);
      if(rc < 0) {
        s_log(G_WARNING, "Error releasing memory class lock.\n");
        rc = -1;
      }
      else {
        s_log(G_DSYNC, "Released memory class lock.\n");
        rc = 0;
      }
      break;

    case CLS_DISK:
      bookkeep_sync(gopts, DIO_CLASS_LOCK_IDX, L_DEL);
      rc = pthread_mutex_unlock(&gopts->dio_lock);
      if(rc < 0) {
        s_log(G_WARNING, "Error releasing disk class lock.\n");
        rc = -1;
      }
      else {
        s_log(G_DSYNC, "Released disk class lock.\n");
        rc = 0;
      }
      break;

    case CLS_NET:
      bookkeep_sync(gopts, NIO_CLASS_LOCK_IDX, L_DEL);
      rc = pthread_mutex_unlock(&gopts->nio_lock);
      if(rc < 0) {
        s_log(G_WARNING, "Error releasing network class lock.\n");
        rc = -1;
      }
      else {
        s_log(G_DSYNC, "Released network class lock.\n");
        rc = 0;
      }
      break;

    default:
      s_log(G_WARNING, "Unknown class for class lock: %d.\n", wcls);
      rc = -1;
  }

  return rc;
}

/*
 * Lock/unlock a worker struct in preparation for modifications.
 */
int lock_worker(gamut_opts *gopts, worker_class wcls, int widx)
{
  int rc;
  
  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS) {
        s_log(G_WARNING, "Invalid index for CPU worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (CPU_BASE_LOCK_IDX + widx), L_ADD);
        rc = pthread_mutex_lock(&gopts->cpu[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error getting CPU worker %d lock.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Got CPU worker %d lock.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS) {
        s_log(G_WARNING, "Invalid index for memory worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (MEM_BASE_LOCK_IDX + widx), L_ADD);
        rc = pthread_mutex_lock(&gopts->mem[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error getting memory worker %d lock.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Got memory worker %d lock.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS) {
        s_log(G_WARNING, "Invalid index for disk worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (DIO_BASE_LOCK_IDX + widx), L_ADD);
        rc = pthread_mutex_lock(&gopts->disk_io[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error getting disk worker %d lock.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Got disk worker %d lock.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS) {
        s_log(G_WARNING, "Invalid index for net worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (NIO_BASE_LOCK_IDX + widx), L_ADD);
        rc = pthread_mutex_lock(&gopts->net_io[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error getting network worker %d lock.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Got net worker %d lock.\n", widx);
          rc = 0;
        }
      }
      break;

    default:
      s_log(G_WARNING, "Unknown class for worker lock: %d.\n", wcls);
      rc = -1;
  }

  return rc;
}

int unlock_worker(gamut_opts *gopts, worker_class wcls, int widx)
{
  int rc;

  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS) {
        s_log(G_WARNING, "Invalid index for CPU worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (CPU_BASE_LOCK_IDX + widx), L_DEL);
        rc = pthread_mutex_unlock(&gopts->cpu[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error releasing CPU worker %d lock.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Released CPU worker %d lock.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS) {
        s_log(G_WARNING, "Invalid index for memory worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (MEM_BASE_LOCK_IDX + widx), L_DEL);
        rc = pthread_mutex_unlock(&gopts->mem[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error releasing memory worker %d lock.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Released memory worker %d lock.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS) {
        s_log(G_WARNING, "Invalid index for disk worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (DIO_BASE_LOCK_IDX + widx), L_DEL);
        rc = pthread_mutex_unlock(&gopts->disk_io[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error releasing disk worker %d lock.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Released disk worker %d lock.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS) {
        s_log(G_WARNING, "Invalid index for net worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (NIO_BASE_LOCK_IDX + widx), L_DEL);
        rc = pthread_mutex_unlock(&gopts->net_io[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error releasing network worker %d lock.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Released net worker %d lock.\n", widx);
          rc = 0;
        }
      }
      break;

    default:
      s_log(G_WARNING, "Unknown class for worker lock: %d.\n", wcls);
      rc = -1;
  }

  return rc;
}

int wait_worker(gamut_opts *gopts, worker_class wcls, int widx)
{
  int rc;
  
  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS) {
        s_log(G_WARNING, "Invalid index for CPU worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (CPU_BASE_LOCK_IDX + widx), L_DEL);
        rc = pthread_cond_wait(&gopts->cpu[widx].shopts.t_sync.cond,
                               &gopts->cpu[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error waiting on CPU worker %d.\n", widx);
          rc = -1;
        }
        else {
          bookkeep_sync(gopts, (CPU_BASE_LOCK_IDX + widx), L_ADD);
          s_log(G_DSYNC, "Finished waiting on CPU worker %d.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS) {
        s_log(G_WARNING, "Invalid index for memory worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (MEM_BASE_LOCK_IDX + widx), L_DEL);
        rc = pthread_cond_wait(&gopts->mem[widx].shopts.t_sync.cond,
                               &gopts->mem[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error waiting on memory worker %d.\n", widx);
          rc = -1;
        }
        else {
          bookkeep_sync(gopts, (MEM_BASE_LOCK_IDX + widx), L_ADD);
          s_log(G_DSYNC, "Finished waiting on memory worker %d.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS) {
        s_log(G_WARNING, "Invalid index for disk worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (DIO_BASE_LOCK_IDX + widx), L_DEL);
        rc = pthread_cond_wait(&gopts->disk_io[widx].shopts.t_sync.cond,
                               &gopts->disk_io[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error waiting on disk worker %d.\n", widx);
          rc = -1;
        }
        else {
          bookkeep_sync(gopts, (DIO_BASE_LOCK_IDX + widx), L_ADD);
          s_log(G_DSYNC, "Finished waiting on disk worker %d.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS) {
        s_log(G_WARNING, "Invalid index for net worker: %d.\n", widx);
        rc = -1;
      }
      else {
        bookkeep_sync(gopts, (NIO_BASE_LOCK_IDX + widx), L_DEL);
        rc = pthread_cond_wait(&gopts->net_io[widx].shopts.t_sync.cond,
                               &gopts->net_io[widx].shopts.t_sync.lock);
        if(rc < 0) {
          s_log(G_WARNING, "Error waiting on net worker %d.\n", widx);
          rc = -1;
        }
        else {
          bookkeep_sync(gopts, (NIO_BASE_LOCK_IDX + widx), L_ADD);
          s_log(G_DSYNC, "Finished waiting on net worker %d.\n", widx);
          rc = 0;
        }
      }
      break;

    default:
      s_log(G_WARNING, "Unknown class for worker wait: %d.\n", wcls);
      rc = -1;
  }

  return rc;
}

int signal_worker(gamut_opts *gopts, worker_class wcls, int widx)
{
  int rc;

  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS) {
        s_log(G_WARNING, "Invalid index for CPU worker: %d.\n", widx);
        rc = -1;
      }
      else {
        rc = pthread_cond_signal(&gopts->cpu[widx].shopts.t_sync.cond);
        if(rc < 0) {
          s_log(G_WARNING, "Could not signal CPU worker %d.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Signalled CPU worker %d.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS) {
        s_log(G_WARNING, "Invalid index for memory worker: %d.\n", widx);
        rc = -1;
      }
      else {
        rc = pthread_cond_signal(&gopts->mem[widx].shopts.t_sync.cond);
        if(rc < 0) {
          s_log(G_WARNING, "Could not signal memory worker %d.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Signalled memory worker %d.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS) {
        s_log(G_WARNING, "Invalid index for disk worker: %d.\n", widx);
        rc = -1;
      }
      else {
        rc = pthread_cond_signal(&gopts->disk_io[widx].shopts.t_sync.cond);
        if(rc < 0) {
          s_log(G_WARNING, "Could not signal disk worker %d.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Signalled disk worker %d.\n", widx);
          rc = 0;
        }
      }
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS) {
        s_log(G_WARNING, "Invalid index for net worker: %d.\n", widx);
        rc = -1;
      }
      else {
        rc = pthread_cond_signal(&gopts->net_io[widx].shopts.t_sync.cond);
        if(rc < 0) {
          s_log(G_WARNING, "Could not signal net worker %d.\n", widx);
          rc = -1;
        }
        else {
          s_log(G_DSYNC, "Signalled net worker %d.\n", widx);
          rc = 0;
        }
      }
      break;

    default:
      s_log(G_WARNING, "Unknown class for worker signal: %d.\n", wcls);
      rc = -1;
  }

  return rc;
}


/*
 * Since a worker might have to lock other workers -- i.e., during
 *   worker exit, linked operations, etc -- we'll use this to make
 *   ordered locking easier.
 */
int init_worker_order(worker_order *worder)
{
  if(!worder)
    return -1;

  memset(worder, 0, sizeof(worker_order));
  return 0;
}

int append_worker(worker_order *worder, worker_class wcls,
                  uint32_t widx)
{
  if(!worder)
    return -1;

  if(worder->num_locks == MAX_LOCK_IDX) {
    return 0;
  }

  worder->wdata[worder->num_locks].wcls         = wcls;
  worder->wdata[worder->num_locks].worker_index = widx;
  worder->num_locks++;

  qsort(worder->wdata, worder->num_locks, sizeof(worker_data),
        worker_compare);

  return 1;
}

int lock_worker_order(gamut_opts *gopts, worker_order *worder)
{
  int32_t i;

  if(!gopts || !worder)
    return -1;

  for(i = 0;i < worder->num_locks;i++) {
    int rc;

    rc = lock_worker(gopts, worder->wdata[i].wcls,
                     worder->wdata[i].worker_index);
    if(rc < 0) {
      s_log(G_WARNING, "lock_worder_order: error at %d (%d, %d).\n",
                       i, worder->wdata[i].wcls,
                       worder->wdata[i].worker_index);
      break;
    }
  }

  /*
   * If we didn't get all the way through, that means we should
   *   unlock and bail back out.  We decrement 'i' since that
   *   takes us to the last successful lock_worker call.
   *
   * Otherwise, return success.
   */
  if(i != worder->num_locks) {
    for(i = i - 1;i >= 0;i--) {
      int rc;

      rc = unlock_worker(gopts, worder->wdata[i].wcls,
                         worder->wdata[i].worker_index);
      if(rc < 0) {
        s_log(G_WARNING, "Error bailing out of lock_worker_order.\n");
      }
    }

    return -1;
  }
  else {
    return 0;
  }
}

int unlock_worker_order(gamut_opts *gopts, worker_order *worder)
{
  int frc;
  int32_t i;

  if(!gopts || !worder)
    return -1;

  frc = 0;

  /*
   * Unlock in reverse order.
   */
  for(i = worder->num_locks - 1;i >= 0;i--) {
    int rc;

    rc = unlock_worker(gopts, worder->wdata[i].wcls,
                       worder->wdata[i].worker_index);
    if(rc < 0) {
      s_log(G_WARNING, "unlock_worder_order: error at %d (%d, %d).\n",
                       i, worder->wdata[i].wcls,
                       worder->wdata[i].worker_index);
      frc = -1;
    }
  }

  return frc;
}

/*
 * Dump lock information to a buffer.
 */
int get_lock_info(char *buf, uint32_t bufsize, thread_sync *t_sync)
{
  int i;
  int rc;
  int pos;

  if(!buf || !bufsize || !t_sync)
    return -1;

  pos = 0;
  memset(buf, 0, bufsize);
  for(i = 0;i < t_sync->curr_lock;i++) {
    rc = snprintf(&buf[pos], bufsize - pos, "%u ",
                  t_sync->lock_order[i]);
    if(rc > (bufsize - pos)) {
      s_log(G_WARNING, "Overflowed get_lock_info buffer.\n");
      goto clean_out;
    }
    else {
      pos += rc;
    }
  }

  buf[pos] = '\0';

  return 0;

clean_out:
  memset(buf, 0, bufsize);

  return -1;
}

/*
 * Perform the bookkeeping for this synchronization operation. 
 */
static void bookkeep_sync(gamut_opts *gopts, uint32_t lockID,
                          uint8_t op)
{
  int          rc;
  int          widx;
  pthread_t    me;
  thread_sync  *t_sync;
  worker_class wcls;

  if(!debug_sync) {
    return;
  }

  me     = pthread_self();
  t_sync = NULL;

  widx   = -1;
  wcls   = CLS_ALL;
  rc = find_worker_by_tid(gopts, &wcls, pthread_self(), &widx);
  if(rc > 0) {
    shared_opts *shopts;

    shopts = get_shared_opts(gopts, wcls, widx);
    if(!shopts) {
      s_log(G_WARNING, "Found worker (%u, %u) but not shopts (?).\n",
                       wcls, widx);
      return;
    }
    else {
      t_sync = &shopts->t_sync;
    }
  }
  else {
    /*
     * This isn't a worker.  That leaves
     * - the master
     * - the input
     * - the reaper
     */
    if(gopts->mctl.t_sync.tid == me) {
      t_sync = &gopts->mctl.t_sync;
    }
    else if(gopts->i_sync.t_sync.tid == me) {
      t_sync = &gopts->i_sync.t_sync;
    }
    else if(gopts->r_sync.t_sync.tid == me) {
      t_sync = &gopts->r_sync.t_sync;
    }
  }

  if(!t_sync) {
    s_log(G_WARNING, "Could not find t_sync struct for %lu.\n", me);
    return;
  }

  s_log(G_DSYNC, "OP %s  LockID %3u  LastLock %3u  CurrArrPos %3u\n",
                 (op ? "del" : "add"), lockID,
                 t_sync->lock_order[t_sync->curr_lock - 1],
                 t_sync->curr_lock);

  if(t_sync->curr_lock >= MAX_LOCK_IDX) {
    s_log(G_WARNING, "WARNING: Thread is holding too many locks.\n");
    return;
  }

  if(op == L_ADD) {
    if(t_sync->lock_order[t_sync->curr_lock - 1] >= lockID) {
      s_log(G_WARNING, "WARNING: Thread is locking out-of-order.\n");
    }
    t_sync->lock_order[t_sync->curr_lock++] = lockID;
  }
  else if(op == L_DEL) {
    if(t_sync->lock_order[t_sync->curr_lock - 1] != lockID) {
      s_log(G_WARNING, "WARNING: Release locks out-of-order.\n");
    }
    t_sync->lock_order[--t_sync->curr_lock] = 0;
  }
  else {
    return;
  }
}

/*
 * Compare two workers by their locking order.
 */
static int worker_compare(const void *w_a, const void *w_b)
{
  worker_data *wda = (worker_data *)w_a;
  worker_data *wdb = (worker_data *)w_b;

  if(wda->wcls < wdb->wcls) {
    return -1;
  }
  else if(wda->wcls > wdb->wcls) {
    return 1;
  }
  else {
    if(wda->worker_index < wdb->worker_index) {
      return -1;
    }
    else if(wda->worker_index > wdb->worker_index) {
      return 1;
    }
    else {
      return 0;
    }
  }
}
