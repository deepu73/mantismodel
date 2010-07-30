/*
 * Copyright 2004 Justin Moore, justin@cs.duke.edu
 * Copyright 2004 HP Labs, justinm@hpl.hp.com
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#include "utillog.h"
#include "utilrand.h"
#include "calibrate.h"
#include "workeropts.h"

unsigned long long callcnt = 0;
unsigned long long second_count = 0;
unsigned long long prng_count = 0;
unsigned long long select_count = 0;

/*
 * Calibrate this CPU to figure out how high we can count in one second.
 * We use this later on for decent CPU burn rates and exact delays for
 * sub-10ms-sleeps.
 */
void* calibrate_cpu(void *opt)
{
  cpu_opts *cpu;
  struct timeval start;
  struct timeval finish;
  unsigned long long my_count;

  if(!opt)
    return NULL;
  cpu = (cpu_opts *)opt;

  (void)gettimeofday(&start, NULL);
  for(my_count = 0;!cpu->shopts.exiting;my_count++)
    ;
  (void)gettimeofday(&finish, NULL);

  second_count = my_count / (unsigned long long)CALIBRATE_SECONDS;
  {
    int64_t timediff;

    timediff = calculate_timediff(&start, &finish);
    s_log(G_DEBUG, "Target calibration time: %llu usec; "
                   "actual calibration time: %lld usec.\n",
		   (CALIBRATE_SECONDS * US_SEC), timediff);
  }

  return NULL;
}

/*
 * Calibrate the PRNG on this particular machine.  We use it to randomize
 * the data in disk and network I/O so the system can't optimize anything.
 */
void* calibrate_prng(void *opt)
{
  cpu_opts *cpu;
  unsigned long long my_count;

  if(!opt)
    return NULL;

  cpu = (cpu_opts *)opt;
  seedMT((unsigned int)time(NULL));
  for(my_count = 0;!cpu->shopts.exiting;my_count++) {
    unsigned int r;

    r = randomMT();
  }

  prng_count = my_count / (unsigned long long)CALIBRATE_SECONDS;

  return NULL;
}

/*
 * Conduct all of our benchmarks.  We run the benchmarks 'num_trials'
 *   times and take the best from each one.
 */
void benchmark_delays(unsigned int num_trials)
{
  uint32_t i;
  unsigned long long best_cpu_count;
  unsigned long long best_prng_count;

  best_cpu_count  = (signed long long)-1;
  best_prng_count = (signed long long)-1;

  if(!num_trials)
    return;

  for(i = 0;i < num_trials;i++) {
    int rc;
    cpu_opts cpu;

    memset(&cpu, 0, sizeof(cpu_opts));
    rc = pthread_create(&cpu.shopts.t_sync.tid, (pthread_attr_t *)NULL,
                        calibrate_cpu, (void *)&cpu);
    if(rc) {
      s_log(G_WARNING, "Error launching CPU calibration thread %u.\n", i);
      goto fail_out;
    }
    else {
      s_log(G_NOTICE, "Launched CPU calibration thread %u.\n", i);
    }
    sleep(CALIBRATE_SECONDS);
    cpu.shopts.exiting = 1;
    rc = pthread_join(cpu.shopts.t_sync.tid, (void **)NULL);
    if(rc) {
      s_log(G_WARNING, "Error joining CPU calibration thread %u.\n", i);
      goto fail_out;
    }

    memset(&cpu, 0, sizeof(cpu_opts));
    rc = pthread_create(&cpu.shopts.t_sync.tid, (pthread_attr_t *)NULL,
                        calibrate_prng, (void *)&cpu);
    if(rc) {
      s_log(G_WARNING, "Error launching PRNG calibration thread %u.\n", i);
      goto fail_out;
    }
    else {
      s_log(G_NOTICE, "Launched PRNG calibration thread %u.\n", i);
    }
    sleep(CALIBRATE_SECONDS);
    cpu.shopts.exiting = 1;
    rc = pthread_join(cpu.shopts.t_sync.tid, (void **)NULL);
    if(rc) {
      s_log(G_WARNING, "Error joining PRNG calibration thread %u.\n", i);
      goto fail_out;
    }

    s_log(G_INFO, "Trial %i: (%llu, %llu).\n", i, second_count, prng_count);

    if(!i) {
      best_cpu_count  = second_count;
      best_prng_count = prng_count;
    }
    else {
      if(second_count > best_cpu_count)
        best_cpu_count = second_count;
      if(prng_count > best_prng_count)
        best_prng_count = prng_count;
    }
  }

  second_count        = best_cpu_count;
  prng_count          = best_prng_count;

  return;

fail_out:
  second_count = 0;
  prng_count   = 0;

  return;
}

/*
 * Calculate the time difference (in usec) between two timevals.
 */
int64_t calculate_timediff(struct timeval *a, struct timeval *b)
{
  int64_t aval;
  int64_t bval;

  if(!a || !b)
    return -1;

  aval  = a->tv_usec;
  aval += a->tv_sec * US_SEC;

  bval  = b->tv_usec;
  bval += b->tv_sec * US_SEC;

  return (bval - aval);
}
