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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>

#include "calibrate.h"
#include "constants.h"
#include "linklib.h"
#include "memworker.h"
#include "utilrand.h"
#include "utillog.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

/*
 * Do the work for this epoch.
 */
static int memwork(gamut_opts *gopts, mem_opts *mem, char *buf,
                   uint64_t *currpos, int64_t *target_memio,
                   double blocks_per_epoch, double *curr_blocks,
                   int64_t *stride_left);

/*
 * Allocate memory of a given size, then cycle through the working
 *   set size, touching each block to force it back into memory.
 */
void* memworker(void *opts)
{
  char *buf;
  int i;
  int rc;
  int mem_index;
  int32_t target_epochs;
  int64_t link_waittime;    /* Total time waiting on links (usecs) */
  int64_t stride_left;      /* How long before a random block? */
  int64_t target_memio;     /* Target number of epochs */
  uint64_t currpos;         /* Current position (bytes) */
  uint64_t next_deadline;   /* Next deadline (usecs) */
  double blocks_per_epoch;  /* Blocks we touch per epoch */
  double curr_blocks;       /* Blocks this epoch */
  double curr_epochs;       /* How many epochs for this link */
  double epochs_per_link;   /* Epochs per link (if any) */
  mem_opts *mem;
  gamut_opts *gopts;
  struct timeval start;
  struct timeval finish;
  struct timeval finish_time;

  if(!opts)
    return NULL;

  gopts = (gamut_opts *)opts;

  mem_index = worker_register(gopts, CLS_MEM);
  if(mem_index < 0) {
    return NULL;
  }
  else {
    mem = &gopts->mem[mem_index];
  }

  /*
   * See if we need to wait for another linked worker
   *   to set us off.
   */
  rc = link_start_wait(gopts, CLS_MEM, mem_index);
  if(rc < 0) {
    return NULL;
  }

  (void)gettimeofday(&mem->shopts.start_time, NULL);
  mem->total_memio             = 0;
  mem->shopts.missed_deadlines = 0;
  mem->shopts.missed_usecs     = 0;
  mem->shopts.total_deadlines  = 0;

  buf           = NULL;
  link_waittime = 0;

restart:
  (void)gettimeofday(&mem->shopts.mod_time, NULL);
  mem->shopts.dirty = 0;

  target_memio    = 0;
  epochs_per_link = 0.0;
  curr_epochs     = 0.0;

  rc = validate_worker_opts(opts, CLS_MEM, mem_index);
  if(rc <= 0)
  {
    s_log(G_WARNING, "%s has invalid memory options.\n",
                     mem->shopts.label);
    goto clean_out;
  }

  /*
   * Calculate the first deadline and the final deadline (if necessary).
   */
  {
    struct timeval tv;

    (void)gettimeofday(&tv, NULL);

    /*
     * If there's a time limit, let's calculate it.
     */
    if(mem->shopts.exec_time) {
      finish_time.tv_sec  = tv.tv_sec + mem->shopts.exec_time;
      finish_time.tv_usec = tv.tv_usec;
    }
    else {
      finish_time.tv_sec  = 0;
      finish_time.tv_usec = 0;
    }

    next_deadline  = tv.tv_usec;
    next_deadline += tv.tv_sec * US_SEC;
  }

  /*
   * We call 'realloc' here instead of malloc, since this
   *   handles the situation where we're coming through the
   *   first time (buf will be NULL, and realloc behaves
   *   like malloc) and when we're here because of a 'dirty'
   *   tag (buf will not be NULL, but realloc works anyway).
   */
  buf = (char *)realloc(buf, (size_t)mem->total_ram);
  if(!buf) {
    s_log(G_WARNING, "Could not malloc %llu bytes: %s.\n",
                     mem->total_ram, strerror(errno));
    goto clean_out;
  }


  /*
   * Figure out how many blocks we need to touch per epoch.
   */
  blocks_per_epoch = (double)mem->iorate
                     / (mem->blksize * WORKER_EPOCHS_PER_SEC);

  /*
   * The first step is to go through and touch all the blocks,
   *   forcing the OS to actually allocate them.
   */
  for(i = 0;i < mem->ntblks;i++) {
    buf[i * mem->blksize] = (char)(0xff & i);
  }

  /*
   * Should we start the random stride counter?  This counts down how
   *   long before we jump to a random address.  Until then, simply go
   *   to the next address.
   */
  if(mem->stride)
    stride_left = 0;
  else
    stride_left = -1;

  /*
   * Calculate the total number of memory accesses this worker will
   *   perform
   */
  if(mem->shopts.max_work)
  {
    target_memio = mem->shopts.max_work / mem->blksize;
    /*
     * Even if we're not supposed to do one block's worth of work,
     *   do one anyway (just to do /something/).
     */
    if(!target_memio)
      target_memio = 1;
  }
  else
  {
    target_memio = -1;
  }

  /*
   * Make sure that if we've been asked to be part of a link,
   *   that we've got enough information to be sure that the
   *   link will actually work.
   */
  if(mem->shopts.link_work && mem->shopts.next_worker)
  {
    shared_opts *link_shopts;

    epochs_per_link = (double)mem->shopts.link_work
                      / (blocks_per_epoch * mem->blksize);
    curr_epochs     = epochs_per_link;
    target_epochs   = (int32_t)curr_epochs;

    link_shopts      = (shared_opts *)mem->shopts.next_worker;

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
   * 2. Perform the accesses
   * 3. If we're linked with another worker, see if it's handoff time.
   * 4. See if it's time to exit
   * 5. Sleep, if there's enough time
   */
  currpos     = 0;   /* Start at the beginning of the buffer */
  curr_blocks = 0.0; /* We haven't touched anything yet */
  (void)gettimeofday(&start, NULL);
  while(!mem->shopts.exiting) {
    struct timeval now;

    /* Steps 1 & 2 for an unlinked worker */
    if(target_epochs < 0) {
      /* Step 1 */
      next_deadline += US_PER_WORKER_EPOCH;

      /* Step 2 */
      rc = memwork(gopts, mem, buf, &currpos, &target_memio,
                   blocks_per_epoch, &curr_blocks, &stride_left);
      if(rc < 0) {
        s_log(G_WARNING, "Error doing memwork.  Exiting.\n");
        mem->shopts.exiting = 1;
        break;
      }
      else if(!rc) {
        s_log(G_DEBUG, "Memwork says we need to bail.\n");
        break;
      }
    }
    else { /* (target_epochs >= 0), a linked worker */
      if(target_epochs > 0) {
        /* Step 1 */
        next_deadline += US_PER_WORKER_EPOCH;

        rc = memwork(gopts, mem, buf, &currpos, &target_memio,
                     blocks_per_epoch, &curr_blocks, &stride_left);
        if(rc < 0) {
          s_log(G_WARNING, "Error doing memwork.  Exiting.\n");
          mem->shopts.exiting = 1;
          break;
        }
        else if(!rc) {
          s_log(G_DEBUG, "Memwork says we need to bail.\n");
          break;
        }

        target_epochs--;
      } /* end (target_epochs > 0) */

      /*
       * If we do have to wait, make sure we don't over-work
       *   when it's our turn.  Move the next_deadline backward
       *   by as much time as we spend waiting.
       */
      if(!target_epochs) {
        int64_t timediff;
        struct timeval b_link;

        (void)gettimeofday(&b_link, NULL);
        rc = link_next_wait(gopts, CLS_MEM, mem_index, epochs_per_link,
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
    (void)gettimeofday(&now, NULL);

      /* Are we past the deadline? */
    if(!finish_time.tv_sec
       || (finish_time.tv_sec > now.tv_sec)
       || ((finish_time.tv_sec == now.tv_sec)
           && (finish_time.tv_usec > now.tv_usec)
          )
      )
    {
      /* Is there enough time to sleep? */
      int64_t time_diff;
      int64_t current_time;

      current_time  = now.tv_usec;
      current_time += now.tv_sec * US_SEC;

      time_diff = next_deadline - current_time;
      if(current_time < next_deadline)
      {
        if(time_diff > MIN_SLEEP_US) {
          struct timeval sleeptv;

          sleeptv.tv_sec  = time_diff / US_SEC;
          sleeptv.tv_usec = time_diff - (sleeptv.tv_sec * US_SEC);
          (void)select(0, (fd_set *)NULL, (fd_set *)NULL,
                       (fd_set *)NULL, &sleeptv);
        }
      }
      else
      {
        mem->shopts.missed_deadlines++;
        mem->shopts.missed_usecs += (current_time - next_deadline);
      }
      mem->shopts.total_deadlines++;
    }
    else {
      mem->shopts.exiting = 1;
      break;
    }

    if(mem->shopts.dirty) {
      s_log(G_INFO, "%s reloading values.\n", mem->shopts.label);
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
  if(mem->total_memio)
  {
    char iorate[SMBUFSIZE];
    double iotime;
    double totaltime;
    int64_t io_usec;
    int64_t total_usec;
    uint64_t avg_miss_time;

    total_usec = calculate_timediff(&start, &finish);
    io_usec    = total_usec - link_waittime;
    totaltime  = (double)total_usec / US_SEC;
    iotime     = (double)io_usec / US_SEC;
    if(mem->shopts.missed_deadlines) {
      avg_miss_time = mem->shopts.missed_usecs
                      / mem->shopts.missed_deadlines;
    }
    else {
      avg_miss_time = 0;
    }

    print_scaled_number(iorate, SMBUFSIZE,
                        (uint64_t)(mem->total_memio / totaltime), 1);
    s_log(G_NOTICE, "%s did %llu I/O in %.4f sec "
                    "at %sps (total).\n", mem->shopts.label,
                    mem->total_memio, totaltime, iorate);

    if(link_waittime) {
      print_scaled_number(iorate, SMBUFSIZE,
                          (uint64_t)(mem->total_memio / iotime), 1);
      s_log(G_NOTICE, "%s did %llu I/O in %.4f sec "
                      "at %sps (work).\n", mem->shopts.label,
                      mem->total_memio, iotime, iorate);
    }

    s_log(G_NOTICE, "%s missed %llu of %llu deadlines by %llu usecs (avg).\n",
                    mem->shopts.label, mem->shopts.missed_deadlines,
                    mem->shopts.total_deadlines, avg_miss_time);
  }

  if(buf)
    free(buf);

  /*
   * Remove ourselves from any links.
   */
  rc = link_remove(gopts, CLS_MEM, mem_index);
  if(rc < 0) {
    s_log(G_WARNING, "Error removing %s from any link.\n",
                     mem->shopts.label);
  }

  (void)worker_unregister(gopts, CLS_MEM);

  return NULL;  
}

/*
 * Do the work for this epoch.
 */
static int memwork(gamut_opts *gopts, mem_opts *mem, char *buf,
                   uint64_t *currpos, int64_t *target_memio,
                   double blocks_per_epoch, double *curr_blocks,
                   int64_t *stride_left)
{
  int64_t l_stride_left;
  int64_t l_target_memio;
  uint64_t l_currpos;
  uint64_t target_blocks;
  double l_curr_blocks;

  if(!gopts || !mem || !buf || !currpos || !target_memio
     || (blocks_per_epoch < 0) || !curr_blocks || !stride_left)
  {
    return -1;
  }

  /*
   * Create local copies of all these variables.
   */
  l_currpos      = *currpos;
  l_curr_blocks  = *curr_blocks;
  l_stride_left  = *stride_left;
  l_target_memio = *target_memio;

  l_curr_blocks += blocks_per_epoch;
  target_blocks  = (uint64_t)l_curr_blocks;

  s_log(G_DLOOP, "Target blocks: %llu.\n", target_blocks);

  while(target_blocks) {
    if(!l_stride_left) {
      /*
       * Generate a random block address
       */
      l_currpos = (long)RandInt(mem->nwblks - 1);
      l_stride_left = mem->stride;
    }
    else {
      l_currpos++;
      if(l_currpos == mem->nwblks)
        l_currpos = 0;
    }

    /*
     * Mask the 8 lowest bits and write to memory.
     */
    buf[l_currpos * mem->blksize] = (char)(l_currpos & 0xff);
    mem->total_memio += mem->blksize;
    target_blocks--;

    if(l_stride_left > 0)
      l_stride_left--;

    if(l_target_memio > 0) {
      l_target_memio--;
      if(!l_target_memio) {
        mem->shopts.exiting = 1;
        break;
      }
    }
  }

  l_curr_blocks -= (uint64_t)l_curr_blocks;

  /*
   * Now copy the local variables back out to the calling variables.
   */
  *currpos      = l_currpos;
  *curr_blocks  = l_curr_blocks;
  *stride_left  = l_stride_left;
  *target_memio = l_target_memio;

  if(mem->shopts.exiting)
    return 0;
  else
    return 1;
}
