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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "opts.h"
#include "utilio.h"
#include "utillog.h"
#include "workerinfo.h"
#include "workersync.h"

/*
 * Worker-class specific option printing functions.
 */
static void print_shared_opts(shared_opts *shopts, int detail);
static void print_cpu_opts(cpu_opts *cpu, int detail);
static void print_mem_opts(mem_opts *mem, int detail);
static void print_dio_opts(dio_opts *dio, int detail);
static void print_nio_opts(nio_opts *nio, int detail);

/*
 * Print the contents of the statistics header.
 */
void print_stats_info(gamut_opts *gopts, int detail)
{
  if(!gopts || (detail < 0))
    return;

  s_log(G_INFO, "Workers requested: %4d  Workers created: %4d\n",
                gopts->wstats.workers_parsed,
                gopts->wstats.workers_spawned);
  s_log(G_INFO, "Workers errors:    %4d  Workers pending: %4d\n",
                gopts->wstats.workers_invalid,
                gopts->wstats.workers_pending);
  s_log(G_INFO, "Workers waiting:   %4d  Workers leading: %4d\n",
                gopts->wstats.workers_waiting,
                gopts->wstats.workers_leading);
  s_log(G_INFO, "Workers linked:    %4d  Workers running: %4d\n",
                gopts->wstats.workers_linked,
                gopts->wstats.workers_running);
  s_log(G_INFO, "Workers exiting:   %4d  Workers reaped:  %4d\n",
                gopts->wstats.workers_exiting,
                gopts->wstats.workers_reaped);

  /*
   * Print the lock info for the master, input, and reaper thread.
   */
  if(debug_sync) {
    char buf[BUFSIZE];
    int rc;

    rc = get_lock_info(buf, BUFSIZE, &gopts->mctl.t_sync);
    if(rc < 0) {
      s_log(G_WARNING, "Error getting master lock info.\n");
    }
    else {
      s_log(G_INFO, "MASTER LOCK: %s.\n", buf);
    }

    rc = get_lock_info(buf, BUFSIZE, &gopts->r_sync.t_sync);
    if(rc < 0) {
      s_log(G_WARNING, "Error getting reaper lock info.\n");
    }
    else {
      s_log(G_INFO, "REAPER LOCK: %s.\n", buf);
    }
    rc = get_lock_info(buf, BUFSIZE, &gopts->i_sync.t_sync);
    if(rc < 0) {
      s_log(G_WARNING, "Error getting input lock info.\n");
    }
    else {
      s_log(G_INFO, "INPUT LOCK: %s.\n", buf);
    }
  }

  return;
}

/*
 * Print all the information for a given worker.
 */
