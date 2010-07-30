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

#include "utilio.h"
#include "utillog.h"
#include "utilnet.h"
#include "workerctl.h"
#include "workerlib.h"
#include "workeropts.h"
#include "workersync.h"

#include "cpuburn.h"

/*
 * All workers must have an ID and a label.
 *  If the label isn't provided, we give it one based on the ID.
 */
static workerID next_workerID = 1;
static workerID get_next_workerID(void);

/*
 * Worker-class specific synchronization initialization functions.
 */
static int init_cpu_opts(gamut_opts *gopts);
static int init_mem_opts(gamut_opts *gopts);
static int init_dio_opts(gamut_opts *gopts);
static int init_nio_opts(gamut_opts *gopts);

/*
 * Worker-class specific option parsing functions.
 */
static int parse_cpu_opts(gamut_opts *gopts, cpu_opts *cpu, char *attrs);
static int parse_mem_opts(gamut_opts *gopts, mem_opts *mem, char *attrs);
static int parse_dio_opts(gamut_opts *gopts, dio_opts *dio, char *attrs);
static int parse_nio_opts(gamut_opts *gopts, nio_opts *nio, char *attrs);

/*
 * Worker-class specific copying functions.
 */
static int copy_cpu_opts(cpu_opts *src, cpu_opts *dest, int keepID);
static int copy_mem_opts(mem_opts *src, mem_opts *dest, int keepID);
static int copy_dio_opts(dio_opts *src, dio_opts *dest, int keepID);
static int copy_nio_opts(nio_opts *src, nio_opts *dest, int keepID);

/*
 * Worker-class specific option validation functions.
 */
static int validate_cpu_opts(gamut_opts *gopts, cpu_opts *cpu);
static int validate_mem_opts(gamut_opts *gopts, mem_opts *mem);
static int validate_dio_opts(gamut_opts *gopts, dio_opts *dio);
static int validate_nio_opts(gamut_opts *gopts, nio_opts *nio);

/*
 * Worker-class specific option cleaning functions.
 */
static void clean_cpu_opts(cpu_opts *cpu, int keepID);
static void clean_mem_opts(mem_opts *mem, int keepID);
static void clean_dio_opts(dio_opts *dio, int keepID);
static void clean_nio_opts(nio_opts *nio, int keepID);

/*
 * Validate that an 'after' tag is good, and tell that worker
 *   that they're now leading other workers.
 */
static int validate_after_opts(gamut_opts *gopts, shared_opts *shopts);

/*
 * Count the number of times a given label is present.
 */
static int label_count(gamut_opts *gopts, char *label);

/*
 * Initialize all the portions of the options struct.
 *   Mainly this involves the threading variables.
 *   Exit on failure.
 */
