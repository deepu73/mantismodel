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
#include "cpuworker.h"
#include "utillog.h"
#include "workerctl.h"
#include "workeropts.h"
#include "workersync.h"

void uint64_1_burn(void *cpu, cpu_burn_opts *cbopts);
void uint64_2_burn(void *cpu, cpu_burn_opts *cbopts);
void uint64_3_burn(void *cpu, cpu_burn_opts *cbopts);

void uint64_1_opts(void *cpu, cpu_burn_opts *srcopts,
                   cpu_burn_opts *dstopts);
void uint64_2_opts(void *cpu, cpu_burn_opts *srcopts,
                   cpu_burn_opts *dstopts);
void uint64_3_opts(void *cpu, cpu_burn_opts *srcopts,
                   cpu_burn_opts *dstopts);

typedef struct {
  char          *cbf_label;
  cpu_burn_opts opts;
  cpu_opts_func ofunc;
  cpu_burn_func bfunc;
} cb_func;

cb_func cpu_burn_funcs[] = {
  { "burn64_1", ZERO_CPU_OPTS, uint64_1_opts, uint64_1_burn },
  { "burn64_2", ZERO_CPU_OPTS, uint64_2_opts, uint64_2_burn },
  { "burn64_3", ZERO_CPU_OPTS, uint64_3_opts, uint64_3_burn }
};
uint32_t num_burn_funcs = sizeof(cpu_burn_funcs)
                          / sizeof(cpu_burn_funcs[0]);

/*
 * Get the number of CPU burning functions defined.
 */
int get_num_burn_functions(void)
{
  return num_burn_funcs;
}

/*
 * Find a given function to burn CPU
 *  Calling with a NULL parameter returns the first (default) function.
 */
cpu_burn_func get_burn_function_by_label(char *flabel)
{
  uint32_t i;

  if(!flabel || !strlen(flabel)) {
    if(cpu_burn_funcs[0].bfunc) {
      return cpu_burn_funcs[0].bfunc;
    }
    else {
      return (cpu_burn_func)NULL;
    }
  }

  for(i = 0;i < num_burn_funcs;i++) {
    int rc;

    rc = strcmp(flabel, cpu_burn_funcs[i].cbf_label);
    if(!rc) {
      break;
    }
  }

  if(i == num_burn_funcs) {
    return (cpu_burn_func)NULL;
  }
  else {
    return cpu_burn_funcs[i].bfunc;
  }
}

/*
 * Get a burn function by index.
 */
cpu_burn_func get_burn_function_by_index(int idx)
{
  if((idx < 0) || (idx >= num_burn_funcs))
    return NULL;

  if(cpu_burn_funcs[idx].bfunc) {
    return cpu_burn_funcs[idx].bfunc;
  }
  else {
    return NULL;
  }
}

/*
 * Get the label by index.
 */
char* get_burn_label_by_index(int idx)
{
  if((idx < 0) || (idx >= num_burn_funcs))
    return NULL;

  if(cpu_burn_funcs[idx].cbf_label) {
    return cpu_burn_funcs[idx].cbf_label;
  }
  else {
    return NULL;
  }
}

/*******************************************************************/
/********************** End of extern funcs ************************/
/*******************************************************************/

void uint64_1_burn(void *cpu, cpu_burn_opts *cbopts)
{
  uint64_t cnt;
  cpu_opts *pcpu;

  if(!cpu || !cbopts)
    return;

  pcpu = (cpu_opts *)cpu;

  for(cnt = cbopts->count64;cnt;cnt--)
    ;
  pcpu->total_work += (cbopts->count64 - cnt);
}

void uint64_2_burn(void *cpu, cpu_burn_opts *cbopts)
{
  uint64_t cnt1;
  uint64_t cnt2;
  uint64_t sum;
  cpu_opts *pcpu;

  if(!cpu || !cbopts)
    return;

  pcpu = (cpu_opts *)cpu;

  cnt1 = cnt2 = cbopts->count64;

  while(cnt1) {
    cnt1--;
    cnt2--;
  }
  sum = cnt1 + cnt2;

  pcpu->total_work += ((cbopts->count64 * 2) - sum);
}

void uint64_3_burn(void *cpu, cpu_burn_opts *cbopts)
{
  uint64_t cnt1;
  uint64_t cnt2;
  uint64_t cnt3;
  uint64_t sum;
  cpu_opts *pcpu;

  if(!cpu || !cbopts)
    return;

  pcpu = (cpu_opts *)cpu;

  cnt1 = cnt2 = cnt3 = cbopts->count64;

  while(cnt1) {
    cnt1--;
    cnt2--;
    cnt3--;
  }
  sum = cnt1 + cnt2 + cnt3;

  pcpu->total_work += ((cbopts->count64 * 3) - sum);
}

void uint64_1_opts(void *cpu, cpu_burn_opts *srcopts,
                   cpu_burn_opts *dstopts)
{
  if(!cpu || !srcopts || !dstopts) {
    return;
  }

  return;
}

void uint64_2_opts(void *cpu, cpu_burn_opts *srcopts,
                   cpu_burn_opts *dstopts)
{
  if(!cpu || !srcopts || !dstopts) {
    return;
  }

  return;
}

void uint64_3_opts(void *cpu, cpu_burn_opts *srcopts,
                   cpu_burn_opts *dstopts)
{
  if(!cpu || !srcopts || !dstopts) {
    return;
  }

  return;
}