void print_worker_info(gamut_opts *gopts, worker_class wcls,
                       int widx, int detail)
{
  int rc;
  
  if(!gopts || !is_valid_cls(wcls) || (detail < 0))
    return;

  if(widx < 0) {
    int i;

    /*
     * If a particular index has not been specified, we iterate
     *   over all the currently-used workers in that class.
     *   Since we're dealing with the whole class, lock it down.
     */
    rc = lock_class(gopts, wcls);
    if(rc < 0) {
      goto fail_out;
    }
    switch(wcls) {
      case CLS_CPU:
        for(i = 0;i < MAX_CPUS;i++) {
          if(gopts->cpu[i].shopts.used) {
            rc = lock_worker(gopts, wcls, i);
            if(rc < 0) {
              (void)unlock_worker(gopts, wcls, i);
              goto class_out;
            }
            print_cpu_opts(&gopts->cpu[i], detail);
            rc = unlock_worker(gopts, wcls, i);
            if(rc < 0) {
              goto class_out;
            }
          }
        }
        break;

      case CLS_MEM:
        for(i = 0;i < MAX_MEMS;i++) {
          if(gopts->mem[i].shopts.used) {
            rc = lock_worker(gopts, wcls, i);
            if(rc < 0) {
              (void)unlock_worker(gopts, wcls, i);
              goto class_out;
            }
            print_mem_opts(&gopts->mem[i], detail);
            rc = unlock_worker(gopts, wcls, i);
            if(rc < 0) {
              goto class_out;
            }
          }
        }
        break;

      case CLS_DISK:
        for(i = 0;i < MAX_DIOS;i++) {
          if(gopts->disk_io[i].shopts.used) {
            rc = lock_worker(gopts, wcls, i);
            if(rc < 0) {
              (void)unlock_worker(gopts, wcls, i);
              goto class_out;
            }
            print_dio_opts(&gopts->disk_io[i], detail);
            rc = unlock_worker(gopts, wcls, i);
            if(rc < 0) {
              goto class_out;
            }
          }
        }
        break;

      case CLS_NET:
        for(i = 0;i < MAX_NIOS;i++) {
          if(gopts->net_io[i].shopts.used) {
            rc = lock_worker(gopts, wcls, i);
            if(rc < 0) {
              (void)unlock_worker(gopts, wcls, i);
              goto class_out;
            }
            print_nio_opts(&gopts->net_io[i], detail);
            rc = unlock_worker(gopts, wcls, i);
            if(rc < 0) {
              goto class_out;
            }
          }
        }
        break;

      default:
        break;
    }
class_out:
    (void)unlock_class(gopts, wcls);
  }
  else {
    /*
     * If a specific worker index has been provided, we print the
     *   details for that worker only.
     */
    rc = lock_worker(gopts, wcls, widx);
    if(rc < 0) {
      goto fail_out;
    }
    switch(wcls) {
      case CLS_CPU:
        if(widx >= MAX_CPUS)
          break;

        print_cpu_opts(&gopts->cpu[widx], detail);
        break;

      case CLS_MEM:
        if(widx >= MAX_MEMS)
          break;

        print_mem_opts(&gopts->mem[widx], detail);
        break;

      case CLS_DISK:
        if(widx >= MAX_DIOS)
          break;

        print_dio_opts(&gopts->disk_io[widx], detail);
        break;

      case CLS_NET:
        if(widx >= MAX_NIOS)
          break;

        print_nio_opts(&gopts->net_io[widx], detail);
        break;

      default:
        break;
    }
    rc = unlock_worker(gopts, wcls, widx);
    if(rc < 0) {
      goto fail_out;
    }
  }

fail_out:
  return;
}

/*
 * Worker-class specific option printing functions.
 */
static void print_shared_opts(shared_opts *shopts, int detail)
{
  char sbuf[BUFSIZE];
  char mbuf[BUFSIZE];

  if(!shopts || (detail < 0))
    return;

  s_log(G_INFO, "Worker ID: %6u  Worker label: \"%s\"\n",
                shopts->wid, shopts->label);
  s_log(G_INFO, "US:%d PE:%d WA:%d LI:%d LE:%d RU:%d "
                "LW:%d DI:%d MW:%d EX:%d PA:%d\n",
                shopts->used, shopts->pending, shopts->waiting,
                shopts->linked, shopts->leading, shopts->running,
                shopts->linkwait, shopts->dirty, shopts->mwait,
                shopts->exiting, shopts->paused);
  if(shopts->start_time.tv_sec) {
    struct tm *tm;

    tm = localtime((const time_t *) &shopts->start_time.tv_sec);
    strftime(sbuf, BUFSIZE, "%g/%m/%d %H:%M:%S", tm);
    tm = localtime((const time_t *) &shopts->mod_time.tv_sec);
    strftime(mbuf, BUFSIZE, "%g/%m/%d %H:%M:%S", tm);

    s_log(G_INFO, "Start time:   %s.%06u\n", sbuf,
                  (unsigned int)shopts->start_time.tv_usec);
    s_log(G_INFO, "Updated time: %s.%06u\n", mbuf,
                  (unsigned int)shopts->mod_time.tv_usec);
  }
  s_log(G_INFO, "Max run time: %u secs\n", shopts->exec_time);
  if(shopts->prev_worker
     && strlen(((shared_opts *)shopts->prev_worker)->label)
    )
  {
    s_log(G_INFO, "Prev. link:   %s\n",
                  ((shared_opts *)shopts->prev_worker)->label);
  }
  if(shopts->next_worker
     && strlen(((shared_opts *)shopts->next_worker)->label)
    )
  {
    s_log(G_INFO, "Next link:    %s\n",
                  ((shared_opts *)shopts->next_worker)->label);
  }

  /*
   * NOTE: We don't print maximum work here, since the units of
   *       work are specific to each worker class.
   */

  if(debug_sync) {
    char buf[BUFSIZE];
    int rc;

    rc = get_lock_info(buf, BUFSIZE, &shopts->t_sync);
    if(rc < 0) {
      s_log(G_WARNING, "Error getting lock info.\n");
    }
    else {
      s_log(G_INFO, "%s LOCK: %s\n", shopts->label, buf);
    }
  }
}

