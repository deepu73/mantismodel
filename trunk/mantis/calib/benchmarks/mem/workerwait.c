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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utillog.h"
#include "workeropts.h"
#include "workersync.h"

/*
 * Functions for each of the worker sub-classes.
 */
static int tag_cpu_mwait(gamut_opts *gopts);
static int tag_mem_mwait(gamut_opts *gopts);
static int tag_dio_mwait(gamut_opts *gopts);
static int tag_nio_mwait(gamut_opts *gopts);

/*
 * Tag all workers that will exit on their own
 *   (i.e., have a time limit or a maximum amount of work)
 *   as having the master wait for them.
 */
int tag_worker_mwait(gamut_opts *gopts, worker_class wcls)
{
  int rc;
  int frc;
  int num_tag;
  
  if(!gopts || !is_valid_cls(wcls))
    return -1;

  frc = -1;

  rc = lock_stats(gopts);
  if(rc < 0) {
    goto fail_out;
  }

  rc = lock_class(gopts, wcls);
  if(rc < 0) {
    goto stats_out;
  }

  num_tag = 0;
  switch(wcls) {
    case CLS_CPU:
      num_tag = tag_cpu_mwait(gopts);
      break;
    
    case CLS_MEM:
      num_tag = tag_mem_mwait(gopts);
      break;

    case CLS_DISK:
      num_tag = tag_dio_mwait(gopts);
      break;

    case CLS_NET:
      num_tag = tag_nio_mwait(gopts);
      break;

    default:
      num_tag = -1;
      break;
  }

  if(num_tag < 0) {
    s_log(G_WARNING, "Error tagging waiting workers.\n");
  }
  else {
    s_log(G_DEBUG, "Tagged %d waiting workers for class %d.\n",
                   num_tag, wcls);
    frc = num_tag;
  }

  rc = unlock_class(gopts, wcls);
  if(rc < 0) {
    frc = -1;
  }

stats_out:
  rc = unlock_stats(gopts);
  if(rc < 0) {
    frc = -1;
  }

fail_out:
  return frc;
}

/*
 * Functions for each of the worker sub-classes.
 */
static int tag_cpu_mwait(gamut_opts *gopts)
{
  int i;
  int rc;
  int frc;
  int num_tag;

  frc     = -1;
  num_tag = 0;
  for(i = 0;i < MAX_CPUS;i++) {
    if(!gopts->cpu[i].shopts.used) {
      continue;
    }
    rc = lock_worker(gopts, CLS_CPU, i);
    if(rc < 0) {
      goto fail_out;
    }
    if(gopts->cpu[i].shopts.exec_time
       || gopts->cpu[i].shopts.max_work)
    {
      gopts->cpu[i].shopts.mwait = 1;
      num_tag++;
    }
    else {
      gopts->cpu[i].shopts.mwait = 0;
    }
    rc = unlock_worker(gopts, CLS_CPU, i);
    if(rc < 0) {
      goto fail_out;
    }
  }

  frc = num_tag;

fail_out:
  return frc;
}

static int tag_mem_mwait(gamut_opts *gopts)
{
  int i;
  int rc;
  int frc;
  int num_tag;

  frc     = -1;
  num_tag = 0;
  for(i = 0;i < MAX_MEMS;i++) {
    if(!gopts->mem[i].shopts.used) {
      continue;
    }
    rc = lock_worker(gopts, CLS_MEM, i);
    if(rc < 0) {
      goto fail_out;
    }
    if(gopts->mem[i].shopts.exec_time
       || gopts->mem[i].shopts.max_work)
    {
      gopts->mem[i].shopts.mwait = 1;
      num_tag++;
    }
    else {
      gopts->mem[i].shopts.mwait = 0;
    }
    rc = unlock_worker(gopts, CLS_MEM, i);
    if(rc < 0) {
      goto fail_out;
    }
  }

  frc = num_tag;

fail_out:
  return frc;

}

static int tag_dio_mwait(gamut_opts *gopts)
{
  int i;
  int rc;
  int frc;
  int num_tag;

  frc     = -1;
  num_tag = 0;
  for(i = 0;i < MAX_DIOS;i++) {
    if(!gopts->disk_io[i].shopts.used) {
      continue;
    }
    rc = lock_worker(gopts, CLS_DISK, i);
    if(rc < 0) {
      goto fail_out;
    }
    if(gopts->disk_io[i].shopts.exec_time
       || gopts->disk_io[i].shopts.max_work)
    {
      gopts->disk_io[i].shopts.mwait = 1;
      num_tag++;
    }
    else {
      gopts->disk_io[i].shopts.mwait = 0;
    }
    rc = unlock_worker(gopts, CLS_DISK, i);
    if(rc < 0) {
      goto fail_out;
    }
  }

  frc = num_tag;

fail_out:
  return frc;

}

static int tag_nio_mwait(gamut_opts *gopts)
{
  int i;
  int rc;
  int frc;
  int num_tag;

  frc     = -1;
  num_tag = 0;
  for(i = 0;i < MAX_NIOS;i++) {
    if(!gopts->net_io[i].shopts.used) {
      continue;
    }
    rc = lock_worker(gopts, CLS_NET, i);
    if(rc < 0) {
      goto fail_out;
    }
    if(gopts->net_io[i].shopts.exec_time
       || gopts->net_io[i].shopts.max_work)
    {
      gopts->net_io[i].shopts.mwait = 1;
      num_tag++;
    }
    else {
      gopts->net_io[i].shopts.mwait = 0;
    }
    rc = unlock_worker(gopts, CLS_NET, i);
    if(rc < 0) {
      goto fail_out;
    }
  }

  frc = num_tag;

fail_out:
  return frc;
}