void init_opts(gamut_opts *gopts)
{
  int rc;
  
  if(!gopts) {
    exit(EXIT_FAILURE);
  }

  s_log(G_DEBUG, "Option size: %5d b  Master size: %5d b\n",
                 sizeof(gamut_opts), sizeof(master_ctl));
  s_log(G_DEBUG, "Stats size:  %5d b  Sync size:   %5d b\n",
                 sizeof(worker_stats), sizeof(worker_sync));
  s_log(G_DEBUG, "CPU size:    %5d b  Mem size:    %5d b\n",
                 sizeof(cpu_opts), sizeof(mem_opts));
  s_log(G_DEBUG, "Disk size:   %5d b  Net size:    %5d b\n",
                 sizeof(dio_opts), sizeof(nio_opts));

  /*
   * Initialize the master control struct.
   */
  gopts->mctl.t_sync.tid     = pthread_self();

  rc = pthread_mutex_init(&gopts->mctl.t_sync.lock,
                          (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing master mutex.\n");
    goto fail_out;
  }
  rc = pthread_cond_init(&gopts->mctl.t_sync.cond,
                         (pthread_condattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing master CV.\n");
    goto fail_out;
  }
  rc = pthread_mutex_init(&gopts->mctl.start_lock,
                          (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing master start mutex.\n");
    goto fail_out;
  }

  gopts->mctl.mcmd = MCMD_FREE;
  memset(gopts->mctl.mbuf, 0, sizeof(gopts->mctl.mbuf));

  gopts->mctl.t_sync.curr_lock = 0;
  memset(gopts->mctl.t_sync.lock_order, 0,
         sizeof(gopts->mctl.t_sync.lock_order));

  /*
   * Initialize the statistics and synchronization variables.
   */
  rc = pthread_mutex_init(&gopts->wstats.stats_lock,
                          (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing count mutex.\n");
    goto fail_out;
  }

  gopts->wstats.workers_parsed   = 0;
  gopts->wstats.workers_invalid  = 0;
  gopts->wstats.workers_spawned  = 0;
  gopts->wstats.workers_pending  = 0;
  gopts->wstats.workers_waiting  = 0;
  gopts->wstats.workers_linked   = 0;
  gopts->wstats.workers_leading  = 0;
  gopts->wstats.workers_running  = 0;
  gopts->wstats.workers_linkwait = 0;
  gopts->wstats.workers_moved    = 0;
  gopts->wstats.workers_exiting  = 0;
  gopts->wstats.workers_reaped   = 0;

  /*
   * Now take care of the reaper struct.
   */
  rc = pthread_mutex_init(&gopts->r_sync.t_sync.lock,
                          (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing reaper mutex.\n");
    goto fail_out;
  }
  rc = pthread_cond_init(&gopts->r_sync.t_sync.cond,
                         (pthread_condattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing reaper condition variable.\n");
    goto fail_out;
  }

  memset(gopts->r_sync.wdata, 0, sizeof(gopts->r_sync.wdata));
  gopts->r_sync.wqueue_size = 0;
  gopts->r_sync.exiting     = 0;

  gopts->r_sync.t_sync.curr_lock = 0;
  memset(gopts->r_sync.t_sync.lock_order, 0,
         sizeof(gopts->r_sync.t_sync.lock_order));

  /*
   * Now the 'after' bookkeeping struct.
   */
  rc = pthread_mutex_init(&gopts->a_sync.t_sync.lock,
                          (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing 'after' mutex.\n");
    goto fail_out;
  }
  rc = pthread_cond_init(&gopts->a_sync.t_sync.cond,
                         (pthread_condattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing 'after' condition variable.\n");
    goto fail_out;
  }

  memset(gopts->a_sync.wdata, 0, sizeof(gopts->a_sync.wdata));
  gopts->a_sync.wqueue_size = 0;
  gopts->a_sync.exiting     = 0;

  /*
   * Now the input struct.
   */
  rc = pthread_mutex_init(&gopts->i_sync.t_sync.lock,
                          (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing input mutex.\n");
    goto fail_out;
  }
  rc = pthread_cond_init(&gopts->i_sync.t_sync.cond,
                         (pthread_condattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing input condition variable.\n");
    goto fail_out;
  }

  memset(gopts->i_sync.wdata, 0, sizeof(gopts->i_sync.wdata));
  gopts->i_sync.wqueue_size = 0;
  gopts->i_sync.exiting     = 0;

  gopts->i_sync.t_sync.curr_lock = 0;
  memset(gopts->i_sync.t_sync.lock_order, 0,
         sizeof(gopts->i_sync.t_sync.lock_order));

  /*
   * Now the wait counter.
   */
  rc = pthread_mutex_init(&gopts->wcounter.c_lock,
                          (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing wait counter lock.\n");
    goto fail_out;
  }
  rc = pthread_cond_init(&gopts->wcounter.c_cond,
                         (pthread_condattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing wait condition variable.\n");
    goto fail_out;
  }

  gopts->wcounter.count = 0;

  /*
   * Now the set of linked workers.
   */
  rc = pthread_mutex_init(&gopts->wlinks.link_lock,
                          (pthread_mutexattr_t *)NULL);

  if(rc) {
    s_log(G_WARNING, "Error initializing worker link lock.\n");
    goto fail_out;
  }

  memset(gopts->wlinks.wlink, 0, sizeof(gopts->wlinks.wlink));

  /*
   * Initialize the four worker class locks.
   */
  rc = pthread_mutex_init(&gopts->cpu_lock, (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing CPU mutex.\n");
    goto fail_out;
  }
  rc = pthread_mutex_init(&gopts->mem_lock, (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing memory mutex.\n");
    goto fail_out;
  }
  rc = pthread_mutex_init(&gopts->dio_lock, (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing disk mutex.\n");
    goto fail_out;
  }
  rc = pthread_mutex_init(&gopts->nio_lock, (pthread_mutexattr_t *)NULL);
  if(rc) {
    s_log(G_WARNING, "Error initializing network mutex.\n");
    goto fail_out;
  }

  /*
   * Now go through and initialize all the sub-structures.
   */
  rc = init_cpu_opts(gopts);
  if(rc < 0) {
    goto fail_out;
  }
  rc = init_mem_opts(gopts);
  if(rc < 0) {
    goto fail_out;
  }
  rc = init_dio_opts(gopts);
  if(rc < 0) {
    goto fail_out;
  }
  rc = init_nio_opts(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  return;

fail_out:
  exit(EXIT_FAILURE);
}

/*
 * Parse the class-specific options and fill in the data structures.
 */
int parse_worker_opts(gamut_opts *gopts, worker_class wcls,
                      int widx, char *attrs)
{
  int rc;

  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  rc = -1;
  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS) {
        s_log(G_WARNING, "Invalid CPU index in parse: %d.\n", widx);
        goto fail_out;
      }
      rc = parse_cpu_opts(gopts, &gopts->cpu[widx], attrs);
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS) {
        s_log(G_WARNING, "Invalid memory index in parse: %d.\n", widx);
        goto fail_out;
      }
      rc = parse_mem_opts(gopts, &gopts->mem[widx], attrs);
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS) {
        s_log(G_WARNING, "Invalid disk index in parse: %d.\n", widx);
        goto fail_out;
      }
      rc = parse_dio_opts(gopts, &gopts->disk_io[widx], attrs);
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS) {
        s_log(G_WARNING, "Invalid net index in parse: %d.\n", widx);
        goto fail_out;
      }
      rc = parse_nio_opts(gopts, &gopts->net_io[widx], attrs);
      break;

    default:
      goto fail_out;
      break;
  }

fail_out:
  if(rc < 0) {
    gopts->wstats.workers_invalid++;
  }

  return rc;
}

/*
 * Validate the options that have been passed to the structure.
 */
int validate_worker_opts(gamut_opts *gopts,
                         worker_class wcls, int widx)
{
  int rc;

  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return -1;

  rc = -1;
  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS) {
        s_log(G_WARNING, "Invalid CPU index in validate: %d.\n", widx);
        break;
      }
      rc = validate_cpu_opts(gopts, &gopts->cpu[widx]);
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS) {
        s_log(G_WARNING, "Invalid memory index in validate: %d.\n", widx);
        break;
      }
      rc = validate_mem_opts(gopts, &gopts->mem[widx]);
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS) {
        s_log(G_WARNING, "Invalid disk index in validate: %d.\n", widx);
        break;
      }
      rc = validate_dio_opts(gopts, &gopts->disk_io[widx]);
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS) {
        s_log(G_WARNING, "Invalid net index in validate: %d.\n", widx);
        break;
      }
      rc = validate_nio_opts(gopts, &gopts->net_io[widx]);
      break;

    default:
      break;
  }

  return rc;
}

/*
 * Clean out a struct to prepare it for use (or re-use).
 *   This is done IN PLACE OF a memet(struct, 0, sizeof(struct))
 *   Do we keep the identity of the worker (tid, wid, headers, label)?
 */
void clean_worker_opts(gamut_opts *gopts, worker_class wcls,
                       int widx, int keepID)
{
  if(!gopts || !is_valid_cls(wcls) || (widx < 0) || (keepID < 0))
    return;

  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS) {
        s_log(G_WARNING, "Invalid CPU index in clean: %d.\n", widx);
        break;
      }
      clean_cpu_opts(&gopts->cpu[widx], keepID);
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS) {
        s_log(G_WARNING, "Invalid memory index in clean: %d.\n", widx);
        break;
      }
      clean_mem_opts(&gopts->mem[widx], keepID);
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS) {
        s_log(G_WARNING, "Invalid disk index in clean: %d.\n", widx);
        break;
      }
      clean_dio_opts(&gopts->disk_io[widx], keepID);
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS) {
        s_log(G_WARNING, "Invalid net index in clean: %d.\n", widx);
        break;
      }
      clean_nio_opts(&gopts->net_io[widx], keepID);
      break;

    default:
      break;
  }
}

/*
 * Get the "shared opts" struct of a given worker.
 */
shared_opts* get_shared_opts(gamut_opts *gopts,
                             worker_class wcls, int widx)
{
  shared_opts *shopts;
  
  if(!gopts || !is_valid_cls(wcls) || (widx < 0))
    return (shared_opts *)NULL;

  shopts = NULL;
  switch(wcls) {
    case CLS_CPU:
      if(widx >= MAX_CPUS)
        break;

      shopts = &gopts->cpu[widx].shopts;
      break;

    case CLS_MEM:
      if(widx >= MAX_MEMS)
        break;

      shopts = &gopts->mem[widx].shopts;
      break;

    case CLS_DISK:
      if(widx >= MAX_DIOS)
        break;

      shopts = &gopts->disk_io[widx].shopts;
      break;

    case CLS_NET:
      if(widx >= MAX_NIOS)
        break;

      shopts = &gopts->net_io[widx].shopts;
      break;

    default:
      break;
  }

  return shopts;
}

static workerID get_next_workerID(void)
{
  return next_workerID++;
}

/*
 * Worker-class specific synchronization initialization functions.
 */
static int init_cpu_opts(gamut_opts *gopts)
{
  int i;
  
  if(!gopts)
    return -1;

  for(i = 0;i < MAX_CPUS;i++) {
    int rc;

    rc = pthread_mutex_init(&gopts->cpu[i].shopts.t_sync.lock,
                            (pthread_mutexattr_t *)NULL);
    if(rc) {
      s_log(G_WARNING, "Error initializing CPU %02d mutex.\n", i);
      goto fail_out;
    }
    rc = pthread_cond_init(&gopts->cpu[i].shopts.t_sync.cond,
                           (pthread_condattr_t *)NULL);
    if(rc) {
      s_log(G_WARNING, "Error initializing CPU %02d condition variable.\n", i);
      goto fail_out;
    }
    gopts->cpu[i].shopts.lockpos = CPU_BASE_LOCK_IDX + i;
    gopts->cpu[i].shopts.wcls    = CLS_CPU;
    gopts->cpu[i].shopts.widx    = (uint32_t)i;

    /*
     * Set all values to 0
     */
    clean_cpu_opts(&gopts->cpu[i], WC_NOKEEPID);
  }

  return 0;

fail_out:
  return -1;
}

static int init_mem_opts(gamut_opts *gopts)
{
  int i;

  if(!gopts)
    return -1;

  for(i = 0;i < MAX_MEMS;i++) {
    int rc;

    rc = pthread_mutex_init(&gopts->mem[i].shopts.t_sync.lock,
                            (pthread_mutexattr_t *)NULL);
    if(rc) {
      s_log(G_WARNING, "Error initializing memory %02d mutex.\n", i);
      goto fail_out;
    }
    rc = pthread_cond_init(&gopts->mem[i].shopts.t_sync.cond,
                           (pthread_condattr_t *)NULL);
    if(rc) {
      s_log(G_WARNING, "Error initializing memory %02d condition variable.\n",
                       i);
      goto fail_out;
    }
    gopts->mem[i].shopts.lockpos = MEM_BASE_LOCK_IDX + i;
    gopts->mem[i].shopts.wcls    = CLS_MEM;
    gopts->mem[i].shopts.widx    = (uint32_t)i;

    /*
     * Set all values to 0
     */
    clean_mem_opts(&gopts->mem[i], WC_NOKEEPID);
  }

  return 0;

fail_out:
  return -1;
}

static int init_dio_opts(gamut_opts *gopts)
{
  int i;

  if(!gopts)
    return -1;

  for(i = 0;i < MAX_DIOS;i++) {
    int rc;

    rc = pthread_mutex_init(&gopts->disk_io[i].shopts.t_sync.lock,
                            (pthread_mutexattr_t *)NULL);
    if(rc) {
      s_log(G_WARNING, "Error initializing disk %02d mutex.\n", i);
      goto fail_out;
    }
    rc = pthread_cond_init(&gopts->disk_io[i].shopts.t_sync.cond,
                           (pthread_condattr_t *)NULL);
    if(rc) {
      s_log(G_WARNING, "Error initializing disk %02d condition variable.\n", i);
      goto fail_out;
    }
    gopts->disk_io[i].shopts.lockpos = DIO_BASE_LOCK_IDX + i;
    gopts->disk_io[i].shopts.wcls    = CLS_DISK;
    gopts->disk_io[i].shopts.widx    = (uint32_t)i;

    /*
     * Set all values to 0
     */
    clean_dio_opts(&gopts->disk_io[i], WC_NOKEEPID);
  }

  return 0;

fail_out:
  return -1;
}

static int init_nio_opts(gamut_opts *gopts)
{
  int i;

  if(!gopts)
    return -1;

  for(i = 0;i < MAX_NIOS;i++) {
    int rc;

    rc = pthread_mutex_init(&gopts->net_io[i].shopts.t_sync.lock,
                            (pthread_mutexattr_t *)NULL);
    if(rc) {
      s_log(G_WARNING, "Error initializing network %02d mutex.\n", i);
      goto fail_out;
    }
    rc = pthread_cond_init(&gopts->net_io[i].shopts.t_sync.cond,
                           (pthread_condattr_t *)NULL);
    if(rc) {
      s_log(G_WARNING, "Error initializing network %02d condition variable.\n",
                       i);
      goto fail_out;
    }
    gopts->net_io[i].shopts.lockpos = NIO_BASE_LOCK_IDX + i;
    gopts->net_io[i].shopts.wcls    = CLS_NET;
    gopts->net_io[i].shopts.widx    = (uint32_t)i;

    /*
     * Set all values to 0
     */
    clean_nio_opts(&gopts->net_io[i], WC_NOKEEPID);
  }

  return 0;

fail_out:
  return -1;
}

/*
 * Parse the class-specific options and fill in the data structures.
 */
static int parse_cpu_opts(gamut_opts *gopts, cpu_opts *cpu, char *attrs)
{
  char *q;
  char *args[NUM_CPU_OPTS];
  int i;
  int rc;
  int nargs;
  int args_done[NUM_CPU_OPTS];
  cpu_opts tcpu;

  if(!gopts || !cpu || !attrs)
    return -1;

  memset(args_done, 0, sizeof(args_done));

  /*
   * If the struct we are given is already in use, only update
   *   the portions of the struct that are specified in attrs.
   *
   * NOTE: We CANNOT update the 'after' or 'label' portion of a
   *       struct in use.
   */
  if(cpu->shopts.used) {
    copy_cpu_opts(cpu, &tcpu, WC_NOKEEPID);
  }
  else {
    memset(&tcpu, 0, sizeof(tcpu));
  }

  nargs = split(",", attrs, args, NUM_CPU_OPTS, ws_keep);
  if(nargs < 1)
    goto fail_out;

  for(i = 0;i < nargs;i++) {
    char *pargs[2];
    int npargs;

    npargs = split("=", args[i], pargs, 2, ws_keep);
    if(npargs != 2)
      goto fail_out;

    if(!strcmp("load", pargs[0])) {
#define CPU_LOAD_ARG 0
      if(args_done[CPU_LOAD_ARG]++)
        goto fail_out;

      errno = 0;
      tcpu.percent_cpu = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
    }
    else if(!strcmp("burn", pargs[0])) {
#define CPU_BURN_ARG (CPU_LOAD_ARG + 1)
      if(args_done[CPU_BURN_ARG]++)
        goto fail_out;

      tcpu.cbfunc = get_burn_function_by_label(pargs[1]);
      if(!tcpu.cbfunc)
        goto fail_out;
    }
    else if(!strcmp("etime", pargs[0])) {
#define CPU_ETIME_ARG (CPU_BURN_ARG + 1)
      if(args_done[CPU_ETIME_ARG]++)
        goto fail_out;

      errno = 0;
      tcpu.shopts.exec_time = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
    }
    else if(!strcmp("work", pargs[0])) {
#define CPU_WORK_ARG (CPU_ETIME_ARG + 1)
      if(args_done[CPU_WORK_ARG]++)
        goto fail_out;

      errno = 0;
      tcpu.shopts.max_work = (uint64_t)strtoull(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tcpu.shopts.max_work *= get_multiplier(q);
    }
    else if(!strcmp("label", pargs[0])) {
#define CPU_LABEL_ARG (CPU_WORK_ARG + 1)
      if(args_done[CPU_LABEL_ARG]++)
        goto fail_out;
      if(tcpu.shopts.used)
        goto fail_out;

      if(strlen(pargs[1]) > SMBUFSIZE)
        goto fail_out;
      strncpy(tcpu.shopts.label, pargs[1], SMBUFSIZE);
    }
    else if(!strcmp("after", pargs[0])) {
#define CPU_AFTER_ARG (CPU_LABEL_ARG + 1)
      args_done[CPU_AFTER_ARG]++;
      if(args_done[CPU_AFTER_ARG] >= MAX_AFTERS)
        goto fail_out;
      if(tcpu.shopts.used)
        goto fail_out;

      if(strlen(pargs[1]) > SMBUFSIZE)
        goto fail_out;
      strncpy(tcpu.shopts.after[tcpu.shopts.num_afters],
              pargs[1], SMBUFSIZE);
      tcpu.shopts.num_afters++;

      /*
       * Seeing as we can only even fill in this value
       *   if we're not going to cause problems, tag it.
       */
      tcpu.shopts.waiting = 1;
    }
    else {
      goto fail_out;
    }
  }

  /*
   * Provide a CPU usage function if one was not specified.
   */
  if(!tcpu.cbfunc) {
    tcpu.cbfunc = get_burn_function_by_label(NULL);
  }

  /*
   * If the struct is not in use, that means that it is new.
   *   We should provide it with a worker ID and a label.
   */
  if(!tcpu.shopts.used) {
    /*
     * Provide a label if one was not specified.
     */
    tcpu.shopts.wid = get_next_workerID();
    if(!strlen(tcpu.shopts.label)) {
      (void)snprintf(tcpu.shopts.label, SMBUFSIZE, "CPU%05u",
                     tcpu.shopts.wid);
    }
  }

  rc = validate_cpu_opts(gopts, &tcpu);
  if(rc <= 0) {
    goto fail_out;
  }

  /*
   * If the temporary CPU struct does not have the 'used' flag set,
   *   this means that this is a new struct.  Tell it we're now
   *   in use.
   */
  if(!tcpu.shopts.used) {
    tcpu.shopts.used = 1;
    gopts->wstats.workers_parsed++;
  }

  /*
   * Regardless of whether this is new or used, set the dirty flag
   *   so the worker knows to reload values.
   */
  tcpu.shopts.dirty = 1;

  copy_cpu_opts(&tcpu, cpu, WC_NOKEEPID);

  return 0;

fail_out:
  /*
   * We can just bail, since all the work was done
   *   on a temporary struct.
   */
  return -1;
}

static int parse_mem_opts(gamut_opts *gopts, mem_opts *mem, char *attrs)
{
  char *args[NUM_MEM_OPTS];
  int i;
  int rc;
  int nargs;
  int args_done[NUM_MEM_OPTS]; 
  mem_opts tmem;

  if(!gopts || !mem || !attrs)
    return -1;

  memset(&args_done, 0, sizeof(args_done));

  if(mem->shopts.used) {
    copy_mem_opts(mem, &tmem, WC_NOKEEPID);
  }
  else {
    memset(&tmem, 0, sizeof(tmem));
  }

  errno = 0;
  nargs = split(",", attrs, args, NUM_MEM_OPTS, ws_is_delim);
  if(nargs < 1)
    goto fail_out;

  for(i = 0;i < nargs;i++) {
    char *q;
    char *pargs[2];
    int npargs;

    npargs = split("=", args[i], pargs, 2, ws_keep);
    if(npargs != 2)
      goto fail_out;

    if(!strcmp("total", pargs[0])) {
#define MEM_TOTAL_ARG 0
      if(args_done[MEM_TOTAL_ARG]++)
        goto fail_out;

      errno = 0;
      tmem.total_ram = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tmem.total_ram *= get_multiplier(q);
    }
    else if(!strcmp("wset", pargs[0])) {
#define MEM_WSET_ARG  (MEM_TOTAL_ARG + 1)
      if(args_done[MEM_WSET_ARG]++)
        goto fail_out;

      errno = 0;
      tmem.working_ram = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tmem.working_ram *= get_multiplier(q);
    }
    else if(!strcmp("blksize", pargs[0])) {
#define MEM_BSIZE_ARG (MEM_WSET_ARG + 1)
      if(args_done[MEM_BSIZE_ARG]++)
        goto fail_out;

      errno = 0;
      tmem.blksize = (uint64_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tmem.blksize *= get_multiplier(q);
    }
    else if(!strcmp("iorate", pargs[0])) {
#define MEM_IORATE_ARG (MEM_BSIZE_ARG + 1)
      if(args_done[MEM_IORATE_ARG]++)
        goto fail_out;

      errno = 0;
      tmem.iorate = (uint64_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tmem.iorate *= get_multiplier(q);
    }
    else if(!strcmp("stride", pargs[0])) {
#define MEM_STRIDE_ARG (MEM_IORATE_ARG + 1)
      if(args_done[MEM_STRIDE_ARG]++)
        goto fail_out;

      errno = 0;
      tmem.stride = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
    }
    else if(!strcmp("etime", pargs[0])) {
#define MEM_ETIME_ARG (MEM_STRIDE_ARG + 1)
      if(args_done[MEM_ETIME_ARG]++)
        goto fail_out;

      errno = 0;
      tmem.shopts.exec_time = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
    }
    else if(!strcmp("work", pargs[0])) {
#define MEM_WORK_ARG (MEM_ETIME_ARG + 1)
      if(args_done[MEM_WORK_ARG]++)
        goto fail_out;

      errno = 0;
      tmem.shopts.max_work = (uint64_t)strtoull(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tmem.shopts.max_work *= get_multiplier(q);
    }
    else if(!strcmp("label", pargs[0])) {
#define MEM_LABEL_ARG (MEM_WORK_ARG + 1)
      if(args_done[MEM_LABEL_ARG]++)
        goto fail_out;
      if(tmem.shopts.used)
        goto fail_out;

      if(strlen(pargs[1]) > SMBUFSIZE)
        goto fail_out;
      strncpy(tmem.shopts.label, pargs[1], SMBUFSIZE);
    }
    else if(!strcmp("after", pargs[0])) {
#define MEM_AFTER_ARG (MEM_LABEL_ARG + 1)
      args_done[MEM_AFTER_ARG]++;
      if(args_done[MEM_AFTER_ARG] >= MAX_AFTERS)
        goto fail_out;
      if(tmem.shopts.used)
        goto fail_out;

      if(strlen(pargs[1]) > SMBUFSIZE)
        goto fail_out;
      strncpy(tmem.shopts.after[tmem.shopts.num_afters],
              pargs[1], SMBUFSIZE);
      tmem.shopts.num_afters++;
      tmem.shopts.waiting = 1;
    }
    else {
      goto fail_out;
    }
  }

  if(!tmem.shopts.used) {
    tmem.shopts.wid = get_next_workerID();
    if(!strlen(tmem.shopts.label)) {
      (void)snprintf(tmem.shopts.label, SMBUFSIZE, "MEM%05u",
                     tmem.shopts.wid);
    }
  }

  s_log(G_DEBUG, "Will try to validate opts of %s.\n", tmem.shopts.label);

  rc = validate_mem_opts(gopts, &tmem);
  if(rc <= 0)
    goto fail_out;

  if(!tmem.shopts.used) {
    tmem.shopts.used = 1;
    gopts->wstats.workers_parsed++;
  }

  tmem.shopts.dirty = 1;

  copy_mem_opts(&tmem, mem, WC_NOKEEPID);

  return 0;

fail_out:

  return -1;
}

static int parse_dio_opts(gamut_opts *gopts, dio_opts *dio, char *attrs)
{
  char *args[NUM_DIO_OPTS];
  int i;
  int rc;
  int nargs;
  int args_done[NUM_DIO_OPTS];
  dio_opts tdio;

  if(!gopts || !dio || !attrs) {
    return -1;
  }

  memset(args_done, 0, sizeof(args_done));

  if(dio->shopts.used) {
    copy_dio_opts(dio, &tdio, WC_NOKEEPID);
  }
  else {
    memset(&tdio, 0, sizeof(tdio));
  }

  nargs = split(",", attrs, args, NUM_DIO_OPTS, ws_is_delim);
  if(nargs < 1)
    goto fail_out;

  for(i = 0;i < nargs;i++) {
    char *q;
    char *pargs[2];
    int npargs;

    npargs = split("=", args[i], pargs, 2, ws_keep);
    if(npargs != 2)
      goto fail_out;

    if(!strcmp("file", pargs[0])) {
      int flen;

#define DIO_FILE_ARG 0
      if(args_done[DIO_FILE_ARG]++)
        goto fail_out;

      if(tdio.file) {
        goto fail_out;
      }

      flen = strlen(pargs[1]);
      if(!flen)
        goto fail_out;

      tdio.file = (char *)malloc(flen + 1);
      if(!tdio.file)
        goto fail_out;

      strncpy(tdio.file, pargs[1], flen);
      tdio.file[flen] = '\0';
    }
    else if(!strcmp("blksize", pargs[0])) {
#define DIO_BSIZE_ARG (DIO_FILE_ARG + 1)
      if(args_done[DIO_BSIZE_ARG]++)
        goto fail_out;

      errno = 0;
      tdio.blksize = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tdio.blksize *= get_multiplier(q);
    }
    else if(!strcmp("nblks", pargs[0])) {
#define DIO_NBLKS_ARG (DIO_BSIZE_ARG + 1)
      if(args_done[DIO_NBLKS_ARG]++)
        goto fail_out;

      errno = 0;
      tdio.nblks = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tdio.nblks *= get_multiplier(q);
    }
    else if(!strcmp("iorate", pargs[0])) {
#define DIO_IORATE_ARG (DIO_NBLKS_ARG + 1)
      if(args_done[DIO_IORATE_ARG]++)
        goto fail_out;

      errno = 0;
      tdio.iorate = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tdio.iorate *= get_multiplier(q);
    }
    else if(!strcmp("sync", pargs[0])) {
#define DIO_SYNC_ARG (DIO_IORATE_ARG + 1)
      if(args_done[DIO_SYNC_ARG]++)
        goto fail_out;

      errno = 0;
      tdio.sync_f = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tdio.sync_f *= get_multiplier(q);
    }
    else if(!strcmp("mode", pargs[0])) {
#define DIO_MODE_ARG (DIO_SYNC_ARG + 1)
      if(args_done[DIO_MODE_ARG]++)
        goto fail_out;

      errno = 0;
      tdio.create = (uint16_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
    }
    else if(!strcmp("iomix", pargs[0])) {
      char *spargs[3];
      int nspargs;

#define DIO_IOMIX_ARG (DIO_MODE_ARG + 1)
      if(args_done[DIO_IOMIX_ARG]++)
        goto fail_out;

      nspargs = split("/", pargs[1], spargs, 3, ws_is_delim);
      if(nspargs != 3)
        goto fail_out;

      errno = 0;
      tdio.iomix.numrds = (uint16_t)strtoul(spargs[0], &q, 10);
      if(errno || (spargs[0] == q))
        goto fail_out;

      errno = 0;
      tdio.iomix.numwrs = (uint16_t)strtoul(spargs[1], &q, 10);
      if(errno || (spargs[1] == q))
        goto fail_out;

      errno = 0;
      tdio.iomix.numsks = (uint16_t)strtoul(spargs[2], &q, 10);
      if(errno || (spargs[2] == q))
        goto fail_out;
    }
    else if(!strcmp("etime", pargs[0])) {
#define DIO_ETIME_ARG (DIO_IOMIX_ARG + 1)
      if(args_done[DIO_ETIME_ARG]++)
        goto fail_out;

      errno = 0;
      tdio.shopts.exec_time = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
    }
    else if(!strcmp("work", pargs[0])) {
#define DIO_WORK_ARG (DIO_ETIME_ARG + 1)
      if(args_done[DIO_WORK_ARG]++)
        goto fail_out;

      errno = 0;
      tdio.shopts.max_work = (uint64_t)strtoull(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tdio.shopts.max_work *= get_multiplier(q);
    }
    else if(!strcmp("label", pargs[0])) {
#define DIO_LABEL_ARG (DIO_WORK_ARG + 1)
      if(args_done[DIO_LABEL_ARG]++)
        goto fail_out;
      if(tdio.shopts.used)
        goto fail_out;

      if(strlen(pargs[1]) > SMBUFSIZE)
        goto fail_out;
      strncpy(tdio.shopts.label, pargs[1], SMBUFSIZE);
    }
    else if(!strcmp("after", pargs[0])) {
#define DIO_AFTER_ARG (DIO_LABEL_ARG + 1)
      args_done[DIO_AFTER_ARG]++;
      if(args_done[DIO_AFTER_ARG] >= MAX_AFTERS)
        goto fail_out;
      if(tdio.shopts.used)
        goto fail_out;

      if(strlen(pargs[1]) > SMBUFSIZE)
        goto fail_out;
      strncpy(tdio.shopts.after[tdio.shopts.num_afters],
              pargs[1], SMBUFSIZE);
      tdio.shopts.num_afters++;
      tdio.shopts.waiting = 1;
    }

    else {
      s_log(G_WARNING, "Unknown disk option: %s\n", pargs[0]);
      goto fail_out;
    }
  }

  if(!tdio.shopts.used) {
    tdio.shopts.wid = get_next_workerID();
    if(!strlen(tdio.shopts.label)) {
      (void)snprintf(tdio.shopts.label, SMBUFSIZE, "DSK%05u",
                     tdio.shopts.wid);
    }
  }

  rc = validate_dio_opts(gopts, &tdio);
  if(rc <= 0) {
    goto fail_out;
  }

  if(!tdio.shopts.used) {
    tdio.shopts.used = 1;
    gopts->wstats.workers_parsed++;
  }

  tdio.shopts.dirty = 1;

  copy_dio_opts(&tdio, dio, WC_NOKEEPID);

  return 0;

fail_out:

  return -1;
}

static int parse_nio_opts(gamut_opts *gopts, nio_opts *nio, char *attrs)
{
  char *args[NUM_NIO_OPTS];
  int i;
  int rc;
  int nargs;
  int args_done[NUM_NIO_OPTS];
  nio_opts tnio;
 
  if(!gopts || !nio || !attrs)
    return -1;

  memset(args_done, 0, sizeof(args_done));

  if(nio->shopts.used) {
    copy_nio_opts(nio, &tnio, WC_NOKEEPID);
  }
  else {
    memset(&tnio, 0, sizeof(tnio));
  }

  tnio.protocol = (uint16_t)-1;

  nargs = split(",", attrs, args, NUM_NIO_OPTS, ws_is_delim);
  if(nargs < 1)
    goto fail_out;

  for(i = 0;i < nargs;i++) {
    char *q;
    char *pargs[2];
    int npargs;

    npargs = split("=", args[i], pargs, 2, ws_keep);
    if(npargs != 2)
      goto fail_out;

    if(!strcmp("addr", pargs[0])) {
#define NIO_ADDR_ARG 0
      if(args_done[NIO_ADDR_ARG]++)
        goto fail_out;

      rc = host_lookup(pargs[1], &tnio.addr);
      if(rc < 0)
        goto fail_out;
    }
    else if(!strcmp("port", pargs[0])) {
#define NIO_PORT_ARG (NIO_ADDR_ARG + 1)
      if(args_done[NIO_PORT_ARG]++)
        goto fail_out;

      errno = 0;
      tnio.port = (uint16_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
    }
    else if(!strcmp("proto", pargs[0])) {
#define NIO_PROTO_ARG (NIO_PORT_ARG + 1)
      if(args_done[NIO_PROTO_ARG]++)
        goto fail_out;

      if(!strcasecmp(pargs[1], "udp"))
        tnio.protocol = IPPROTO_UDP;
      else if(!strcasecmp(pargs[1], "tcp"))
        tnio.protocol = IPPROTO_TCP;
      else
        goto fail_out;
    }
    else if(!strcmp("mode", pargs[0])) {
#define NIO_MODE_ARG (NIO_PROTO_ARG + 1)
      if(args_done[NIO_MODE_ARG]++)
        goto fail_out;

      /*
       * We have whether this is read-only or write-only.
       *   Mode is one character only -- 'r' or 'w' -- so
       *   the second character should be a terminating NULL.
       */
      if(pargs[1][1] != '\0')
        goto fail_out;
      else if((char)tolower(pargs[1][0]) == 'r')
        tnio.mode = O_RDONLY;
      else if((char)tolower(pargs[1][0]) == 'w')
        tnio.mode = O_WRONLY;
      else
        goto fail_out;
    }
    else if(!strcmp("pktsize", pargs[0])) {
#define NIO_PSIZE_ARG (NIO_MODE_ARG + 1)
      if(args_done[NIO_PSIZE_ARG]++)
        goto fail_out;

      errno = 0;
      tnio.pktsize = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tnio.pktsize *= get_multiplier(q);
    }
    else if(!strcmp("iorate", pargs[0])) {
#define NIO_IORATE_ARG (NIO_PSIZE_ARG + 1)
      if(args_done[NIO_IORATE_ARG]++)
        goto fail_out;

      errno = 0;
      tnio.iorate = (uint64_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tnio.iorate *= get_multiplier(q);
    }
    else if(!strcmp("etime", pargs[0])) {
#define NIO_ETIME_ARG (NIO_IORATE_ARG + 1)
      if(args_done[NIO_ETIME_ARG]++)
        goto fail_out;

      errno = 0;
      tnio.shopts.exec_time = (uint32_t)strtoul(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out; 
    }
    else if(!strcmp("work", pargs[0])) {
#define NIO_WORK_ARG (NIO_ETIME_ARG + 1)
      if(args_done[NIO_WORK_ARG]++)
        goto fail_out;

      errno = 0;
      tnio.shopts.max_work = (uint64_t)strtoull(pargs[1], &q, 10);
      if(errno || (pargs[1] == q))
        goto fail_out;
      tnio.shopts.max_work *= get_multiplier(q);
    }
    else if(!strcmp("label", pargs[0])) {
#define NIO_LABEL_ARG (NIO_WORK_ARG + 1)
      if(args_done[NIO_LABEL_ARG]++)
        goto fail_out;
      if(tnio.shopts.used)
        goto fail_out;

      if(strlen(pargs[1]) > SMBUFSIZE)
        goto fail_out;
      strncpy(tnio.shopts.label, pargs[1], SMBUFSIZE);
    }
    else if(!strcmp("after", pargs[0])) {
#define NIO_AFTER_ARG (NIO_LABEL_ARG + 1)
      args_done[NIO_AFTER_ARG]++;
      if(args_done[NIO_AFTER_ARG] >= MAX_AFTERS)
        goto fail_out;
      if(tnio.shopts.used)
        goto fail_out;

      if(strlen(pargs[1]) > SMBUFSIZE)
        goto fail_out;
      strncpy(tnio.shopts.after[tnio.shopts.num_afters],
              pargs[1], SMBUFSIZE);
      tnio.shopts.num_afters++;
      tnio.shopts.waiting = 1;
    }
    else {
      goto fail_out;
    }
  }

  if(!tnio.shopts.used) {
    tnio.shopts.wid = get_next_workerID();
    if(!strlen(tnio.shopts.label)) {
      (void)snprintf(tnio.shopts.label, SMBUFSIZE, "NET%05u",
                     tnio.shopts.wid);
    }
  }

  rc = validate_nio_opts(gopts, nio);
  if(rc <= 0)
    goto fail_out;

  if(!tnio.shopts.used) {
    tnio.shopts.used = 1;
    gopts->wstats.workers_parsed++;
  }

  tnio.shopts.dirty = 1;

  copy_nio_opts(&tnio, nio, WC_NOKEEPID);

  return 0;

fail_out:

  return -1;
}

/*
 * Shared structure copying functions.
 */
#define copy_shared(s, d) { \
  d->shopts.start_time.tv_sec  = s->shopts.start_time.tv_sec; \
  d->shopts.start_time.tv_usec = s->shopts.start_time.tv_usec; \
  d->shopts.mod_time.tv_sec    = s->shopts.mod_time.tv_sec; \
  d->shopts.mod_time.tv_usec   = s->shopts.mod_time.tv_usec; \
  d->shopts.missed_deadlines   = s->shopts.missed_deadlines; \
  d->shopts.missed_usecs       = s->shopts.missed_usecs; \
  d->shopts.total_deadlines    = s->shopts.total_deadlines; \
  d->shopts.link_work   = s->shopts.link_work; \
  d->shopts.prev_worker = s->shopts.prev_worker; \
  d->shopts.next_worker = s->shopts.next_worker; \
  d->shopts.max_work    = s->shopts.max_work;  \
  d->shopts.exec_time   = s->shopts.exec_time ; \
}

#define copy_shared_id(s, d) { \
    d->shopts.t_sync.tid       = s->shopts.t_sync.tid; \
    d->shopts.t_sync.curr_lock = s->shopts.t_sync.curr_lock; \
    d->shopts.num_afters       = s->shopts.num_afters; \
    d->shopts.wid       = s->shopts.wid; \
    d->shopts.used      = s->shopts.used; \
    d->shopts.pending   = s->shopts.pending; \
    d->shopts.waiting   = s->shopts.waiting; \
    d->shopts.linked    = s->shopts.linked; \
    d->shopts.leading   = s->shopts.leading; \
    d->shopts.running   = s->shopts.running; \
    d->shopts.linkwait  = s->shopts.linkwait; \
    d->shopts.dirty     = s->shopts.dirty; \
    d->shopts.mwait     = s->shopts.mwait; \
    d->shopts.exiting   = s->shopts.exiting; \
    d->shopts.msource   = s->shopts.msource; \
    d->shopts.mdest     = s->shopts.mdest; \
    d->shopts.paused    = s->shopts.paused; \
    d->shopts.padding   = s->shopts.padding; \
    strncpy(d->shopts.label, s->shopts.label, sizeof(d->shopts.label)); \
    memcpy(d->shopts.after, s->shopts.after, MAX_AFTERS * SMBUFSIZE); \
    memcpy(d->shopts.t_sync.lock_order, s->shopts.t_sync.lock_order, \
           sizeof(d->shopts.t_sync.lock_order)); \
}

/*
 * Worker-class specific copying functions.
 */
static int copy_cpu_opts(cpu_opts *src, cpu_opts *dest, int keepID)
{
  if(!src || !dest || (keepID < 0))
    return -1;

  dest->percent_cpu = src->percent_cpu;
  dest->total_work  = src->total_work;
  dest->cbfunc      = src->cbfunc;

  copy_shared(src, dest);
  if(!keepID) {
    copy_shared_id(src, dest);
  }

  return 0;
}

static int copy_mem_opts(mem_opts *src, mem_opts *dest, int keepID)
{
  if(!src || !dest || (keepID < 0))
    return -1;

  dest->total_ram   = src->total_ram;
  dest->working_ram = src->working_ram;
  dest->blksize     = src->blksize;
  dest->iorate      = src->iorate;
  dest->stride      = src->stride;
  dest->ntblks      = src->ntblks;
  dest->nwblks      = src->nwblks;

  copy_shared(src, dest);
  if(!keepID) {
    copy_shared_id(src, dest);
  }

  return 0;
}

static int copy_dio_opts(dio_opts *src, dio_opts *dest, int keepID)
{
  if(!src || !dest || (keepID < 0))
    return -1;

  if(dest->file) {
    free(dest->file);
  }
  dest->file    = src->file;
  dest->blksize = src->blksize;
  dest->nblks   = src->nblks;
  dest->create  = src->create;
  dest->iorate  = src->iorate;
  dest->sync_f  = src->sync_f;
  dest->iomix.numrds = src->iomix.numrds;
  dest->iomix.numwrs = src->iomix.numwrs;
  dest->iomix.numsks = src->iomix.numsks;
  dest->total_diskio = src->total_diskio;
  memcpy(dest->num_diskio, src->num_diskio, sizeof(dest->num_diskio));
  memcpy(dest->io_usec,    src->io_usec,    sizeof(dest->io_usec));

  copy_shared(src, dest);
  if(!keepID) {
    copy_shared_id(src, dest);
  }

  return 0;
}

static int copy_nio_opts(nio_opts *src, nio_opts *dest, int keepID)
{
  if(!src || !dest || (keepID < 0))
    return -1;

  dest->addr     = src->addr;
  dest->port     = src->port;
  dest->protocol = src->protocol;
  dest->mode     = src->mode;
  dest->pktsize  = src->pktsize;
  dest->iorate   = src->iorate;

  copy_shared(src, dest);
  if(!keepID) {
    copy_shared_id(src, dest);
  }

  return 0;
}

/*
 * Validate the correctness/feasibility of the given option struct.
 * All will return 1 on success,
 *                 0 on failure,
 *                -1 on error.
 */
static int validate_cpu_opts(gamut_opts *gopts, cpu_opts *cpu)
{
  int i;
  int rc;
  
  if(!gopts || !cpu)
    return -1;

  if((cpu->percent_cpu > 100) || (cpu->percent_cpu == 0))
    return 0;

  if(!cpu->cbfunc)
    return 0;

  rc = label_count(gopts, cpu->shopts.label);
  if((rc < 0) || (rc > 1)) {
    return 0;
  }
  else if(!rc) {
    if(cpu->shopts.used) {
      /*
       * If this is not a new request (used == 1) but we
       *   couldn't find our label, something is wrong.
       */
       return 0;
    }
  }
  else if(rc == 1) {
    if(!cpu->shopts.used) {
      /*
       * If this is a new request (used == 0), but we found
       *   our label, something is wrong.
       */
      return 0;
    }
  }
  else {
    /* Not really sure how we got here */
    return -1;
  }

  for(i = 0;i < cpu->shopts.num_afters;i++) {
    rc = strcmp(cpu->shopts.label, cpu->shopts.after[i]);
    if(!rc) {
      s_log(G_WARNING, "Worker %s is trying to follow itself.\n",
                       cpu->shopts.label);
      return 0;
    }
  }

  if(cpu->shopts.waiting) {
    rc = validate_after_opts(gopts, &cpu->shopts);
    if(rc < 0) {
      return 0;
    }
  }

  return 1;
}

static int validate_mem_opts(gamut_opts *gopts, mem_opts *mem)
{
  int i;
  int rc;

  if(!gopts || !mem)
    return -1;

  if(mem->total_ram == 0)
    return 0;

  if(mem->working_ram == 0)
    mem->working_ram = mem->total_ram;
  else if(mem->working_ram > mem->total_ram)
    return 0;

  if(mem->iorate == 0)
    return 0;

  /*
   * If there's no blocksize given, use the system pagesize.
   */
  if(mem->blksize == 0) {
    errno = 0;
    mem->blksize = (uint64_t)sysconf(_SC_PAGESIZE);
    if(errno || (mem->blksize == (uint64_t)-1)) {
      return 0;
    }
  }

  if(mem->blksize > mem->working_ram)
    return 0;

  if(mem->working_ram % mem->blksize) {
    s_log(G_DEBUG, "%s: Working set of %llu with %llu-byte blocks "
                   "has %llu bytes remaining.\n", mem->shopts.label,
                   mem->working_ram, mem->blksize,
                   mem->working_ram % mem->blksize);
  }
  mem->ntblks = mem->total_ram / mem->blksize;
  mem->nwblks = mem->working_ram / mem->blksize;

  rc = label_count(gopts, mem->shopts.label);
  if((rc < 0) || (rc > 1)) {
    return 0;
  }
  else if(!rc) {
    if(mem->shopts.used) {
      /*
       * If this is not a new request (used == 1) but we
       *   couldn't find our label, something is wrong.
       */
       return 0;
    }
  }
  else if(rc == 1) {
    if(!mem->shopts.used) {
      /*
       * If this is a new request (used == 0), but we found
       *   our label, something is wrong.
       */
      return 0;
    }
  }
  else {
    /* Not really sure how we got here */
    return -1;
  }

  for(i = 0;i < mem->shopts.num_afters;i++) {
    rc = strcmp(mem->shopts.label, mem->shopts.after[i]);
    if(!rc) {
      s_log(G_WARNING, "Worker %s is trying to follow itself.\n",
                       mem->shopts.label);
      return 0;
    }
  }

  if(mem->shopts.waiting) {
    rc = validate_after_opts(gopts, &mem->shopts);
    if(rc < 0) {
      return 0;
    }
  }

  return 1;
}

/*
 * fname:   file/path name
 * create:  the create/modify permissions on this path;
 *            0 - don't create the file or modify its contents
 *            1 - create only if it doesn't exist (don't overwrite)
 *            2 - create and overwrite if necessary
 * dowrite: are we going to write to this file?
 */
static int validate_dio_opts(gamut_opts *gopts, dio_opts *dio)
{
  char *fname;
  int i;
  int rc;
  int bail;
  int create;
  int dowrite;
  int f_exists;
  struct stat sbuf;

  if(!gopts || !dio)
    return -1;

  fname   = dio->file;
  create  = dio->create;
  dowrite = !!dio->iomix.numwrs;

  if(!fname || !strlen(fname))
    return 0;

  /*
   * Can we find the file that we've been given?
   */
  f_exists = stat(fname, &sbuf);
  if(f_exists == 0) {       /* A 0 indicates that it exists */
    f_exists = 1;
  } else if(f_exists < 0) { /* A -1 indicates problems of some sort */
    f_exists = 0;
  } else {                  /* Not sure what anything else means */
    s_log(G_WARNING, "Error getting stat data for \"%s\": %d.\n",
                     fname, f_exists);
    return -1;
  }

  /*
   * Check to make sure that the right combination of parameters
   *   exists for us to do what we've been directed to do.
   */
  bail = 0;
  if(dowrite) {
    if(create == C_RDONLY) {
      /* If we're writing but not allowed to modify the file, bail. */
      bail = 1;
    }
    else if((create == C_IFNEXIST) && f_exists) {
      /* If we can't overwrite a file that exists, bail */
      bail = 1;
    }
    else {
      /*
       * Make sure the base path exists so we can write
       *   to a file in that base path.
       */
      char *p;
      char *dname;
      int flen;
      struct stat dbuf;

      flen = strlen(fname);
      dname = (char *)malloc(flen + 1);
      if(!dname) {
        s_log(G_WARNING, "Error getting directory string for \"%s\".\n",
                         fname);
        return -1;
      }
      strncpy(dname, fname, flen);

      p = strrchr(dname, '/');
      if(p) {
        *p = '\0';

        rc = stat(dname, &dbuf);
        free(dname);

        /* Bail if we can't find the dir. or the path isn't a dir. */
        if(rc < 0) {
          bail = 1;
        }
        else if(!S_ISDIR(dbuf.st_mode)) {
          bail = 1;
        }
      }
      else {
        /* This must be in the current directory, which is OK */
        free(dname);
      }
    } /* end else */
  } /* end if(dowrite) */
  else { /* !dowrite */
    if(!f_exists) {
      /* If we're just reading, but nothing to read, bail */
      bail = 1;
    }
    else if(!S_ISREG(sbuf.st_mode)) {
      /* If we're just reading but it's not a regular file, bail */
      bail = 1;
    }
    /* If we've gotten here, everything is OK */
  }
  if(bail)
    return 0;

  /*
   * Validate the rest of the dio opts
   */
  if(!dio->blksize || !dio->iorate
     || !(dio->iomix.numrds || dio->iomix.numwrs || dio->iomix.numsks)
    )
  {
    return 0;
  }

  /*
   * If we're just reading, set the number of blocks
   */
  if(!dowrite) {
    uint32_t nblks;
    uint32_t remain;

    nblks  = (uint32_t)(sbuf.st_size / dio->blksize);
    remain = (uint32_t)(sbuf.st_size % dio->blksize);
    if(!nblks) {
      s_log(G_WARNING, "File \"%s\": Requested block size of %u KiB "
                       "is larger than filesize of %u KiB.\n",
                       fname, (uint32_t)(dio->blksize / KILO),
                       (uint32_t)(sbuf.st_size / KILO));
      return 0;
    }
    else if(dio->nblks > nblks) {
      s_log(G_WARNING, "File \"%s\": Asked to use %u blocks, but only "
                       "%u blocks exist (blocksize = %u KiB).\n",
                       fname, dio->nblks, nblks,
                       (uint32_t)(dio->blksize / KILO));
      return 0;
    }
    else if(!dio->nblks) {
      if(remain) {
        s_log(G_DEBUG, "File \"%s\": %u bytes remain after %u blocks "
                       "of size %u B.\n", fname, remain, nblks,
                       dio->blksize);
        return 0;
      }
      else {
        dio->nblks = nblks;
      }
    }
  }

  rc = label_count(gopts, dio->shopts.label);
  if((rc < 0) || (rc > 1)) {
    return 0;
  }
  else if(!rc) {
    if(dio->shopts.used) {
      /*
       * If this is not a new request (used == 1) but we
       *   couldn't find our label, something is wrong.
       */
       return 0;
    }
  }
  else if(rc == 1) {
    if(!dio->shopts.used) {
      /*
       * If this is a new request (used == 0), but we found
       *   our label, something is wrong.
       */
      return 0;
    }
  }
  else {
    /* Not really sure how we got here */
    return -1;
  }

  for(i = 0;i < dio->shopts.num_afters;i++) {
    rc = strcmp(dio->shopts.label, dio->shopts.after[i]);
    if(!rc) {
      s_log(G_WARNING, "Worker %s is trying to follow itself.\n",
                       dio->shopts.label);
      return 0;
    }
  }

  if(dio->shopts.waiting) {
    rc = validate_after_opts(gopts, &dio->shopts);
    if(rc < 0) {
      return 0;
    }
  }

  return 1;
}

static int validate_nio_opts(gamut_opts *gopts, nio_opts *nio)
{
  int i;
  int rc;

  if(!gopts || !nio)
    return -1;

  if((nio->mode != O_RDONLY) && (nio->mode != O_WRONLY))
    nio->mode = O_RDONLY;

  /* If we're going to have I/O with 0.0.0.0, it can be read-only. */
  if((nio->addr == INADDR_ANY) && (nio->mode != O_RDONLY))
    return 0;

  if(!nio->port)
    return 0;

  /* Make sure we have privilege for lower ports */
  if((nio->port <= IPPORT_RESERVED) && getuid() && geteuid())
    return 0;

  if((nio->protocol != IPPROTO_UDP) && (nio->protocol != IPPROTO_TCP))
    nio->protocol = IPPROTO_TCP;

  if(!nio->pktsize)
    return 0;

  if(!nio->iorate)
    return 0;

  rc = label_count(gopts, nio->shopts.label);
  if((rc < 0) || (rc > 1)) {
    return 0;
  }
  else if(!rc) {
    if(nio->shopts.used) {
      /*
       * If this is not a new request (used == 1) but we
       *   couldn't find our label, something is wrong.
       */
       return 0;
    }
  }
  else if(rc == 1) {
    if(!nio->shopts.used) {
      /*
       * If this is a new request (used == 0), but we found
       *   our label, something is wrong.
       */
      return 0;
    }
  }
  else {
    /* Not really sure how we got here */
    return -1;
  }

  for(i = 0;i < nio->shopts.num_afters;i++) {
    rc = strcmp(nio->shopts.label, nio->shopts.after[i]);
    if(!rc) {
      s_log(G_WARNING, "Worker %s is trying to follow itself.\n",
                       nio->shopts.label);
      return 0;
    }
  }

  if(nio->shopts.waiting) {
    rc = validate_after_opts(gopts, &nio->shopts);
    if(rc < 0) {
      return 0;
    }
  }

  return 1;
}

#define clean_shared(t) { \
  t->shopts.link_work   = 0; \
  t->shopts.prev_worker = NULL; \
  t->shopts.next_worker = NULL; \
  t->shopts.max_work    = 0; \
  t->shopts.exec_time   = 0; \
  t->shopts.start_time.tv_sec  = 0; \
  t->shopts.start_time.tv_usec = 0; \
  t->shopts.mod_time.tv_sec    = 0; \
  t->shopts.mod_time.tv_usec   = 0; \
}

#define clean_shared_id(t) { \
    t->shopts.t_sync.tid       = 0; \
    t->shopts.t_sync.curr_lock = 0; \
    t->shopts.num_afters       = 0; \
    t->shopts.wid      = 0; \
    t->shopts.used     = 0; \
    t->shopts.pending  = 0; \
    t->shopts.waiting  = 0; \
    t->shopts.linked   = 0; \
    t->shopts.leading  = 0; \
    t->shopts.running  = 0; \
    t->shopts.linkwait = 0; \
    t->shopts.dirty    = 0; \
    t->shopts.mwait    = 0; \
    t->shopts.exiting  = 0; \
    t->shopts.msource  = 0; \
    t->shopts.mdest    = 0; \
    t->shopts.paused   = 0; \
    t->shopts.padding  = 0; \
    memset(t->shopts.label, 0, sizeof(t->shopts.label)); \
    memset(t->shopts.after, 0, sizeof(t->shopts.after)); \
    memset(t->shopts.t_sync.lock_order, 0, \
           sizeof(t->shopts.t_sync.lock_order)); \
}

void clean_cpu_opts(cpu_opts *cpu, int keepID)
{
  if(!cpu || (keepID < 0))
    return;

  cpu->percent_cpu = 0;
  cpu->total_work  = 0;
  cpu->cbfunc      = (cpu_burn_func)NULL;

  clean_shared(cpu);
  if(!keepID) {
    clean_shared_id(cpu);
  }
}

static void clean_mem_opts(mem_opts *mem, int keepID)
{
  if(!mem || (keepID < 0))
    return;

  mem->total_ram   = 0;
  mem->working_ram = 0;
  mem->iorate      = 0;
  mem->stride      = 0;
  mem->blksize     = 0;
  mem->ntblks      = 0;
  mem->nwblks      = 0;

  clean_shared(mem);
  if(!keepID) {
    clean_shared_id(mem);
  }
}

static void clean_dio_opts(dio_opts *dio, int keepID)
{
  if(!dio || (keepID < 0))
    return;

  if(dio->file)
    free(dio->file);
  dio->file = NULL;
  dio->blksize = 0;
  dio->nblks   = 0;
  dio->create  = 0;
  dio->iorate  = 0;
  dio->sync_f  = 0;
  dio->iomix.numrds = 0;
  dio->iomix.numwrs = 0;
  dio->iomix.numsks = 0;
  dio->total_diskio = 0;
  memset(dio->num_diskio, 0, sizeof(dio->num_diskio));
  memset(dio->io_usec,    0, sizeof(dio->io_usec));

  clean_shared(dio);
  if(!keepID) {
    clean_shared_id(dio);
  }
}

static void clean_nio_opts(nio_opts *nio, int keepID)
{
  if(!nio || (keepID < 0))
    return;

  nio->addr     = 0;
  nio->port     = 0;
  nio->protocol = 0;
  nio->mode     = 0;
  nio->pktsize  = 0;
  nio->iorate   = 0;

  clean_shared(nio);
  if(!keepID) {
    clean_shared_id(nio);
  }
}

/*
 * Validate that an 'after' tag is good, and tell that worker
 *   that they're now leading other workers.
 *
 * NOTE: If we can't find the worker on which we're supposed to wait,
 *       it could be that the worker is already done.  For now, we
 *       assume this is the case and happily charge on.
 *
 * This function return -1 on error,
 *                       0 if we could not confirm the leader worker,
 *                       1 if we could confirm the leader worker.
 */
static int validate_after_opts(gamut_opts *gopts, shared_opts *shopts)
{
  char tmplabels[MAX_AFTERS][SMBUFSIZE];
  int i;
  int rc;
  int frc;
  int ntmplabels;

  if(!gopts || !shopts)
    return -1;

  ntmplabels = 0;
  memset(tmplabels, 0, sizeof(tmplabels));

  /*
   * Make sure we don't have any duplicates.
   */
  for(i = 0;i < shopts->num_afters;i++) {
    int j;

    for(j = 0;j < ntmplabels;j++) {
      if(!strcmp(shopts->after[i], tmplabels[j])) {
        break;
      }
    }

    /*
     * If we got to the end, we haven't seen this label already.
     */
    if(j == ntmplabels) {
      strncpy(tmplabels[j], shopts->after[i], SMBUFSIZE);
      ntmplabels++;
    }
    else {
      s_log(G_DEBUG, "Found duplicate 'after' label.\n");
    }
  }

  /*
   * Copy the unique labels back to the shopts and continue.
   */
  memcpy(tmplabels, shopts->after, ntmplabels * SMBUFSIZE);
  shopts->num_afters = ntmplabels;

  ntmplabels = 0;
  memset(tmplabels, 0, sizeof(tmplabels));

  /*
   * Now go through and validate the labels.
   */
  frc  = -1;
  for(i = 0;i < shopts->num_afters;i++) {
    int aidx;
    shared_opts *ashopts;
    worker_class acls;

    aidx = -1;
    acls = CLS_ALL;
    rc = find_worker_by_label(gopts, &acls, shopts->after[i], &aidx);
    if(rc < 0) {
      break;
    }
    else if(!rc || !is_valid_cls(acls) || (aidx < 0)) {
      /*
       * Couldn't find it.  Continue to the next one.
       */
      continue;
    }

    /*
     * We've found it.  Lock the worker and tell them they're leading.
     */
    rc = lock_worker(gopts, acls, aidx);
    if(rc < 0) {
      break;
    }

    ashopts = get_shared_opts(gopts, acls, aidx);
    if(!ashopts) {
      (void)unlock_worker(gopts, acls, aidx);
      break;
    }

    /*
     * If this worker is not already leading other workers,
     *   then do the bookkeeping.
     */
    if(!ashopts->leading) {
      ashopts->leading = 1;
      gopts->wstats.workers_leading++;
      s_log(G_DEBUG, "Incrementing number of workers leading to %d.\n",
                     gopts->wstats.workers_leading);
    }
    strncpy(tmplabels[ntmplabels], shopts->after[i], SMBUFSIZE);
    ntmplabels++;

    rc = unlock_worker(gopts, acls, aidx);
    if(rc < 0) {
      break;
    }
  }

  if(i != shopts->num_afters) {
    /*
     * If we bailed early, spew a warning message since we've
     *   just left a bunch of workers in a weird state:
     *   leading but with no one following.
     */
    s_log(G_WARNING, "Error validating 'after' labels.\n");
    frc = -1;
  }
  else if(!ntmplabels) {
    /*
     * We went through but didn't find anyone to follow.
     */
    shopts->num_afters = 0;
    shopts->waiting    = 0;
    frc                = 0;
  }
  else {
    /*
     * We found workers who are following us.  Copy the names over.
     */
    memcpy(shopts->after, tmplabels, ntmplabels * SMBUFSIZE);
    shopts->num_afters             = ntmplabels;
    shopts->waiting                = 1;
    gopts->wstats.workers_waiting += 1;
    frc                            = ntmplabels;
  }

  return frc;
}

/*
 * Make sure a label is unique (i.e., unused now).
 * This function returns -1 on error,
 *                        0 if the label is not unique, or
 *                        1 if the label is unique.
 */
static int label_count(gamut_opts *gopts, char *label)
{
  int i;
  int count;

  if(!gopts || !label || !strlen(label))
    return -1;

  /*
   * We iterate over the used worker structs and count the number
   *   of times this label shows up.  It should be 1.
   */
  count = 0;
  for(i = 0;i < MAX_CPUS;i++) {
    if(!gopts->cpu[i].shopts.used)
      continue;

    if(!strcmp(gopts->cpu[i].shopts.label, label)) {
      count++;
    }
  }

  for(i = 0;i < MAX_MEMS;i++) {
    if(!gopts->mem[i].shopts.used)
      continue;

    if(!strcmp(gopts->mem[i].shopts.label, label)) {
      count++;
    }
  }

  for(i = 0;i < MAX_DIOS;i++) {
    if(!gopts->disk_io[i].shopts.used)
      continue;

    if(!strcmp(gopts->disk_io[i].shopts.label, label)) {
      count++;
    }
  }

  for(i = 0;i < MAX_NIOS;i++) {
    if(!gopts->net_io[i].shopts.used)
      continue;

    if(!strcmp(gopts->net_io[i].shopts.label, label)) {
      count++;
    }
  }

  return count;
}