static void print_cpu_opts(cpu_opts *cpu, int detail)
{
  char pct_buf[SMBUFSIZE];
  char max_buf[SMBUFSIZE];
  char mdlines[SMBUFSIZE];
  char tdlines[SMBUFSIZE];

  if(!cpu || (detail < 0))
    return;

  print_scaled_number(pct_buf, SMBUFSIZE, cpu->total_work, 0);
  print_scaled_number(max_buf, SMBUFSIZE, cpu->shopts.max_work, 0);
  print_scaled_number(mdlines, SMBUFSIZE, cpu->shopts.missed_deadlines, 0);
  print_scaled_number(tdlines, SMBUFSIZE, cpu->shopts.total_deadlines, 0);

  print_shared_opts(&cpu->shopts, detail);

  s_log(G_INFO, "Load avg: %8u %%\n", cpu->percent_cpu);
  s_log(G_INFO, "Ops done: %12llu (%9sOps)\n",
                cpu->total_work, pct_buf);
  s_log(G_INFO, "Max. ops: %12llu (%9sOps)\n",
                cpu->shopts.max_work, max_buf);
  s_log(G_INFO, "Missed deadlines: %12llu (%9s)\n",
                cpu->shopts.missed_deadlines, mdlines);
  s_log(G_INFO, "Missed by usecs:  %12llu\n",
                cpu->shopts.missed_usecs);
  s_log(G_INFO, "Total deadlines:  %12llu (%9s)\n",
                cpu->shopts.total_deadlines, tdlines);
}

static void print_mem_opts(mem_opts *mem, int detail)
{
  char total[SMBUFSIZE];
  char wset[SMBUFSIZE];
  char rate[SMBUFSIZE];
  char io_done[SMBUFSIZE];
  char io_max[SMBUFSIZE];
  char mdlines[SMBUFSIZE];
  char tdlines[SMBUFSIZE];

  if(!mem || (detail < 0))
    return;

  print_shared_opts(&mem->shopts, detail);
  print_scaled_number(total,   SMBUFSIZE, mem->total_ram, 1);
  print_scaled_number(wset,    SMBUFSIZE, mem->working_ram, 1);
  print_scaled_number(rate,    SMBUFSIZE, mem->iorate, 1);
  print_scaled_number(io_done, SMBUFSIZE, mem->total_memio, 1);
  print_scaled_number(io_max,  SMBUFSIZE, mem->shopts.max_work, 1);
  print_scaled_number(mdlines, SMBUFSIZE, mem->shopts.missed_deadlines, 0);
  print_scaled_number(tdlines, SMBUFSIZE, mem->shopts.total_deadlines, 0);

  s_log(G_INFO, "Total memory:  %12llu (%9s)\n",
                mem->total_ram, total);
  s_log(G_INFO, "Working set:   %12llu (%9s)\n",
                mem->working_ram, wset);
  s_log(G_INFO, "Stride length: %12u pages\n", mem->stride);
  s_log(G_INFO, "I/O rate:      %12llu/s (%9s)\n",
                mem->iorate, rate);
  s_log(G_INFO, "I/O done:      %12llu   (%9s)\n",
                mem->total_memio, io_done);
  s_log(G_INFO, "Max. I/O:      %12llu   (%9s)\n",
                mem->shopts.max_work, io_max);
  s_log(G_INFO, "Missed deadlines: %12llu (%9s)\n",
                mem->shopts.missed_deadlines, mdlines);
  s_log(G_INFO, "Missed by usecs:  %12llu\n",
                mem->shopts.missed_usecs);
  s_log(G_INFO, "Total deadlines:  %12llu (%9s)\n",
                mem->shopts.total_deadlines, tdlines);
}

