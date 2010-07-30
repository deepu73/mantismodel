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

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "calibrate.h"
#include "constants.h"
#include "cpuburn.h"
#include "cpuworker.h"
#include "linklib.h"
#include "utillog.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

/*
 * Burn CPU at a steady rate in this worker.
 */
void* cpuworker(void *opts)
{
  int rc;
  int cpu_index;
  int32_t target_epochs;
  int64_t next_deadline;
  int64_t target_cpuwork;
  int64_t link_waittime;
  double curr_epochs;
  double epochs_per_link;
  cpu_opts *cpu;
  cpu_burn_opts cbopts;
  gamut_opts *gopts;
  struct timeval start;
  struct timeval finish;
  struct timeval finish_time;

  if(!opts)
    return NULL;

  gopts = (gamut_opts *)opts;

  cpu_index = worker_register(gopts, CLS_CPU);
  if(cpu_index < 0) {
    return NULL;
  }
  else {
    cpu = &gopts->cpu[cpu_index];
  }

  /*
   * See if we need to wait for another linked worker
   *   to set us off.
   */
  rc = link_start_wait(gopts, CLS_CPU, cpu_index);
  if(rc < 0) {
    return NULL;
  }

  (void)gettimeofday(&cpu->shopts.start_time, NULL);
  cpu->total_work              = 0;
  cpu->shopts.missed_deadlines = 0;
  cpu->shopts.missed_usecs     = 0;
  cpu->shopts.total_deadlines  = 0;

  link_waittime                = 0;

restart:
  (void)gettimeofday(&cpu->shopts.mod_time, NULL);
  cpu->shopts.dirty = 0;

  target_cpuwork  = 0;
  epochs_per_link = 0.0;
  curr_epochs     = 0.0;
  target_epochs   = 0;

  rc = validate_worker_opts(opts, CLS_CPU, cpu_index);
  if(rc <= 0) {
    s_log(G_WARNING, "%s has invalid settings.\n", cpu->shopts.label);
    goto clean_out;
  }

  /*
   * We set the timer and deadline first so any delays in starting our
   *    work are absorbed; we'll catch up if necessary.
   */
  {
    struct timeval tv;

    (void)gettimeofday(&tv, NULL);
    
    if(cpu->shopts.exec_time) {
      finish_time.tv_sec  = tv.tv_sec + cpu->shopts.exec_time;
      finish_time.tv_usec = tv.tv_usec;
    }
    else {
      finish_time.tv_sec  = 0;
      finish_time.tv_usec = 0;
    }

    next_deadline  = tv.tv_usec;
    next_deadline += tv.tv_sec * US_SEC;
  }

  memset(&cbopts, 0, sizeof(cbopts));
  cbopts.count64 = (uint64_t)(second_count * cpu->percent_cpu)
                   / (100 * WORKER_EPOCHS_PER_SEC);
  s_log(G_INFO, "%s will do %lld CPU work per epoch.\n",
                 cpu->shopts.label, cbopts.count64);

  if(cpu->shopts.max_work)
  {
    target_cpuwork = cpu->shopts.max_work / cbopts.count64;
    /*
     * If the target work is 0, it's less than an epoch's worth.
     * Do one iteration anyway.
     */
    if(!target_cpuwork)
      target_cpuwork = 1;
  }
  else
  {
    target_cpuwork = -1;
  }

  /*
   * Make sure that if we've been asked to be part of a link,
   *   that we've got enough information to be sure that the
   *   link will actually work.
   */
  if(cpu->shopts.link_work && cpu->shopts.next_worker)
  {
    shared_opts *link_shopts;

    epochs_per_link = (double)cpu->shopts.link_work / cbopts.count64;
    curr_epochs     = epochs_per_link;
    target_epochs   = (int32_t)curr_epochs;

    link_shopts      = (shared_opts *)cpu->shopts.next_worker;

    s_log(G_DEBUG, "Will do %.2f epochs per link, handing off to %s.\n",
                   epochs_per_link, link_shopts->label);
  }
  else
  {
    target_epochs = -1;
  }

  /*
   * Each time through, perform these operations in this order:
   * 1. Calculate the next deadline
   * 2. Perform the CPU work
   * 3. If we're linked with another worker, see if it's handoff time
   * 4. See if it's time to exit
   * 5. Sleep, if there's enough time
   */
  (void)gettimeofday(&start, NULL);
  while(!cpu->shopts.exiting) {
    struct timeval now;

    /* Steps 1, 2 & 3 */
    if(target_epochs < 0) {
      /*
       * Just step 1 & 2 (burn CPU) since there are no links here.
       */
      next_deadline += US_PER_WORKER_EPOCH;

      cpu->cbfunc(cpu, &cbopts);
    }
    else /* (target_epochs >= 0) */ {
      if(target_epochs > 0) {
        next_deadline += US_PER_WORKER_EPOCH;

        cpu->cbfunc(cpu, &cbopts);
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
        rc = link_next_wait(gopts, CLS_CPU, cpu_index, epochs_per_link,
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
    if(target_cpuwork > 0) {
      target_cpuwork--;
      if(!target_cpuwork) {
        cpu->shopts.exiting = 1;
        break;
      }
    }

    /* Step 5 */
    (void)gettimeofday(&now, NULL);
    if(!finish_time.tv_sec
       || (finish_time.tv_sec > now.tv_sec)
       || ((finish_time.tv_sec == now.tv_sec)
           && (finish_time.tv_usec > now.tv_usec)
          )
      )
    {
      /*
       * Either there isn't a deadline
       *   or there is one, but we're a few seconds short
       *   or we're spot-on with the seconds
       *      and the usecs are short.
       */
      int64_t time_diff;
      int64_t current_time;

      current_time  = now.tv_usec;
      current_time += now.tv_sec * US_SEC;

      time_diff = next_deadline - current_time;
      s_log(G_DLOOP, "TD %lld\n", time_diff);
      if(current_time < next_deadline)
      {
        if(time_diff > MIN_SLEEP_US) {
          struct timeval sleeptv;

          sleeptv.tv_sec  = time_diff / US_SEC;
          sleeptv.tv_usec = time_diff - (sleeptv.tv_sec * US_SEC);
          s_log(G_DLOOP, "%s sleep.\n", cpu->shopts.label);
          (void)select(0, (fd_set *)NULL, (fd_set *)NULL,
                     (fd_set *)NULL, &sleeptv);
          s_log(G_DLOOP, "%s woke.\n", cpu->shopts.label);
        }
      }
      else
      {
        cpu->shopts.missed_deadlines++;
        cpu->shopts.missed_usecs += (current_time - next_deadline);
      }
      cpu->shopts.total_deadlines++;
    }
    else
    {
      cpu->shopts.exiting = 1;
      break;
    }

    if(cpu->shopts.dirty) {
      s_log(G_INFO, "%s reloading values.\n", cpu->shopts.label);
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
  if(cpu->total_work)
  {
    int64_t cpu_usec;
    int64_t total_usec;
    uint64_t avg_miss_time;
    double cputime;
    double totaltime;

    total_usec    = calculate_timediff(&start, &finish);
    cpu_usec      = total_usec - link_waittime;
    totaltime     = (double)total_usec / US_SEC;
    cputime       = (double)cpu_usec / US_SEC;
    if(cpu->shopts.missed_deadlines) {
      avg_miss_time = cpu->shopts.missed_usecs
                      / cpu->shopts.missed_deadlines;
    }
    else {
      avg_miss_time = 0;
    }

    s_log(G_NOTICE, "%s did %llu CPU work in %.3f sec (total).\n",
                    cpu->shopts.label, cpu->total_work, totaltime);

    /*
     * Only print this out if we spent any time waiting on a link.
     */
    if(link_waittime) {
      s_log(G_NOTICE, "%s did %llu CPU work in %.3f sec (working).\n",
                      cpu->shopts.label, cpu->total_work, cputime);
    }

    s_log(G_INFO, "%s missed %llu of %llu deadlines by %llu usecs (avg).\n",
                  cpu->shopts.label, cpu->shopts.missed_deadlines,
                  cpu->shopts.total_deadlines, avg_miss_time);
  }

  /*
   * Remove ourselves from any link.
   */
  rc = link_remove(gopts, CLS_CPU, cpu_index);
  if(rc < 0) {
    s_log(G_WARNING, "Error removing %s from any link.\n",
                     cpu->shopts.label);
  }

  /*
   * Something has come along and decided we need to go.
   *   Clean up state behind us.
   */
  (void)worker_unregister(gopts, CLS_CPU);

  return NULL;
}