static void print_dio_opts(dio_opts *dio, int detail)
{
  char bsize[SMBUFSIZE];
  char rate[SMBUFSIZE];
  char total_io[SMBUFSIZE];
  char max_io[SMBUFSIZE];
  char reads[SMBUFSIZE];
  char writes[SMBUFSIZE];
  char seeks[SMBUFSIZE];
  char read_us[SMBUFSIZE];
  char write_us[SMBUFSIZE];
  char seek_us[SMBUFSIZE];
  char mdlines[SMBUFSIZE];
  char tdlines[SMBUFSIZE];

  if(!dio || (detail < 0))
    return;

  print_shared_opts(&dio->shopts, detail);

  print_scaled_number(bsize,  SMBUFSIZE, dio->blksize, 1);
  print_scaled_number(rate,   SMBUFSIZE, dio->iorate,  1);
  print_scaled_number(total_io, SMBUFSIZE, dio->total_diskio, 1);
  print_scaled_number(max_io, SMBUFSIZE, dio->shopts.max_work, 1);
  print_scaled_number(reads,  SMBUFSIZE, dio->num_diskio[C_IOREAD], 0);
  print_scaled_number(writes, SMBUFSIZE, dio->num_diskio[C_IOWRITE], 0);
  print_scaled_number(seeks,  SMBUFSIZE, dio->num_diskio[C_IOSEEK], 0);
  print_scaled_number(read_us,  SMBUFSIZE, dio->io_usec[C_IOREAD], 0);
  print_scaled_number(write_us, SMBUFSIZE, dio->io_usec[C_IOWRITE], 0);
  print_scaled_number(seek_us,  SMBUFSIZE, dio->io_usec[C_IOSEEK], 0);
  print_scaled_number(mdlines, SMBUFSIZE, dio->shopts.missed_deadlines, 0);
  print_scaled_number(tdlines, SMBUFSIZE, dio->shopts.total_deadlines, 0);

  s_log(G_INFO, "I/O file:   %s\n", dio->file);
  s_log(G_INFO, "Block size: %u (%9s)\n", dio->blksize, bsize);
  s_log(G_INFO, "Blocks:     %8u\n", dio->nblks);
  s_log(G_INFO, "Mode:       %2hu  I/O mix: %4hu rd/%4hu wr/%4hu sk\n",
                dio->create, dio->iomix.numrds, dio->iomix.numwrs,
                dio->iomix.numsks);
  s_log(G_INFO, "I/O rate:   %8u/s (%9s/s)\n", dio->iorate, rate);
  s_log(G_INFO, "Total I/O:  %8llu   (%9s)\n",
                dio->total_diskio, total_io);
  s_log(G_INFO, "Max I/O:    %8llu   (%9s)\n",
                dio->shopts.max_work, max_io);
  s_log(G_INFO, "# Reads:    %8llu   (%9s)  uSecs: %10llu (%9s)\n",
                dio->num_diskio[C_IOREAD], reads,
                dio->io_usec[C_IOREAD], read_us);
  s_log(G_INFO, "# Writes:   %8llu   (%9s)  uSecs: %10llu (%9s)\n",
                dio->num_diskio[C_IOWRITE], writes,
                dio->io_usec[C_IOWRITE], write_us);
  s_log(G_INFO, "# Seeks:    %8llu   (%9s)  uSecs: %10llu (%9s)\n",
                dio->num_diskio[C_IOSEEK], seeks,
                dio->io_usec[C_IOSEEK], seek_us);
  s_log(G_INFO, "Missed deadlines: %12llu (%9s)\n",
                dio->shopts.missed_deadlines, mdlines);
  s_log(G_INFO, "Missed by usecs:  %12llu\n",
                dio->shopts.missed_usecs);
  s_log(G_INFO, "Total deadlines:  %12llu (%9s)\n",
                dio->shopts.total_deadlines, tdlines);
}

static void print_nio_opts(nio_opts *nio, int detail)
{
  if(!nio || (detail < 0))
    return;

  print_shared_opts(&nio->shopts, detail);
}
